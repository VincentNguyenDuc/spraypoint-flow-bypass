#include "tcp/tcp-diagnostics.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ns3;

namespace
{

int failures = 0;

void
Expect(const std::string& name, uint64_t actual, uint64_t expected)
{
    if (actual != expected)
    {
        std::printf("  %s: %llu, expected %llu\n",
                    name.c_str(),
                    static_cast<unsigned long long>(actual),
                    static_cast<unsigned long long>(expected));
        ++failures;
    }
}

void
MockSequence()
{
    rng::TcpDiagnostics diagnostic;
    diagnostic.SynRetransmission(false);
    diagnostic.SynRetransmission(true);
    diagnostic.SynRetransmission(true);
    diagnostic.DataRtoTimeout();
    diagnostic.DataRetransmission(rng::TcpDiagnostics::DataCause::RTO_HANDLER);
    diagnostic.DataRetransmission(rng::TcpDiagnostics::DataCause::FAST_RECOVERY);
    diagnostic.DataRetransmission(rng::TcpDiagnostics::DataCause::OTHER);
    // A terminal data timeout exhausts the retry budget without a transmission.
    diagnostic.DataRtoTimeout();
    diagnostic.Stop();
    diagnostic.Stop();
    diagnostic.SynRetransmission(false);
    diagnostic.SynRetransmission(true);
    diagnostic.DataRtoTimeout();
    diagnostic.DataRetransmission(rng::TcpDiagnostics::DataCause::RTO_HANDLER);
    diagnostic.DataRetransmission(rng::TcpDiagnostics::DataCause::FAST_RECOVERY);
    diagnostic.DataRetransmission(rng::TcpDiagnostics::DataCause::OTHER);
    const auto& counts = diagnostic.Counts();
    Expect("mock SYN retry", counts.synRetransmissions, 1);
    Expect("mock SYN-ACK retries", counts.synAckRetransmissions, 2);
    Expect("mock data total and completion cutoff", counts.dataRetransmissions, 3);
    Expect("mock data timeout event count", counts.dataRtoTimeouts, 2);
    Expect("mock data RTO retransmission", counts.dataRtoRetransmissions, 1);
    Expect("mock data fast recovery", counts.dataFastRecoveryRetransmissions, 1);
    Expect("mock data other", counts.dataOtherRetransmissions, 1);
}

// Observes packets at the receiving device before deterministic loss. Wire
// retransmissions are independently identified by repeated sequence numbers.
class SelectiveLoss : public ErrorModel
{
  public:
    uint32_t synDrops{0};
    uint32_t synAckDrops{0};
    uint32_t dataDrops{0};
    uint32_t synPackets{0};
    uint32_t synAckPackets{0};
    uint32_t dataRetransmissions{0};
    bool dropAllData{false};
    std::vector<std::pair<int64_t, std::vector<uint8_t>>> wire;

  private:
    bool DoCorrupt(Ptr<Packet> original) override
    {
        std::vector<uint8_t> bytes(original->GetSize());
        original->CopyData(bytes.data(), bytes.size());
        wire.emplace_back(Simulator::Now().GetNanoSeconds(), std::move(bytes));
        auto packet = original->Copy();
        PppHeader ppp;
        packet->RemoveHeader(ppp);
        Ipv4Header ip;
        packet->RemoveHeader(ip);
        if (ip.GetProtocol() != 6)
        {
            return false;
        }
        TcpHeader tcp;
        packet->RemoveHeader(tcp);
        if (tcp.GetFlags() & TcpHeader::SYN)
        {
            const bool ack = tcp.GetFlags() & TcpHeader::ACK;
            ++(ack ? synAckPackets : synPackets);
            auto& remaining = ack ? synAckDrops : synDrops;
            if (remaining > 0)
            {
                --remaining;
                return true;
            }
        }
        else if (packet->GetSize() > 0)
        {
            auto& transmissions = m_sequenceTransmissions[tcp.GetSequenceNumber().GetValue()];
            dataRetransmissions += transmissions++ != 0;
            if (dropAllData)
            {
                return true;
            }
            if (dataDrops > 0)
            {
                --dataDrops;
                return true;
            }
        }
        return false;
    }

