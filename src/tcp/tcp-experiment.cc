#include "tcp-experiment.h"

#include "spraypoint/hash.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-layer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace ns3;

namespace rng
{

namespace
{

const uint32_t SEGMENT_BYTES = 1448; // 1500 MTU less IP, TCP, and timestamp headers
const uint8_t INITIAL_TTL = 64;
const uint32_t PATH_BUCKETS = 8; // fabric hops 0..6, then 7 or more

double
Percentile(std::vector<double> values, double quantile)
{
    if (values.empty())
    {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(std::ceil(quantile * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
}

void
PrintFct(std::ostream& out, const char* name, const std::vector<double>& fctUs)
{
    out << "fct_us class=" << name << " count=" << fctUs.size()
        << " p50=" << Percentile(fctUs, 0.50) << " p99=" << Percentile(fctUs, 0.99)
        << " p999=" << Percentile(fctUs, 0.999) << " max=" << Percentile(fctUs, 1.0) << "\n";
}

void
PrintPathLengths(std::ostream& out, const char* name, const std::vector<uint64_t>& counts)
{
    uint64_t total = 0;
    for (auto count : counts)
    {
        total += count;
    }
    out << name;
    for (size_t hops = 0; hops < counts.size(); ++hops)
    {
        out << " L" << hops << "="
            << (total ? static_cast<double>(counts[hops]) / static_cast<double>(total) : 0.0);
    }
    out << "\n";
}

} // namespace

TcpExperiment::TcpExperiment(const TcpExperimentConfig& config)
    : m_config(config),
      m_tables(config.nodes, config.degree, config.waypoints, config.nextHops, config.seed),
      m_smallThreshold(config.smallThreshold ? config.smallThreshold : config.smallBytes)
{
    for (auto& counts : m_pathLengths)
    {
        counts.assign(PATH_BUCKETS, 0);
    }
    if (!config.flowSizeCdf.empty())
    {
        m_flowSizes.emplace(config.flowSizeCdf);
        if (config.smallThreshold == 0)
        {
            throw std::runtime_error("flowSizeCdf needs smallThreshold");
        }
    }
    else if (config.largeBytes <= config.smallBytes)
    {
        throw std::runtime_error("largeBytes must exceed smallBytes");
    }
    if (config.flows == 0 && config.spanMs <= 0)
    {
        throw std::runtime_error("set flows or spanMs to limit the workload");
    }
    if (config.pattern != "uniform")
    {
        throw std::runtime_error("unknown pattern " + config.pattern + "; use uniform");
    }
    if (config.linkGbps <= 0 || config.fabricQueuePackets == 0 || config.nicQueuePackets == 0)
    {
        throw std::runtime_error("link rate and queue sizes must be positive");
    }
    if (config.hostsPerRouter == 0 || config.nodes * config.hostsPerRouter < 2)
    {
        throw std::runtime_error("need at least two hosts");
    }
    if (config.load <= 0 || config.smallBytes == 0 || config.largeBytes == 0)
    {
        throw std::runtime_error("load and flow sizes must be positive");
    }
    if (config.smallFraction < 0 || config.smallFraction > 1)
    {
        throw std::runtime_error("smallFraction must be between 0 and 1");
    }
}

void
TcpExperiment::Run()
{
    RngSeedManager::SetRun(m_config.seed);
    ConfigureTcp();
    BuildNetwork();
    ConnectNodeTraces();
    GenerateFlows();
    Simulator::Stop(MilliSeconds(m_config.maxSimMs));
    Simulator::Run();
    PrintResults(std::cout);
    WriteOutputs();
    Simulator::Destroy();
}

Ptr<SpraypointRouting>
TcpExperiment::CreateRouting(uint32_t router)
{
    return CreateObject<SpraypointRouting>(m_tables, router);
}

const Spraypoint&
TcpExperiment::GetTables() const
{
    return m_tables;
}

void
TcpExperiment::ConfigureTcp() const
{
    // ns-3's defaults assume a WAN: a 536-byte MSS, a 1 s minimum RTO, a 3 s
    // SYN timeout, and a 200 ms delayed ACK.  One such timeout is thousands of
    // RTTs here and would decide every tail percentile.
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(SEGMENT_BYTES));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));
    Config::SetDefault("ns3::TcpSocket::ConnTimeout",
                       TimeValue(MicroSeconds(10 * m_config.minRtoUs)));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(MicroSeconds(m_config.minRtoUs)));
    Config::SetDefault("ns3::TcpSocketBase::ClockGranularity", TimeValue(MicroSeconds(1)));
    if (m_config.releaseDelayUs > 0)
    {
        // A closed sender waits 2 MSL in TIME_WAIT, by default 240 s, before its
        // socket is freed.  Nothing depends on that wait once the flow is done.
        Config::SetDefault("ns3::TcpSocketBase::MaxSegLifetime",
                           DoubleValue(m_config.releaseDelayUs * 1e-6));
    }
}

