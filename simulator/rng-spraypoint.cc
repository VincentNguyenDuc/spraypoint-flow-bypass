#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ns3;

namespace
{

uint64_t
Mix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t
Key(uint64_t seed, uint32_t a, uint32_t b, uint32_t c = 0)
{
    return Mix(seed ^ (static_cast<uint64_t>(a) << 32) ^
               (static_cast<uint64_t>(b) << 1) ^ static_cast<uint64_t>(c));
}

class SprayHeader : public Header
{
  public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("ns3::SprayHeader")
                                .SetParent<Header>()
                                .SetGroupName("Network")
                                .AddConstructor<SprayHeader>();
        return tid;
    }

    TypeId GetInstanceTypeId() const override
    {
        return GetTypeId();
    }

    uint32_t GetSerializedSize() const override
    {
        return 16;
    }

    void Serialize(Buffer::Iterator i) const override
    {
        i.WriteHtonU32(m_source);
        i.WriteHtonU32(m_destination);
        i.WriteHtonU32(m_flow);
        i.WriteHtonU32(m_hops);
    }

    uint32_t Deserialize(Buffer::Iterator i) override
    {
        m_source = i.ReadNtohU32();
        m_destination = i.ReadNtohU32();
        m_flow = i.ReadNtohU32();
        m_hops = i.ReadNtohU32();
        return GetSerializedSize();
    }

    void Print(std::ostream& os) const override
    {
        os << m_source << "->" << m_destination << " flow=" << m_flow << " hops=" << m_hops;
    }

    void Set(uint32_t source, uint32_t destination, uint32_t flow, uint32_t hops)
    {
        m_source = source;
        m_destination = destination;
        m_flow = flow;
        m_hops = hops;
    }

    void IncrementHops()
    {
        ++m_hops;
    }

    uint32_t GetDestination() const
    {
        return m_destination;
    }

    uint32_t GetFlow() const
    {
        return m_flow;
    }

    uint32_t GetHops() const
    {
        return m_hops;
    }

  private:
    uint32_t m_source{0};
    uint32_t m_destination{0};
    uint32_t m_flow{0};
    uint32_t m_hops{0};
};

NS_OBJECT_ENSURE_REGISTERED(SprayHeader);

class Dinic
{
  public:
    explicit Dinic(uint32_t n)
        : m_graph(n),
          m_level(n),
          m_next(n)
    {
    }

    void AddEdge(uint32_t from, uint32_t to)
    {
        Edge forward{to, static_cast<uint32_t>(m_graph[to].size()), 1};
        Edge reverse{from, static_cast<uint32_t>(m_graph[from].size()), 0};
        m_graph[from].push_back(forward);
        m_graph[to].push_back(reverse);
    }

    uint32_t MaxFlow(uint32_t source, uint32_t sink)
    {
        uint32_t flow = 0;
        while (BuildLevels(source, sink))
        {
            std::fill(m_next.begin(), m_next.end(), 0);
            while (uint32_t pushed = Send(source, sink, std::numeric_limits<uint32_t>::max()))
            {
                flow += pushed;
            }
        }
        return flow;
    }

  private:
    struct Edge
    {
        uint32_t to;
        uint32_t reverse;
        uint32_t capacity;
    };

    bool BuildLevels(uint32_t source, uint32_t sink)
    {
        std::fill(m_level.begin(), m_level.end(), -1);
        std::queue<uint32_t> todo;
        m_level[source] = 0;
        todo.push(source);
        while (!todo.empty())
        {
            auto u = todo.front();
            todo.pop();
            for (const auto& edge : m_graph[u])
            {
                if (edge.capacity && m_level[edge.to] < 0)
                {
                    m_level[edge.to] = m_level[u] + 1;
                    todo.push(edge.to);
                }
            }
        }
        return m_level[sink] >= 0;
    }