    void DoReset() override
    {
    }

    std::unordered_map<uint32_t, uint32_t> m_sequenceTransmissions;
};

struct Scenario
{
    const char* name;
    uint32_t bytes{1000};
    uint32_t synDrops{0};
    uint32_t synAckDrops{0};
    uint32_t dataDrops{0};
    uint32_t dataRetries{6};
    bool dropAllData{false};
    bool complete{true};
    uint32_t expectedTimeouts{0};
    uint32_t expectedRtoRetransmissions{0};
    bool expectFastRecovery{false};
};

struct Observation
{
    Ptr<PacketSink> sink;
    Ptr<BulkSendApplication> source;
    std::shared_ptr<rng::TcpDiagnostics> sender;
    std::shared_ptr<rng::TcpDiagnostics> receiver;
    uint32_t expectedBytes{0};
    uint32_t received{0};
    uint32_t lossTransitions{0};
    bool completed{false};
    bool released{false};
    bool instrumented{true};
    int64_t completionNs{0};

    void Attach()
    {
        // StartApplication has already created both sockets, including the
        // initial SYN. Diagnostics were attached in the socket constructor.
        if (instrumented)
        {
            sender = rng::GetTcpDiagnostics(source->GetSocket());
            receiver = rng::GetTcpDiagnostics(sink->GetListeningSocket());
        }
        const auto tcp = source->GetNode()->GetObject<TcpL4Protocol>();
        ObjectMapValue sockets;
        tcp->GetAttribute("SocketList", sockets);
        Expect("sender registered with TcpL4Protocol", sockets.GetN(), 1);
        sink->GetNode()->GetObject<TcpL4Protocol>()->GetAttribute("SocketList", sockets);
        Expect("listener registered before first SYN", sockets.GetN(), 1);
        for (const auto& [protocolAttribute, socketAttribute] :
             {std::pair{"SocketType", "CongestionOps"}, std::pair{"RecoveryType", "RecoveryOps"}})
        {
            TypeIdValue configured;
            PointerValue actual;
            tcp->GetAttribute(protocolAttribute, configured);
            source->GetSocket()->GetAttribute(socketAttribute, actual);
            Expect(std::string("factory preserves ") + protocolAttribute,
                   actual.GetObject()->GetInstanceTypeId() == configured.Get(),
                   true);
        }
        source->GetSocket()->TraceConnectWithoutContext(
            "CongState", MakeCallback(&Observation::OnCongState, this));
    }

    void OnCongState(TcpSocketState::TcpCongState_t, TcpSocketState::TcpCongState_t state)
    {
        lossTransitions += state == TcpSocketState::CA_LOSS;
    }

    void OnRx(Ptr<const Packet> packet, const Address&, const Address&)
    {
        received += packet->GetSize();
        if (!completed && received >= expectedBytes)
        {
            completed = true;
            completionNs = Simulator::Now().GetNanoSeconds();
            if (instrumented)
            {
                sender->Stop();
                receiver->Stop();
            }
            Simulator::Schedule(MilliSeconds(1), &Observation::Release, this);
        }
    }

    void Release()
    {
        for (const auto& socket : sink->GetAcceptedSockets())
        {
            if (instrumented)
            {
                Expect("accepted socket shares listener diagnostics",
                       rng::GetTcpDiagnostics(socket) == receiver,
                       true);
            }
            socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
            socket->Close();
        }
        const auto listener = sink->GetListeningSocket();
        listener->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                    MakeNullCallback<void, Ptr<Socket>, const Address&>());
        listener->SetCloseCallbacks(MakeNullCallback<void, Ptr<Socket>>(),
                                    MakeNullCallback<void, Ptr<Socket>>());
        listener->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        listener->Close();
        const auto socket = source->GetSocket();
        socket->SetConnectCallback(MakeNullCallback<void, Ptr<Socket>>(),
                                   MakeNullCallback<void, Ptr<Socket>>());
        socket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
        sink->Dispose();
        source->Dispose();
        sink = nullptr;
        source = nullptr;
        released = true;
    }
};