void
TcpExperiment::BuildNetwork()
{
    m_routers.Create(m_config.nodes);
    m_hosts.Create(m_config.nodes * m_config.hostsPerRouter);

    Ipv4StaticRoutingHelper staticRouting;
    InternetStackHelper stack;
    stack.SetRoutingHelper(staticRouting);
    stack.Install(m_routers);
    stack.Install(m_hosts);

    PointToPointHelper link;
    link.SetDeviceAttribute(
        "DataRate",
        DataRateValue(DataRate(static_cast<uint64_t>(m_config.linkGbps * 1e9))));
    link.SetChannelAttribute("Delay", TimeValue(NanoSeconds(m_config.linkDelayNs)));
    link.SetQueue("ns3::DropTailQueue<Packet>",
                  "MaxSize",
                  QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, m_config.fabricQueuePackets)));

    m_routing.reserve(m_config.nodes);
    for (uint32_t router = 0; router < m_config.nodes; ++router)
    {
        m_routing.push_back(CreateRouting(router));
        m_routers.Get(router)->GetObject<Ipv4>()->SetRoutingProtocol(m_routing.back());
    }

    // Assign addresses by hand, not with Ipv4AddressHelper: it would install an
    // FqCoDel queue disc on every interface and replace the DropTail queues
    // being modeled.  Router interfaces need no address, since SpraypointRouting
    // picks the output device directly and point-to-point links have no ARP.
    const auto bringUp = [](Ptr<Node> node, Ptr<NetDevice> device) {
        auto ipv4 = node->GetObject<Ipv4>();
        ipv4->SetUp(ipv4->AddInterface(device));
    };

    const auto& graph = m_tables.GetGraph();
    for (uint32_t a = 0; a < m_config.nodes; ++a)
    {
        for (auto b : graph[a])
        {
            if (a >= b)
            {
                continue;
            }
            auto pair = link.Install(m_routers.Get(a), m_routers.Get(b));
            bringUp(m_routers.Get(a), pair.Get(0));
            bringUp(m_routers.Get(b), pair.Get(1));
            m_routing[a]->AddFabricPort(b, pair.Get(0));
            m_routing[b]->AddFabricPort(a, pair.Get(1));
            ConnectPort(pair.Get(0), FABRIC, a, b);
            ConnectPort(pair.Get(1), FABRIC, b, a);
        }
    }

    for (uint32_t router = 0; router < m_config.nodes; ++router)
    {
        for (uint32_t index = 0; index < m_config.hostsPerRouter; ++index)
        {
            const auto hostIndex = router * m_config.hostsPerRouter + index;
            auto host = m_hosts.Get(hostIndex);
            auto pair = link.Install(host, m_routers.Get(router));
            const auto address = SpraypointRouting::HostAddress(router, index);

            // A host's only way out is its ToR, and a NIC should not drop what
            // its own TCP sends in a burst; Linux's default txqueuelen is 1000.
            DynamicCast<PointToPointNetDevice>(pair.Get(0))
                ->GetQueue()
                ->SetMaxSize(QueueSize(QueueSizeUnit::PACKETS, m_config.nicQueuePackets));

            auto ipv4 = host->GetObject<Ipv4>();
            const auto interface = ipv4->AddInterface(pair.Get(0));
            ipv4->AddAddress(interface, Ipv4InterfaceAddress(address, Ipv4Mask("/24")));
            ipv4->SetUp(interface);
            // With no ARP the device has one peer, so the gateway is never resolved.
            // It must just differ from the host's address, or the host loops the
            // packet back to itself.
            staticRouting.GetStaticRouting(ipv4)->SetDefaultRoute(
                SpraypointRouting::GatewayAddress(router),
                interface);

            bringUp(m_routers.Get(router), pair.Get(1));
            m_routing[router]->AddHostPort(address, pair.Get(1));
            ConnectPort(pair.Get(0), HOST_NIC, hostIndex, router);
            ConnectPort(pair.Get(1), DOWNLINK, router, hostIndex);
        }
    }
}

