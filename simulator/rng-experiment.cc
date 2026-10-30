#include "rng-experiment.h"

#include "hash.h"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace ns3;

namespace rng
{

RngExperiment::RngExperiment(uint32_t nodes,
                             uint32_t degree,
                             uint32_t waypoints,
                             uint32_t nextHops,
                             uint32_t flows,
                             uint32_t samples,
                             uint64_t seed)
    : m_nodes(nodes),
      m_degree(degree),
      m_flows(flows),
      m_samples(samples),
      m_seed(seed),
      m_routing(nodes, degree, waypoints, nextHops, seed)
{
}

void
RngExperiment::Run()
{
    BuildNs3Fabric();
    ScheduleFlows();
    Simulator::Stop(MilliSeconds(100));
    Simulator::Run();
    Simulator::Destroy();
    PrintResults();
}

void
RngExperiment::BuildNs3Fabric()
{
    m_routers.Create(m_nodes);
    m_devices.resize(m_nodes);
    PointToPointHelper link;
    link.SetDeviceAttribute("DataRate", StringValue("100Gbps"));
    link.SetChannelAttribute("Delay", StringValue("500ns"));
    link.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("100p")));

    const auto& graph = m_routing.GetGraph();
    for (uint32_t a = 0; a < m_nodes; ++a)
    {
        for (auto b : graph[a])
        {
            if (a >= b)
            {
                continue;
            }
            auto pair = link.Install(m_routers.Get(a), m_routers.Get(b));
            m_devices[a][b] = pair.Get(0);
            m_devices[b][a] = pair.Get(1);
            pair.Get(0)->SetReceiveCallback(MakeCallback(&RngExperiment::Receive, this).Bind(a));
            pair.Get(1)->SetReceiveCallback(MakeCallback(&RngExperiment::Receive, this).Bind(b));
        }
    }
}

void
RngExperiment::ScheduleFlows()
{
    std::mt19937_64 rng(Key(m_seed, 0xfeed, 0xbeef));
    std::uniform_int_distribution<uint32_t> pick(0, m_nodes - 1);
    m_started.assign(m_flows, Seconds(0));
    for (uint32_t flow = 0; flow < m_flows; ++flow)
    {
        auto source = pick(rng);
        auto destination = pick(rng);
        while (destination == source)
        {
            destination = pick(rng);
        }
        auto start = MicroSeconds(1) + NanoSeconds(400 * flow);
        m_started[flow] = start;
        Simulator::Schedule(start, &RngExperiment::SendInitial, this, source, destination, flow);
    }
}

void
RngExperiment::SendInitial(uint32_t source, uint32_t destination, uint32_t flow)
{
    SprayHeader header;
    header.Set(source, destination, flow, 1);
    Send(source, m_routing.SprayHop(source, destination, flow), header);
}

void
RngExperiment::Send(uint32_t from, uint32_t to, const SprayHeader& header)
{
    auto packet = Create<Packet>(64);
    packet->AddHeader(header);
    auto found = m_devices[from].find(to);
    if (found == m_devices[from].end())
    {
        throw std::runtime_error("route chose a non-neighbor");
    }
    found->second->Send(packet, Address(), 0x0800);
}

bool
RngExperiment::Receive(uint32_t current,
                       Ptr<NetDevice>,
                       Ptr<const Packet> received,
                       uint16_t,
                       const Address&)
{
    auto packet = received->Copy();
    SprayHeader header;
    packet->RemoveHeader(header);
    if (current == header.GetDestination())
    {
        if (header.GetFlow() < m_started.size())
        {
            m_hops.push_back(header.GetHops());
            m_latencies.push_back(
                (Simulator::Now() - m_started[header.GetFlow()]).GetNanoSeconds());
        }
        return true;
    }
    if (header.GetHops() >= 16)
    {
        ++m_droppedForHopLimit;
        return false;
    }
    header.IncrementHops();
    Send(current, m_routing.PointHop(current, header.GetDestination(), header.GetFlow()), header);
    return true;
}

double
RngExperiment::Percentile(std::vector<uint64_t> values, double quantile)
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
RngExperiment::PrintResults()
{
    std::vector<uint32_t> histogram(17, 0);
    for (auto hops : m_hops)
    {
        if (hops < histogram.size())
        {
            ++histogram[hops];
        }
    }

    std::mt19937_64 rng(Key(m_seed, 0x1234, 0x5678));
    std::uniform_int_distribution<uint32_t> pick(0, m_nodes - 1);
    double adjacentSum = 0;
    double nonAdjacentSum = 0;
    uint32_t adjacentCount = 0;
    uint32_t nonAdjacentCount = 0;
    for (uint32_t i = 0; i < m_samples; ++i)
    {
        auto source = pick(rng);
        auto destination = pick(rng);
        while (source == destination)
        {
            destination = pick(rng);
        }
        const auto paths = m_routing.EdgeDisjointPaths(source, destination);
        if (m_routing.Adjacent(source, destination))
        {
            adjacentSum += paths;
            ++adjacentCount;
        }
        else
        {
            nonAdjacentSum += paths;
            ++nonAdjacentCount;
        }
    }

    const auto delivered = m_hops.size();
    std::cout << "RNG Spraypoint ns-3 reproduction\n"
              << "parameters n=" << m_nodes << " d=" << m_degree
              << " p=" << m_routing.GetWaypointCount() << " h=" << m_routing.GetNextHopCount()
              << " waypoint-levels=" << m_routing.GetLevels() << "\n"
              << "delivered=" << delivered << "/" << m_flows
              << " hop-limit-drops=" << m_droppedForHopLimit << "\n"
              << "path_length_fraction";
    for (uint32_t hops = 1; hops <= 5; ++hops)
    {
        std::cout << " L" << hops << "="
                  << (delivered ? static_cast<double>(histogram[hops]) / delivered : 0.0);
    }
    std::cout << "\npacket_latency_ns p50=" << Percentile(m_latencies, 0.50)
              << " p99=" << Percentile(m_latencies, 0.99) << "\n";
    std::cout << "edge_disjoint_paths sampled=" << m_samples
              << " adjacent_mean=" << (adjacentCount ? adjacentSum / adjacentCount : 0.0)
              << " nonadjacent_mean="
              << (nonAdjacentCount ? nonAdjacentSum / nonAdjacentCount : 0.0) << "\n";
    std::cout << "paper_model_nonadjacent="
              << m_degree * (1.0 - std::exp(-static_cast<double>(m_routing.GetNextHopCount())))
              << " (d*(1-exp(-h)))\n";
}

} // namespace rng