struct WireRecord
{
    std::vector<std::pair<int64_t, std::vector<uint8_t>>> forward;
    std::vector<std::pair<int64_t, std::vector<uint8_t>>> reverse;
    int64_t completionNs;
};

WireRecord
RunScenario(const Scenario& scenario, bool instrumented = true)
{
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1000));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));
    Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(MilliSeconds(2)));
    Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(3));
    Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(scenario.dataRetries));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(2)));
    Config::SetDefault("ns3::TcpSocketBase::ClockGranularity", TimeValue(MicroSeconds(1)));
    Config::SetDefault("ns3::TcpSocketBase::MaxSegLifetime", DoubleValue(0.001));
    NodeContainer nodes;
    nodes.Create(2);
    InternetStackHelper internet;
    internet.Install(nodes);
    for (auto node = nodes.Begin(); node != nodes.End(); ++node)
    {
        rng::TcpDiagnosticsSocketFactory::Install(*node);
    }
    PointToPointHelper link;
    link.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    link.SetChannelAttribute("Delay", TimeValue(MicroSeconds(10)));
    const auto devices = link.Install(nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.0.0.0", "255.255.255.0");
    const auto interfaces = addresses.Assign(devices);
    const auto forward = CreateObject<SelectiveLoss>();
    const auto reverse = CreateObject<SelectiveLoss>();
    forward->synDrops = scenario.synDrops;
    forward->dataDrops = scenario.dataDrops;
    forward->dropAllData = scenario.dropAllData;
    reverse->synAckDrops = scenario.synAckDrops;
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(forward));
    devices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(reverse));

    Observation observation;
    observation.expectedBytes = scenario.bytes;
    observation.instrumented = instrumented;
    const char* protocol =
        instrumented ? "rng::TcpDiagnosticsSocketFactory" : "ns3::TcpSocketFactory";
    // Exercise dynamic application creation after simulation startup, as the
    // experiment does, rather than only creating sockets before Run().
    Simulator::Schedule(MicroSeconds(10), [&]() {
        PacketSinkHelper sink(protocol,
                              InetSocketAddress(Ipv4Address::GetAny(), 10000));
        observation.sink = DynamicCast<PacketSink>(sink.Install(nodes.Get(1)).Get(0));
        observation.sink->TraceConnectWithoutContext(
            "RxWithAddresses", MakeCallback(&Observation::OnRx, &observation));
        BulkSendHelper source(protocol,
                              InetSocketAddress(interfaces.GetAddress(1), 10000));
        source.SetAttribute("MaxBytes", UintegerValue(scenario.bytes));
        source.SetAttribute("SendSize", UintegerValue(1000));
        observation.source = DynamicCast<BulkSendApplication>(source.Install(nodes.Get(0)).Get(0));
        Simulator::Schedule(NanoSeconds(1), &Observation::Attach, &observation);
    });
    Simulator::Stop(MilliSeconds(40));
    Simulator::Run();
    const std::string name = std::string(scenario.name) + " ";
    Expect(name + "completion", observation.completed, scenario.complete);
    Expect(name + "release", observation.released, scenario.complete);
    if (scenario.complete)
    {
        for (auto node = nodes.Begin(); node != nodes.End(); ++node)
        {
            ObjectMapValue sockets;
            (*node)->GetObject<TcpL4Protocol>()->GetAttribute("SocketList", sockets);
            Expect(name + "socket removal after release and TIME_WAIT", sockets.GetN(), 0);
        }
    }
    const WireRecord record{forward->wire, reverse->wire, observation.completionNs};
    if (!instrumented)
    {
        Simulator::Destroy();
        return record;
    }
    const auto& sender = observation.sender->Counts();
    const auto& receiver = observation.receiver->Counts();
    Expect(name + "SYN wire retries", sender.synRetransmissions, forward->synPackets - 1);
    Expect(name + "SYN-ACK wire retries",
           receiver.synAckRetransmissions,
           reverse->synAckPackets > 0 ? reverse->synAckPackets - 1 : 0);
    Expect(name + "sender does not send SYN-ACK", sender.synAckRetransmissions, 0);
    Expect(name + "receiver does not send SYN", receiver.synRetransmissions, 0);
    Expect(name + "data wire retransmissions",
           sender.dataRetransmissions,
           forward->dataRetransmissions);
    Expect(name + "data timeout events", sender.dataRtoTimeouts, scenario.expectedTimeouts);
    Expect(name + "data RTO sends",
           sender.dataRtoRetransmissions,
           scenario.expectedRtoRetransmissions);
    Expect(name + "fast recovery",
           sender.dataFastRecoveryRetransmissions > 0,
           scenario.expectFastRecovery);
    Expect(name + "data partition",
           sender.dataRtoRetransmissions + sender.dataFastRecoveryRetransmissions +
               sender.dataOtherRetransmissions,
           sender.dataRetransmissions);
    Expect(name + "receiver data retransmissions", receiver.dataRetransmissions, 0);
    if (scenario.expectedTimeouts > 1)
    {
        Expect(name + "repeated RTO does not require repeated CA_LOSS transition",
               observation.lossTransitions,
               1);
    }
    Simulator::Destroy();
    // All socket/app objects may now be gone; final per-flow values still exist.
    Expect(name + "counters survive destroy",
           observation.sender->Counts().dataRtoTimeouts,
           scenario.expectedTimeouts);
    std::printf("  %s: SYN=%llu SYN-ACK=%llu data=%llu RTO-events=%llu RTO-sends=%llu "
                "fast=%llu other=%llu\n",
                scenario.name,
                static_cast<unsigned long long>(sender.synRetransmissions),
                static_cast<unsigned long long>(receiver.synAckRetransmissions),
                static_cast<unsigned long long>(sender.dataRetransmissions),
                static_cast<unsigned long long>(sender.dataRtoTimeouts),
                static_cast<unsigned long long>(sender.dataRtoRetransmissions),
                static_cast<unsigned long long>(sender.dataFastRecoveryRetransmissions),
                static_cast<unsigned long long>(sender.dataOtherRetransmissions));
    return record;
}

} // namespace