    uint32_t Send(uint32_t u, uint32_t sink, uint32_t available)
    {
        if (u == sink)
        {
            return available;
        }
        for (auto& i = m_next[u]; i < m_graph[u].size(); ++i)
        {
            auto& edge = m_graph[u][i];
            if (!edge.capacity || m_level[edge.to] != m_level[u] + 1)
            {
                continue;
            }
            auto pushed = Send(edge.to, sink, std::min(available, edge.capacity));
            if (pushed)
            {
                edge.capacity -= pushed;
                m_graph[edge.to][edge.reverse].capacity += pushed;
                return pushed;
            }
        }
        return 0;
    }

    std::vector<std::vector<Edge>> m_graph;
    std::vector<int> m_level;
    std::vector<uint32_t> m_next;
};

class Spraypoint
{
  public:
    Spraypoint(uint32_t nodes, uint32_t degree, uint32_t waypoints, uint32_t nextHops, uint64_t seed)
        : m_nodes(nodes),
          m_degree(degree),
          m_waypoints(waypoints),
          m_nextHops(nextHops),
          m_seed(seed),
          m_graph(nodes),
          m_pointing(nodes, std::vector<std::vector<uint32_t>>(nodes))
    {
        if (nodes <= degree || (nodes * degree) % 2)
        {
            throw std::runtime_error("need n > d and an even number of stubs");
        }
        m_levels = std::max(1, static_cast<int>(std::ceil(
                                  std::log(static_cast<double>(nodes) / (2.0 * degree * degree)) /
                                  std::log(static_cast<double>(waypoints)))));
        BuildRandomRegularGraph();
        BuildPointingTables();
    }

    const std::vector<std::vector<uint32_t>>& GetGraph() const
    {
        return m_graph;
    }

    int GetLevels() const
    {
        return m_levels;
    }

    uint32_t GetWaypointCount() const
    {
        return m_waypoints;
    }

    uint32_t GetNextHopCount() const
    {
        return m_nextHops;
    }

    uint32_t SprayHop(uint32_t source, uint32_t destination, uint32_t flow) const
    {
        const auto& neighbors = m_graph[source];
        return neighbors[Key(m_seed, source, destination, flow) % neighbors.size()];
    }

    uint32_t PointHop(uint32_t current, uint32_t destination, uint32_t flow) const
    {
        const auto& choices = m_pointing[destination][current];
        if (choices.empty())
        {
            throw std::runtime_error("pointing table has an empty next-hop set");
        }
        return choices[Key(m_seed ^ 0x4d595df4d0f33173ULL, current, destination, flow) % choices.size()];
    }

    bool Adjacent(uint32_t a, uint32_t b) const
    {
        return std::binary_search(m_graph[a].begin(), m_graph[a].end(), b);
    }

    uint32_t EdgeDisjointPaths(uint32_t source, uint32_t destination) const
    {
        Dinic flow(m_nodes);
        for (uint32_t u = 0; u < m_nodes; ++u)
        {
            if (u == destination)
            {
                continue;
            }
            const auto& edges = (u == source) ? m_graph[u] : m_pointing[destination][u];
            for (auto v : edges)
            {
                flow.AddEdge(u, v);
            }
        }
        return flow.MaxFlow(source, destination);
    }

  private:
    void BuildRandomRegularGraph()
    {
        // Construct d disjoint random perfect matchings.  Rejection sampling a
        // whole configuration-model graph is impractical here: a 1000-node,
        // degree-64 graph almost certainly has at least one parallel edge.
        // Retrying one matching at a time preserves exact degree and gives the
        // quasi-random graph needed by the paper's model.
        std::vector<std::unordered_set<uint32_t>> sets(m_nodes);
        std::mt19937_64 rng(Key(m_seed, 0, 0));
        for (uint32_t round = 0; round < m_degree; ++round)
        {
            bool completed = false;
            for (uint32_t attempt = 0; attempt < 1000 && !completed; ++attempt)
            {
                std::vector<uint32_t> unmatched(m_nodes);
                std::iota(unmatched.begin(), unmatched.end(), 0);
                std::shuffle(unmatched.begin(), unmatched.end(), rng);
                std::vector<std::pair<uint32_t, uint32_t>> matching;
                bool failed = false;
                while (!unmatched.empty())
                {
                    const auto a = unmatched.back();
                    const auto candidates = unmatched.size() - 1;
                    const auto offset = rng() % candidates;
                    uint32_t chosen = candidates;
                    for (uint32_t step = 0; step < candidates; ++step)
                    {
                        const auto index = (offset + step) % candidates;
                        if (!sets[a].contains(unmatched[index]))
                        {
                            chosen = index;
                            break;
                        }
                    }
                    if (chosen == candidates)
                    {
                        failed = true;
                        break;
                    }
                    const auto b = unmatched[chosen];
                    std::swap(unmatched[chosen], unmatched.back());
                    unmatched.pop_back(); // b
                    unmatched.pop_back(); // a
                    matching.emplace_back(a, b);
                }
                if (!failed)
                {
                    for (const auto& [a, b] : matching)
                    {
                        sets[a].insert(b);
                        sets[b].insert(a);
                    }
                    completed = true;
                }
            }
            if (!completed)
            {
                throw std::runtime_error("failed to generate a random simple matching");
            }
        }
        for (uint32_t node = 0; node < m_nodes; ++node)
        {
            m_graph[node].assign(sets[node].begin(), sets[node].end());
            std::sort(m_graph[node].begin(), m_graph[node].end());
        }
    }