void
TcpExperiment::ConnectPort(Ptr<NetDevice> device, PortKind kind, uint32_t from, uint32_t to)
{
    const auto index = static_cast<uint32_t>(m_ports.size());
    m_ports.push_back(Port{.from = from, .to = to, .kind = kind});
    auto p2p = DynamicCast<PointToPointNetDevice>(device);
    p2p->GetQueue()->TraceConnectWithoutContext(
        "PacketsInQueue",
        MakeCallback(&TcpExperiment::OnQueueLength, this).Bind(index));
    p2p->TraceConnectWithoutContext("MacTxDrop", MakeCallback(&TcpExperiment::OnDeviceDrop, this));
    if (kind == FABRIC)
    {
        p2p->TraceConnectWithoutContext("PhyTxEnd",
                                        MakeCallback(&TcpExperiment::OnPortSent, this).Bind(index));
    }
}

void
TcpExperiment::ConnectNodeTraces()
{
    // With no queue disc, a full queue is dropped in the traffic control layer
    // before it reaches the device, so the device's own drop trace stays silent.
    const auto connect = [this](const NodeContainer& nodes, bool atRouter) {
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            nodes.Get(i)->GetObject<TrafficControlLayer>()->TraceConnectWithoutContext(
                "TcDrop",
                MakeCallback(&TcpExperiment::OnTcDrop, this).Bind(atRouter));
            nodes.Get(i)->GetObject<Ipv4L3Protocol>()->TraceConnectWithoutContext(
                "Drop",
                MakeCallback(&TcpExperiment::OnIpDrop, this));
        }
    };
    connect(m_routers, true);
    connect(m_hosts, false);
    for (uint32_t i = 0; i < m_hosts.GetN(); ++i)
    {
        m_hosts.Get(i)->GetObject<Ipv4L3Protocol>()->TraceConnectWithoutContext(
            "LocalDeliver",
            MakeCallback(&TcpExperiment::OnHostDeliver, this));
    }
}

uint32_t
TcpExperiment::RouterOfHost(uint32_t host) const
{
    return host / m_config.hostsPerRouter;
}

bool
TcpExperiment::IsAdjacentFlow(const Flow& flow) const
{
    return m_tables.Adjacent(RouterOfHost(flow.sourceHost), RouterOfHost(flow.destinationHost));
}

void
TcpExperiment::GenerateFlows()
{
    const auto hosts = m_hosts.GetN();
    const double meanBytes = m_flowSizes ? m_flowSizes->Mean()
                                         : m_config.smallFraction * m_config.smallBytes +
                                               (1 - m_config.smallFraction) * m_config.largeBytes;
    const double capacityBytesPerSecond = hosts * m_config.linkGbps * 1e9 / 8;
    const double arrivalsPerSecond = m_config.load * capacityBytesPerSecond / meanBytes;

    std::mt19937_64 rng(Key(m_config.seed, 0xf10, 0xb07));
    std::uniform_int_distribution<uint32_t> pickHost(0, hosts - 1);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::exponential_distribution<double> gap(arrivalsPerSecond);

    std::vector<uint32_t> nextPort(hosts, 10000); // by destination host
    double now = 0;
    for (uint32_t index = 0; m_config.flows == 0 || index < m_config.flows; ++index)
    {
        now += gap(rng);
        if (m_config.spanMs > 0 && now * 1e3 > m_config.spanMs)
        {
            break;
        }
        Flow flow;
        flow.sourceHost = pickHost(rng);
        do
        {
            flow.destinationHost = pickHost(rng);
        } while (flow.destinationHost == flow.sourceHost);
        const double sizeDraw = uniform(rng);
        if (m_flowSizes)
        {
            flow.bytes = m_flowSizes->Sample(sizeDraw);
        }
        else
        {
            flow.bytes =
                sizeDraw < m_config.smallFraction ? m_config.smallBytes : m_config.largeBytes;
        }
        flow.small = flow.bytes <= m_smallThreshold;
        flow.start = Seconds(now);

        // One listener per flow identifies the flow on receipt, and gives each
        // flow its own 5-tuple to hash.
        const auto port = nextPort[flow.destinationHost]++;
        if (port == 0xffff)
        {
            throw std::runtime_error("too many flows to one host");
        }
        flow.port = static_cast<uint16_t>(port);
        m_flows.push_back(flow);
    }
    if (m_flows.empty())
    {
        throw std::runtime_error("the workload has no flows; increase spanMs or load");
    }
    Simulator::Schedule(m_flows.front().start, &TcpExperiment::StartFlow, this, 0);
    Simulator::Schedule(m_flows.back().start, &TcpExperiment::RecordArrivalWindow, this);
}

