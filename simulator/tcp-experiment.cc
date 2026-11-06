#include "tcp-experiment.h"

#include "hash.h"
#include "spraypoint-routing.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-layer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace ns3;

namespace rng
{

namespace
{

const double LINK_BITS_PER_SECOND = 100e9;
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
PrintFct(const char* name, const std::vector<double>& fctUs)
{
    std::cout << "fct_us class=" << name << " count=" << fctUs.size()
              << " p50=" << Percentile(fctUs, 0.50) << " p99=" << Percentile(fctUs, 0.99)
              << " p999=" << Percentile(fctUs, 0.999) << " max=" << Percentile(fctUs, 1.0) << "\n";
}

} // namespace

TcpExperiment::TcpExperiment(const TcpExperimentConfig& config)
    : m_config(config),
      m_tables(config.nodes, config.degree, config.waypoints, config.nextHops, config.seed),
      m_pathLengths(PATH_BUCKETS, 0)
{
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
    ScheduleFlows();
    Simulator::Stop(MilliSeconds(m_config.maxSimMs));
    Simulator::Run();
    PrintResults();
    Simulator::Destroy();
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
    link.SetDeviceAttribute("DataRate", DataRateValue(DataRate(LINK_BITS_PER_SECOND)));
    link.SetChannelAttribute("Delay", StringValue("500ns"));
    link.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("100p")));

    std::vector<Ptr<SpraypointRouting>> routing;
    routing.reserve(m_config.nodes);
    for (uint32_t router = 0; router < m_config.nodes; ++router)
    {
        routing.push_back(CreateObject<SpraypointRouting>(m_tables, router));
        m_routers.Get(router)->GetObject<Ipv4>()->SetRoutingProtocol(routing.back());
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
            routing[a]->AddFabricPort(b, pair.Get(0));
            routing[b]->AddFabricPort(a, pair.Get(1));
            ConnectPort(pair.Get(0), FABRIC);
            ConnectPort(pair.Get(1), FABRIC);
        }
    }

    for (uint32_t router = 0; router < m_config.nodes; ++router)
    {
        for (uint32_t index = 0; index < m_config.hostsPerRouter; ++index)
        {
            auto host = m_hosts.Get(router * m_config.hostsPerRouter + index);
            auto pair = link.Install(host, m_routers.Get(router));
            const auto address = SpraypointRouting::HostAddress(router, index);

            // A host's only way out is its ToR, and a NIC should not drop what
            // its own TCP sends in a burst; Linux's default txqueuelen is 1000.
            DynamicCast<PointToPointNetDevice>(pair.Get(0))
                ->GetQueue()
                ->SetMaxSize(QueueSize("1000p"));

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
            routing[router]->AddHostPort(address, pair.Get(1));
            ConnectPort(pair.Get(0), HOST_NIC);
            ConnectPort(pair.Get(1), DOWNLINK);
        }
    }
}