    std::vector<uint32_t> Choose(const std::vector<uint32_t>& candidates,
                                 uint32_t count,
                                 uint32_t destination,
                                 uint32_t node,
                                 uint32_t phase) const
    {
        auto choices = candidates;
        std::mt19937_64 rng(Key(m_seed, destination, node, phase));
        std::shuffle(choices.begin(), choices.end(), rng);
        choices.resize(std::min<size_t>(count, choices.size()));
        return choices;
    }

    void BuildPointingTables()
    {
        for (uint32_t destination = 0; destination < m_nodes; ++destination)
        {
            std::vector<int> level(m_nodes, -1);
            level[destination] = -2;
            std::vector<uint32_t> previous = m_graph[destination];
            for (auto node : previous)
            {
                level[node] = 0;
            }

            for (int layer = 1; layer <= m_levels; ++layer)
            {
                std::vector<uint32_t> current;
                for (auto parent : previous)
                {
                    std::vector<uint32_t> candidates;
                    for (auto neighbor : m_graph[parent])
                    {
                        if (neighbor != destination && level[neighbor] < 0)
                        {
                            candidates.push_back(neighbor);
                        }
                    }
                    for (auto chosen : Choose(candidates, m_waypoints, destination, parent, layer))
                    {
                        if (level[chosen] < 0)
                        {
                            level[chosen] = layer;
                            current.push_back(chosen);
                        }
                    }
                }
                previous = std::move(current);
            }

            std::vector<uint32_t> inner;
            for (auto waypoint : previous)
            {
                for (auto neighbor : m_graph[waypoint])
                {
                    if (level[neighbor] < 0)
                    {
                        level[neighbor] = m_levels + 1;
                        inner.push_back(neighbor);
                    }
                }
            }

            std::vector<int> distance(m_nodes, -1);
            std::queue<uint32_t> bfs;
            for (auto node : inner)
            {
                distance[node] = 0;
                bfs.push(node);
            }
            while (!bfs.empty())
            {
                auto node = bfs.front();
                bfs.pop();
                for (auto neighbor : m_graph[node])
                {
                    if (distance[neighbor] < 0)
                    {
                        distance[neighbor] = distance[node] + 1;
                        bfs.push(neighbor);
                    }
                }
            }

            for (uint32_t node = 0; node < m_nodes; ++node)
            {
                if (node == destination)
                {
                    continue;
                }
                std::vector<uint32_t> candidates;
                if (level[node] == 0)
                {
                    candidates.push_back(destination);
                }
                else if (level[node] >= 1 && level[node] <= m_levels)
                {
                    for (auto neighbor : m_graph[node])
                    {
                        if (level[neighbor] == level[node] - 1)
                        {
                            candidates.push_back(neighbor);
                        }
                    }
                }
                else if (level[node] == m_levels + 1)
                {
                    for (auto neighbor : m_graph[node])
                    {
                        if (level[neighbor] == m_levels)
                        {
                            candidates.push_back(neighbor);
                        }
                    }
                }
                else
                {
                    for (auto neighbor : m_graph[node])
                    {
                        if (distance[neighbor] >= 0 && distance[neighbor] < distance[node])
                        {
                            candidates.push_back(neighbor);
                        }
                    }
                }
                if (candidates.empty())
                {
                    throw std::runtime_error("unable to construct a pointing next-hop set");
                }
                m_pointing[destination][node] =
                    Choose(candidates, m_nextHops, destination, node, 100 + level[node]);
            }
        }
    }