void
TcpExperiment::StartFlow(uint32_t index)
{
    // Applications and sockets are created only when a flow starts, because
    // holding them for every future flow would dominate memory in large runs.
    const auto& flow = m_flows[index];
    const auto tos = flow.small ? SpraypointRouting::SMALL_FLOW_TOS : uint8_t{0};
    const auto destination =
        SpraypointRouting::HostAddress(RouterOfHost(flow.destinationHost),
                                       flow.destinationHost % m_config.hostsPerRouter);

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), flow.port));
    sinkHelper.SetAttribute("Tos", UintegerValue(tos));
    auto sink = sinkHelper.Install(m_hosts.Get(flow.destinationHost));
    sink.Get(0)->TraceConnectWithoutContext(
        "RxWithAddresses",
        MakeCallback(&TcpExperiment::OnSinkRx, this).Bind(index));

    BulkSendHelper sourceHelper("ns3::TcpSocketFactory", InetSocketAddress(destination, flow.port));
    sourceHelper.SetAttribute("MaxBytes", UintegerValue(flow.bytes));
    sourceHelper.SetAttribute("SendSize", UintegerValue(SEGMENT_BYTES));
    sourceHelper.SetAttribute("Tos", UintegerValue(tos));
    auto source = sourceHelper.Install(m_hosts.Get(flow.sourceHost));

    m_active[index] = ActiveFlow{DynamicCast<PacketSink>(sink.Get(0)),
                                 DynamicCast<BulkSendApplication>(source.Get(0))};

    if (flow.small)
    {
        // PacketSink marks a socket only once the handshake completes, so the
        // SYN-ACK would go out unmarked.  The sink starts in this time step, and
        // no SYN can arrive before the next nanosecond.
        Simulator::Schedule(NanoSeconds(1), &TcpExperiment::MarkListener, this, index);
    }
    if (index + 1 < m_flows.size())
    {
        Simulator::Schedule(m_flows[index + 1].start - Simulator::Now(),
                            &TcpExperiment::StartFlow,
                            this,
                            index + 1);
    }
}

void
TcpExperiment::MarkListener(uint32_t index) const
{
    m_active.at(index).sink->GetListeningSocket()->SetIpTos(SpraypointRouting::SMALL_FLOW_TOS);
}