int
main(int argc, char** argv)
{
    MockSequence();
    if (argc == 1 || std::string(argv[1]) != "--mock-only")
    {
        const auto stock = RunScenario({.name = "stock no loss", .bytes = 20000}, false);
        const auto observed = RunScenario({.name = "no loss", .bytes = 20000});
        Expect("stock parity: complete forward wire packets and timing",
               stock.forward == observed.forward,
               true);
        Expect("stock parity: complete reverse wire packets and timing",
               stock.reverse == observed.reverse,
               true);
        Expect("stock parity: completion nanoseconds", observed.completionNs, stock.completionNs);
        std::printf("  stock parity: %zu forward packets, %zu reverse packets, "
                    "completion=%lld ns\n",
                    stock.forward.size(),
                    stock.reverse.size(),
                    static_cast<long long>(stock.completionNs));
        RunScenario({.name = "lost SYN", .synDrops = 1});
        RunScenario({.name = "lost SYN-ACK", .synAckDrops = 1});
        RunScenario({.name = "failed handshake", .synDrops = 100, .complete = false});
        RunScenario({.name = "data RTO",
                     .dataDrops = 1,
                     .expectedTimeouts = 1,
                     .expectedRtoRetransmissions = 1});
        RunScenario({.name = "repeated data RTO",
                     .dataDrops = 2,
                     .expectedTimeouts = 2,
                     .expectedRtoRetransmissions = 2});
        RunScenario({.name = "exhausted data retries",
                     .dataRetries = 1,
                     .dropAllData = true,
                     .complete = false,
                     .expectedTimeouts = 2,
                     .expectedRtoRetransmissions = 1});
        RunScenario({.name = "fast recovery",
                     .bytes = 20000,
                     .dataDrops = 1,
                     .expectFastRecovery = true});
    }
    std::printf("[TCP diagnostics] %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