    uint32_t m_nodes;
    uint32_t m_degree;
    uint32_t m_waypoints;
    uint32_t m_nextHops;
    uint64_t m_seed;
    int m_levels;
    std::vector<std::vector<uint32_t>> m_graph;
    // Indexed as [destination][current router][eligible next hops].
    std::vector<std::vector<std::vector<uint32_t>>> m_pointing;
};

class RngExperiment
{
  public:
    RngExperiment(uint32_t nodes,
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

    void Run()
    {
        BuildNs3Fabric();
        ScheduleFlows();
        Simulator::Stop(MilliSeconds(100));
        Simulator::Run();
        Simulator::Destroy();
        PrintResults();
    }

  private:
    void BuildNs3Fabric()
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

    void ScheduleFlows()
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

    void SendInitial(uint32_t source, uint32_t destination, uint32_t flow)
    {
        SprayHeader header;
        header.Set(source, destination, flow, 1);
        Send(source, m_routing.SprayHop(source, destination, flow), header);
    }

    void Send(uint32_t from, uint32_t to, const SprayHeader& header)
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

    bool Receive(uint32_t current,
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
                m_latencies.push_back((Simulator::Now() - m_started[header.GetFlow()]).GetNanoSeconds());
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

    static double Percentile(std::vector<uint64_t> values, double quantile)
    {
        if (values.empty())
        {
            return 0;
        }
        std::sort(values.begin(), values.end());
        const auto index = static_cast<size_t>(std::ceil(quantile * values.size())) - 1;
        return values[std::min(index, values.size() - 1)];
    }

    void PrintResults()
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
                  << " p=" << m_routing.GetWaypointCount()
                  << " h=" << m_routing.GetNextHopCount()
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
                  << " nonadjacent_mean=" << (nonAdjacentCount ? nonAdjacentSum / nonAdjacentCount : 0.0)
                  << "\n";
        std::cout << "paper_model_nonadjacent="
                  << m_degree * (1.0 - std::exp(-static_cast<double>(m_routing.GetNextHopCount())))
                  << " (d*(1-exp(-h)))\n";
    }

    uint32_t m_nodes;
    uint32_t m_degree;
    uint32_t m_flows;
    uint32_t m_samples;
    uint64_t m_seed;
    Spraypoint m_routing;
    NodeContainer m_routers;
    std::vector<std::unordered_map<uint32_t, Ptr<NetDevice>>> m_devices;
    std::vector<Time> m_started;
    std::vector<uint32_t> m_hops;
    std::vector<uint64_t> m_latencies;
    uint32_t m_droppedForHopLimit{0};
};

} // namespace

int
main(int argc, char* argv[])
{
    uint32_t nodes = 1000;
    uint32_t degree = 64;
    uint32_t waypoints = 4;
    uint32_t nextHops = 2;
    uint32_t flows = 20000;
    uint32_t samples = 100;
    uint64_t seed = 561;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nodes", "Number of ToR routers (paper default: 1000)", nodes);
    cmd.AddValue("degree", "Fabric degree per ToR (paper default: 64)", degree);
    cmd.AddValue("waypoints", "p: selected waypoint neighbors (paper default: 4)", waypoints);
    cmd.AddValue("nextHops", "h: pointing ECMP next hops (paper default: 2)", nextHops);
    cmd.AddValue("flows", "Single-packet flows used to sample ns-3 path lengths", flows);
    cmd.AddValue("samples", "Source/destination pairs used for max-flow sampling", samples);
    cmd.AddValue("seed", "Random topology and ECMP seed", seed);
    cmd.Parse(argc, argv);

    try
    {
        RngExperiment experiment(nodes, degree, waypoints, nextHops, flows, samples, seed);
        experiment.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