void
TcpExperiment::ReleaseFlow(uint32_t index)
{
    // Closing the sockets lets TCP free them.  The node keeps the emptied
    // application objects, since ns-3 has no way to remove them.
    const auto active = m_active.find(index);
    const auto sink = active->second.sink;
    const auto source = active->second.source;
    for (const auto& socket : sink->GetAcceptedSockets())
    {
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
    const auto sender = source->GetSocket();
    sender->SetConnectCallback(MakeNullCallback<void, Ptr<Socket>>(),
                               MakeNullCallback<void, Ptr<Socket>>());
    sender->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
    sink->Dispose();
    source->Dispose();
    m_active.erase(active);
}

void
TcpExperiment::OnSinkRx(uint32_t index, Ptr<const Packet> packet, const Address&, const Address&)
{
    auto& flow = m_flows[index];
    flow.received += packet->GetSize();
    if (flow.received >= flow.bytes && flow.finish.IsZero())
    {
        flow.finish = Simulator::Now();
        if (++m_completed == m_flows.size())
        {
            Simulator::Stop();
        }
        else if (m_config.releaseDelayUs > 0)
        {
            Simulator::Schedule(MicroSeconds(m_config.releaseDelayUs),
                                &TcpExperiment::ReleaseFlow,
                                this,
                                index);
        }
    }
}

void
TcpExperiment::OnHostDeliver(const Ipv4Header& header, Ptr<const Packet>, uint32_t)
{
    // Each router on the path decrements the TTL, and a path of L fabric hops
    // crosses L + 1 routers.  This counts data and ACKs from the IP header, so
    // it reports the paths TCP really took.
    const auto routers = static_cast<uint32_t>(INITIAL_TTL - header.GetTtl());
    const auto hops = routers == 0 ? 0 : routers - 1;
    PathClass pathClass = LARGE;
    if ((header.GetTos() & 0xfc) == SpraypointRouting::SMALL_FLOW_TOS)
    {
        pathClass = m_tables.Adjacent(SpraypointRouting::RouterOf(header.GetSource()),
                                      SpraypointRouting::RouterOf(header.GetDestination()))
                        ? SMALL_ADJACENT
                        : SMALL_OTHER;
    }
    ++m_pathLengths[pathClass][std::min(hops, PATH_BUCKETS - 1)];
}

void
TcpExperiment::OnQueueLength(uint32_t index, uint32_t, uint32_t length)
{
    auto& port = m_ports[index];
    const auto now = Simulator::Now();
    port.packetSeconds += port.length * (now - port.lastChange).GetSeconds();
    port.length = length;
    port.lastChange = now;
    port.maxLength = std::max(port.maxLength, length);
    m_maxQueue[port.kind] = std::max(m_maxQueue[port.kind], length);
}

void
TcpExperiment::OnPortSent(uint32_t index, Ptr<const Packet> packet)
{
    auto copy = packet->Copy();
    PppHeader ppp;
    copy->RemoveHeader(ppp);
    Ipv4Header ip;
    copy->PeekHeader(ip);
    auto& port = m_ports[index];
    const bool small = (ip.GetTos() & 0xfc) == SpraypointRouting::SMALL_FLOW_TOS;
    (small ? port.sentSmall : port.sentLarge) += copy->GetSize();
}

void
TcpExperiment::RecordArrivalWindow()
{
    const auto now = Simulator::Now();
    for (auto& port : m_ports)
    {
        port.sentDuringArrivals = port.sentSmall + port.sentLarge;
        port.packetSecondsDuringArrivals =
            port.packetSeconds + port.length * (now - port.lastChange).GetSeconds();
    }
}

void
TcpExperiment::OnTcDrop(bool atRouter, Ptr<const Packet>)
{
    ++(atRouter ? m_routerDrops : m_hostDrops);
}

void
TcpExperiment::OnDeviceDrop(Ptr<const Packet>)
{
    ++m_deviceDrops;
}

void
TcpExperiment::OnIpDrop(const Ipv4Header&,
                        Ptr<const Packet>,
                        Ipv4L3Protocol::DropReason reason,
                        Ptr<Ipv4>,
                        uint32_t)
{
    switch (reason)
    {
    case Ipv4L3Protocol::DROP_TTL_EXPIRED:
        ++m_ttlDrops;
        break;
    case Ipv4L3Protocol::DROP_NO_ROUTE:
        ++m_noRouteDrops;
        break;
    default:
        ++m_otherIpDrops;
        break;
    }
}

void
TcpExperiment::PrintResults(std::ostream& out) const
{
    std::vector<double> small;
    std::vector<double> smallAdjacent;
    std::vector<double> large;
    uint32_t unfinishedSmall = 0;
    uint32_t unfinishedSmallAdjacent = 0;
    uint32_t unfinishedLarge = 0;
    Time lastStart;
    for (const auto& flow : m_flows)
    {
        lastStart = std::max(lastStart, flow.start);
        const bool adjacent = flow.small && IsAdjacentFlow(flow);
        if (flow.finish.IsZero())
        {
            ++(flow.small ? unfinishedSmall : unfinishedLarge);
            unfinishedSmallAdjacent += adjacent;
            continue;
        }
        const double fctUs = (flow.finish - flow.start).GetSeconds() * 1e6;
        if (flow.small)
        {
            small.push_back(fctUs);
            if (adjacent)
            {
                smallAdjacent.push_back(fctUs);
            }
        }
        else
        {
            large.push_back(fctUs);
        }
    }

    std::vector<uint64_t> allPathLengths(PATH_BUCKETS, 0);
    for (const auto& counts : m_pathLengths)
    {
        for (uint32_t hops = 0; hops < PATH_BUCKETS; ++hops)
        {
            allPathLengths[hops] += counts[hops];
        }
    }

    out << "RNG Spraypoint TCP experiment\n"
        << "parameters n=" << m_config.nodes << " d=" << m_config.degree
        << " p=" << m_config.waypoints << " h=" << m_config.nextHops
        << " waypoint-levels=" << m_tables.GetLevels()
        << " hosts-per-router=" << m_config.hostsPerRouter << " load=" << m_config.load
        << " small-bytes=" << m_config.smallBytes << " large-bytes=" << m_config.largeBytes
        << " small-fraction=" << m_config.smallFraction << " min-rto-us=" << m_config.minRtoUs
        << " seed=" << m_config.seed << "\n"
        << "settings pattern=" << m_config.pattern << " span-ms=" << m_config.spanMs
        << " flow-size-cdf=" << (m_config.flowSizeCdf.empty() ? "none" : m_config.flowSizeCdf)
        << " small-threshold=" << m_smallThreshold << " link-gbps=" << m_config.linkGbps
        << " link-delay-ns=" << m_config.linkDelayNs
        << " fabric-queue=" << m_config.fabricQueuePackets
        << " nic-queue=" << m_config.nicQueuePackets
        << " release-delay-us=" << m_config.releaseDelayUs << "\n"
        << "completed=" << m_completed << "/" << m_flows.size()
        << " last_flow_start_us=" << lastStart.GetSeconds() * 1e6
        << " sim_end_us=" << Simulator::Now().GetSeconds() * 1e6 << "\n"
        << "unfinished_flows small=" << unfinishedSmall
        << " small_adjacent=" << unfinishedSmallAdjacent << " large=" << unfinishedLarge << "\n";
    PrintPathLengths(out, "path_length_fraction", allPathLengths);
    PrintPathLengths(out, "path_length_fraction_small_adjacent", m_pathLengths[SMALL_ADJACENT]);
    PrintPathLengths(out, "path_length_fraction_small_other", m_pathLengths[SMALL_OTHER]);
    PrintPathLengths(out, "path_length_fraction_large", m_pathLengths[LARGE]);
    PrintFct(out, "small", small);
    PrintFct(out, "small_adjacent", smallAdjacent);
    PrintFct(out, "large", large);
    out << "max_queue_packets fabric=" << m_maxQueue[FABRIC] << " downlink=" << m_maxQueue[DOWNLINK]
        << " host_nic=" << m_maxQueue[HOST_NIC] << "\n"
        << "drops router_queue=" << m_routerDrops << " host_queue=" << m_hostDrops
        << " device=" << m_deviceDrops << " ttl=" << m_ttlDrops << " no_route=" << m_noRouteDrops
        << " other_ip=" << m_otherIpDrops << "\n";
}

void
TcpExperiment::WriteOutputs() const
{
    if (m_config.outputDir.empty())
    {
        return;
    }
    const std::filesystem::path directory(m_config.outputDir);
    std::filesystem::create_directories(directory);
    WriteFlows(directory / "flows.csv");
    WriteLinks(directory / "links.csv");
    std::ofstream summary(directory / "summary.txt");
    PrintResults(summary);
    if (!summary.flush())
    {
        throw std::runtime_error("could not write summary.txt in " + m_config.outputDir);
    }
}

void
TcpExperiment::WriteFlows(const std::string& path) const
{
    std::ofstream file(path);
    file << "flow,source_router,destination_router,adjacent,small,bytes,start_ns,finish_ns\n";
    for (size_t index = 0; index < m_flows.size(); ++index)
    {
        const auto& flow = m_flows[index];
        file << index << "," << RouterOfHost(flow.sourceHost) << ","
             << RouterOfHost(flow.destinationHost) << "," << IsAdjacentFlow(flow) << ","
             << flow.small << "," << flow.bytes << "," << flow.start.GetNanoSeconds() << ",";
        // An unfinished flow has an empty finish time.
        if (!flow.finish.IsZero())
        {
            file << flow.finish.GetNanoSeconds();
        }
        file << "\n";
    }
    if (!file.flush())
    {
        throw std::runtime_error("could not write " + path);
    }
}

void
TcpExperiment::WriteLinks(const std::string& path) const
{
    std::ofstream file(path);
    file << "from,to,sent_small_bytes,sent_large_bytes,sent_bytes_during_arrivals,"
            "delivered_small_bytes,delivered_large_bytes,max_queue_packets,"
            "mean_queue_packets_during_arrivals\n";
    const double arrivalSeconds = m_flows.back().start.GetSeconds();
    for (const auto& port : m_ports)
    {
        if (port.kind != FABRIC)
        {
            continue;
        }
        const auto& arrivals = m_routing[port.to]->GetArrivals(port.from);
        file << port.from << "," << port.to << "," << port.sentSmall << "," << port.sentLarge << ","
             << port.sentDuringArrivals << "," << arrivals.deliveredSmall << ","
             << arrivals.deliveredLarge << "," << port.maxLength << ","
             << port.packetSecondsDuringArrivals / arrivalSeconds << "\n";
    }
    if (!file.flush())
    {
        throw std::runtime_error("could not write " + path);
    }
}

} // namespace rng