void
TcpExperiment::ConnectPort(Ptr<NetDevice> device, PortKind kind)
{
    auto p2p = DynamicCast<PointToPointNetDevice>(device);
    p2p->GetQueue()->TraceConnectWithoutContext(
        "PacketsInQueue",
        MakeCallback(&TcpExperiment::OnQueueLength, this).Bind(kind));
    p2p->TraceConnectWithoutContext("MacTxDrop", MakeCallback(&TcpExperiment::OnDeviceDrop, this));
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

void
TcpExperiment::ScheduleFlows()
{
    const auto hosts = m_hosts.GetN();
    const double meanBytes = m_config.smallFraction * m_config.smallBytes +
                             (1 - m_config.smallFraction) * m_config.largeBytes;
    const double capacityBytesPerSecond = hosts * LINK_BITS_PER_SECOND / 8;
    const double arrivalsPerSecond = m_config.load * capacityBytesPerSecond / meanBytes;

    std::mt19937_64 rng(Key(m_config.seed, 0xf10, 0xb07));
    std::uniform_int_distribution<uint32_t> pickHost(0, hosts - 1);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::exponential_distribution<double> gap(arrivalsPerSecond);

    m_nextPort.assign(hosts, 10000);
    m_flows.reserve(m_config.flows);
    double now = 0;
    for (uint32_t index = 0; index < m_config.flows; ++index)
    {
        now += gap(rng);
        Flow flow;
        flow.sourceHost = pickHost(rng);
        do
        {
            flow.destinationHost = pickHost(rng);
        } while (flow.destinationHost == flow.sourceHost);
        flow.bytes =
            uniform(rng) < m_config.smallFraction ? m_config.smallBytes : m_config.largeBytes;
        flow.start = Seconds(now);
        m_flows.push_back(flow);

        // One listener per flow identifies the flow on receipt, and gives each
        // flow its own 5-tuple to hash.
        const auto port = m_nextPort[flow.destinationHost]++;
        if (port == 0xffff)
        {
            throw std::runtime_error("too many flows to one host");
        }
        const auto destination =
            SpraypointRouting::HostAddress(RouterOfHost(flow.destinationHost),
                                           flow.destinationHost % m_config.hostsPerRouter);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        auto sink = sinkHelper.Install(m_hosts.Get(flow.destinationHost));
        sink.Start(flow.start);
        sink.Get(0)->TraceConnectWithoutContext(
            "RxWithAddresses",
            MakeCallback(&TcpExperiment::OnSinkRx, this).Bind(index));

        BulkSendHelper sourceHelper("ns3::TcpSocketFactory", InetSocketAddress(destination, port));
        sourceHelper.SetAttribute("MaxBytes", UintegerValue(flow.bytes));
        sourceHelper.SetAttribute("SendSize", UintegerValue(SEGMENT_BYTES));
        sourceHelper.Install(m_hosts.Get(flow.sourceHost)).Start(flow.start);
    }
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
    ++m_pathLengths[std::min(hops, PATH_BUCKETS - 1)];
}

void
TcpExperiment::OnQueueLength(PortKind kind, uint32_t, uint32_t length)
{
    m_maxQueue[kind] = std::max(m_maxQueue[kind], length);
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
TcpExperiment::PrintResults() const
{
    std::vector<double> small;
    std::vector<double> smallAdjacent;
    std::vector<double> large;
    Time lastStart;
    for (const auto& flow : m_flows)
    {
        lastStart = std::max(lastStart, flow.start);
        if (flow.finish.IsZero())
        {
            continue;
        }
        const double fctUs = (flow.finish - flow.start).GetSeconds() * 1e6;
        if (flow.bytes == m_config.smallBytes)
        {
            small.push_back(fctUs);
            if (m_tables.Adjacent(RouterOfHost(flow.sourceHost),
                                  RouterOfHost(flow.destinationHost)))
            {
                smallAdjacent.push_back(fctUs);
            }
        }
        else
        {
            large.push_back(fctUs);
        }
    }

    uint64_t delivered = 0;
    for (auto count : m_pathLengths)
    {
        delivered += count;
    }

    std::cout << "RNG Spraypoint TCP experiment\n"
              << "parameters n=" << m_config.nodes << " d=" << m_config.degree
              << " p=" << m_config.waypoints << " h=" << m_config.nextHops
              << " waypoint-levels=" << m_tables.GetLevels()
              << " hosts-per-router=" << m_config.hostsPerRouter << " load=" << m_config.load
              << " small-bytes=" << m_config.smallBytes << " large-bytes=" << m_config.largeBytes
              << " small-fraction=" << m_config.smallFraction << " min-rto-us=" << m_config.minRtoUs
              << " seed=" << m_config.seed << "\n"
              << "completed=" << m_completed << "/" << m_flows.size()
              << " last_flow_start_us=" << lastStart.GetSeconds() * 1e6
              << " sim_end_us=" << Simulator::Now().GetSeconds() * 1e6 << "\n"
              << "path_length_fraction";
    for (uint32_t hops = 0; hops < PATH_BUCKETS; ++hops)
    {
        std::cout << " L" << hops << "="
                  << (delivered ? static_cast<double>(m_pathLengths[hops]) / delivered : 0.0);
    }
    std::cout << "\n";
    PrintFct("small", small);
    PrintFct("small_adjacent", smallAdjacent);
    PrintFct("large", large);
    std::cout << "max_queue_packets fabric=" << m_maxQueue[FABRIC]
              << " downlink=" << m_maxQueue[DOWNLINK] << " host_nic=" << m_maxQueue[HOST_NIC]
              << "\n"
              << "drops router_queue=" << m_routerDrops << " host_queue=" << m_hostDrops
              << " device=" << m_deviceDrops << " ttl=" << m_ttlDrops
              << " no_route=" << m_noRouteDrops << " other_ip=" << m_otherIpDrops << "\n";
}

} // namespace rng
