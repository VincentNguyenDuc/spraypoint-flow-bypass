#include "oversubscription.h"

#include "spraypoint/hash.h"
#include "spraypoint/max-flow.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>

namespace rng
{

// Shortest Spraypoint path under the current link lengths, memoized per query.
class OversubscriptionSolver::PathFinder
{
  public:
    PathFinder(const OversubscriptionSolver& solver, const std::vector<double>& lengths)
        : m_solver(solver),
          m_lengths(lengths),
          m_distance(solver.m_nodes),
          m_via(solver.m_nodes),
          m_stamp(solver.m_nodes, 0)
    {
    }

    // Returns the path length; fills path with link ids when given.  A Spraypoint
    // path sprays to any neighbor and then follows pointing; otherwise the path is
    // a shortest one.
    double Shortest(uint32_t source, uint32_t destination, bool spray, std::vector<uint32_t>* path)
    {
        ++m_epoch;
        m_dag = spray ? &m_solver.m_dags[destination] : &m_solver.m_shortestDags[destination];
        m_destination = destination;
        if (!spray)
        {
            const double length = Distance(source);
            if (path)
            {
                path->clear();
                for (auto node = source; node != destination; node = m_dag->arcTarget[m_via[node]])
                {
                    path->push_back(m_dag->arcLink[m_via[node]]);
                }
            }
            return length;
        }
        const auto& neighbors = m_solver.m_routing.GetGraph()[source];
        double best = std::numeric_limits<double>::infinity();
        uint32_t bestSpray = 0;
        for (uint32_t k = 0; k < neighbors.size(); ++k)
        {
            const double candidate =
                m_lengths[source * m_solver.m_degree + k] + Distance(neighbors[k]);
            if (candidate < best)
            {
                best = candidate;
                bestSpray = k;
            }
        }
        if (path)
        {
            path->clear();
            path->push_back(source * m_solver.m_degree + bestSpray);
            for (auto node = neighbors[bestSpray]; node != destination;
                 node = m_dag->arcTarget[m_via[node]])
            {
                path->push_back(m_dag->arcLink[m_via[node]]);
            }
        }
        return best;
    }

  private:
    double Distance(uint32_t node)
    {
        if (node == m_destination)
        {
            return 0;
        }
        if (m_stamp[node] == m_epoch)
        {
            return m_distance[node];
        }
        double best = std::numeric_limits<double>::infinity();
        for (auto arc = m_dag->arcStart[node]; arc < m_dag->arcStart[node + 1]; ++arc)
        {
            const double candidate =
                m_lengths[m_dag->arcLink[arc]] + Distance(m_dag->arcTarget[arc]);
            if (candidate < best)
            {
                best = candidate;
                m_via[node] = arc;
            }
        }
        m_stamp[node] = m_epoch;
        m_distance[node] = best;
        return best;
    }

    const OversubscriptionSolver& m_solver;
    const std::vector<double>& m_lengths;
    const PointingDag* m_dag{nullptr};
    uint32_t m_destination{0};
    std::vector<double> m_distance;
    std::vector<uint32_t> m_via;
    std::vector<uint64_t> m_stamp;
    uint64_t m_epoch{0};
};

OversubscriptionSolver::OversubscriptionSolver(const Spraypoint& routing)
    : m_routing(routing),
      m_nodes(static_cast<uint32_t>(routing.GetGraph().size())),
      m_degree(static_cast<uint32_t>(routing.GetGraph()[0].size())),
      m_dags(m_nodes)
{
    std::vector<uint32_t> hops(m_nodes);
    for (uint32_t t = 0; t < m_nodes; ++t)
    {
        auto& dag = m_dags[t];
        dag.arcStart.assign(m_nodes + 1, 0);
        for (uint32_t u = 0; u < m_nodes; ++u)
        {
            dag.arcStart[u] = static_cast<uint32_t>(dag.arcLink.size());
            if (u == t)
            {
                continue;
            }
            for (auto v : routing.GetPointing(t, u))
            {
                dag.arcLink.push_back(Link(u, v));
                dag.arcTarget.push_back(v);
            }
        }
        dag.arcStart[m_nodes] = static_cast<uint32_t>(dag.arcLink.size());

        // Pointing hops to t, computed nearest-first: a node's hop count is
        // known once all of its next hops' are (pointing is loop-free; see
        // CheckStructure).
        std::fill(hops.begin(), hops.end(), std::numeric_limits<uint32_t>::max());
        hops[t] = 0;
        bool changed = true;
        for (uint32_t round = 0; changed; ++round)
        {
            if (round > m_nodes)
            {
                throw std::runtime_error("pointing graph has a loop");
            }
            changed = false;
            for (uint32_t u = 0; u < m_nodes; ++u)
            {
                if (hops[u] != std::numeric_limits<uint32_t>::max())
                {
                    continue;
                }
                uint32_t longest = 0;
                bool ready = true;
                for (auto arc = dag.arcStart[u]; arc < dag.arcStart[u + 1] && ready; ++arc)
                {
                    ready = hops[dag.arcTarget[arc]] != std::numeric_limits<uint32_t>::max();
                    longest = std::max(longest, hops[dag.arcTarget[arc]]);
                }
                if (ready)
                {
                    hops[u] = longest + 1;
                    changed = true;
                }
            }
        }
        for (uint32_t u = 0; u < m_nodes; ++u)
        {
            if (u != t)
            {
                dag.farthestFirst.push_back(u);
            }
        }
        std::sort(dag.farthestFirst.begin(), dag.farthestFirst.end(), [&](uint32_t a, uint32_t b) {
            return hops[a] > hops[b];
        });
    }

    // Shortest paths toward each destination: every neighbor one hop closer, the
    // choices that ShortestPaths::NextHop hashes over in tcp-bypass.
    m_shortestDags.resize(m_nodes);
    m_distance.assign(m_nodes, std::vector<uint8_t>(m_nodes, 0xff));
    const auto& graph = routing.GetGraph();
    for (uint32_t t = 0; t < m_nodes; ++t)
    {
        auto& distance = m_distance[t];
        distance[t] = 0;
        std::queue<uint32_t> bfs;
        bfs.push(t);
        while (!bfs.empty())
        {
            const auto node = bfs.front();
            bfs.pop();
            for (auto neighbor : graph[node])
            {
                if (distance[neighbor] == 0xff)
                {
                    distance[neighbor] = distance[node] + 1;
                    bfs.push(neighbor);
                }
            }
        }
        auto& dag = m_shortestDags[t];
        dag.arcStart.assign(m_nodes + 1, 0);
        for (uint32_t u = 0; u < m_nodes; ++u)
        {
            if (distance[u] == 0xff)
            {
                throw std::runtime_error("the router graph is disconnected");
            }
            dag.arcStart[u] = static_cast<uint32_t>(dag.arcLink.size());
            if (u == t)
            {
                continue;
            }
            dag.farthestFirst.push_back(u);
            for (auto v : graph[u])
            {
                if (distance[v] + 1 == distance[u])
                {
                    dag.arcLink.push_back(Link(u, v));
                    dag.arcTarget.push_back(v);
                }
            }
        }
        dag.arcStart[m_nodes] = static_cast<uint32_t>(dag.arcLink.size());
        std::sort(dag.farthestFirst.begin(), dag.farthestFirst.end(), [&](uint32_t a, uint32_t b) {
            return distance[a] > distance[b];
        });
    }
}

uint32_t
OversubscriptionSolver::Link(uint32_t from, uint32_t to) const
{
    const auto& neighbors = m_routing.GetGraph()[from];
    auto found = std::lower_bound(neighbors.begin(), neighbors.end(), to);
    if (found == neighbors.end() || *found != to)
    {
        throw std::runtime_error("pointing next hop is not a neighbor");
    }
    return from * m_degree + static_cast<uint32_t>(found - neighbors.begin());
}

uint32_t
OversubscriptionSolver::Distance(uint32_t source, uint32_t destination) const
{
    return m_distance[destination][source];
}

uint32_t
OversubscriptionSolver::ShortestEdgeDisjointPaths(uint32_t source, uint32_t destination) const
{
    const auto distance = Distance(source, destination);
    const auto& dag = m_shortestDags[destination];
    if (distance <= 2)
    {
        // Each arc out of the source starts its own path: at distance 2 every
        // next hop is a neighbor of the destination with one arc to it.
        return distance == 0 ? 0 : dag.arcStart[source + 1] - dag.arcStart[source];
    }
    Dinic flow(m_nodes);
    for (uint32_t u = 0; u < m_nodes; ++u)
    {
        for (auto arc = dag.arcStart[u]; arc < dag.arcStart[u + 1]; ++arc)
        {
            flow.AddEdge(u, dag.arcTarget[arc]);
        }
    }
    return flow.MaxFlow(source, destination);
}

double
OversubscriptionSolver::SmallShare(uint32_t source,
                                   uint32_t destination,
                                   const Bypass& bypass) const
{
    switch (bypass.mode)
    {
    case BypassMode::SHORTEST:
        return bypass.share;
    case BypassMode::ADJACENT:
        return Distance(source, destination) == 1 ? bypass.share : 0;
    default:
        return 0;
    }
}

template <typename Visit>
void
OversubscriptionSolver::Spread(const PointingDag& dag, std::vector<double>& mass, Visit visit) const
{
    for (auto u : dag.farthestFirst)
    {
        if (mass[u] == 0)
        {
            continue;
        }
        const auto arcs = dag.arcStart[u + 1] - dag.arcStart[u];
        for (auto arc = dag.arcStart[u]; arc < dag.arcStart[u + 1]; ++arc)
        {
            visit(dag.arcLink[arc], mass[u] / arcs);
            mass[dag.arcTarget[arc]] += mass[u] / arcs;
        }
    }
}

double
OversubscriptionSolver::Ecmp(const std::vector<uint32_t>& matching,
                             const Bypass& bypass,
                             std::vector<PairLoad>* pairs) const
{
    // Each ToR sends its full rate, d unit-capacity links, to matching[s].  Sprayed
    // bytes split evenly over the source's neighbors and then over pointing next
    // hops; bypassed bytes split evenly over the next hops one hop closer.
    const double demand = m_degree;
    std::vector<double> load(static_cast<size_t>(m_nodes) * m_degree, 0.0);
    std::vector<double> mass(m_nodes);

    // Calls visit(link, amount, small) for every link that s's bytes cross.
    const auto route = [&](uint32_t s, auto visit) {
        const auto t = matching[s];
        const double small = SmallShare(s, t, bypass) * demand;
        const double large = demand - small;
        if (large > 0)
        {
            std::fill(mass.begin(), mass.end(), 0.0);
            const auto& neighbors = m_routing.GetGraph()[s];
            for (uint32_t k = 0; k < neighbors.size(); ++k)
            {
                visit(s * m_degree + k, large / neighbors.size(), false);
                mass[neighbors[k]] += large / neighbors.size();
            }
            Spread(m_dags[t], mass, [&](uint32_t link, double amount) {
                visit(link, amount, false);
            });
        }
        if (small > 0)
        {
            std::fill(mass.begin(), mass.end(), 0.0);
            mass[s] = small;
            Spread(m_shortestDags[t], mass, [&](uint32_t link, double amount) {
                visit(link, amount, true);
            });
        }
    };

    for (uint32_t s = 0; s < m_nodes; ++s)
    {
        route(s, [&](uint32_t link, double amount, bool) { load[link] += amount; });
    }
    if (pairs)
    {
        pairs->assign(m_nodes, PairLoad{});
        for (uint32_t s = 0; s < m_nodes; ++s)
        {
            auto& pair = (*pairs)[s];
            pair.source = s;
            pair.destination = matching[s];
            route(s, [&](uint32_t link, double, bool small) {
                auto& bottleneck = small ? pair.smallBottleneck : pair.largeBottleneck;
                bottleneck = std::max(bottleneck, load[link]);
            });
        }
    }
    // Every link has unit capacity, so the busiest link sets r.
    return *std::max_element(load.begin(), load.end());
}

OversubscriptionResult
OversubscriptionSolver::Solve(const std::vector<uint32_t>& matching,
                              double epsilon,
                              double tolerance,
                              uint32_t maxPhases) const
{
    return Solve(matching, Bypass{}, epsilon, tolerance, maxPhases);
}

OversubscriptionResult
OversubscriptionSolver::Solve(const std::vector<uint32_t>& matching,
                              const Bypass& bypass,
                              double epsilon,
                              double tolerance,
                              uint32_t maxPhases) const
{
    OversubscriptionResult result;
    result.ecmp = Ecmp(matching, bypass, nullptr);

    // Each ToR sends its full rate, d unit-capacity links, to matching[s]: its
    // sprayed bytes as one commodity and its bypassed bytes as another.  lambda is
    // the fraction of that demand every ToR can send; r = 1/lambda.
    struct Commodity
    {
        uint32_t source;
        uint32_t destination;
        bool spray;
        double demand;
    };

    std::vector<Commodity> commodities;
    for (uint32_t s = 0; s < m_nodes; ++s)
    {
        const double small = SmallShare(s, matching[s], bypass) * m_degree;
        if (m_degree - small > 0)
        {
            commodities.push_back({s, matching[s], true, m_degree - small});
        }
        if (small > 0)
        {
            commodities.push_back({s, matching[s], false, small});
        }
    }
    const size_t links = static_cast<size_t>(m_nodes) * m_degree;
    const double delta = (1 + epsilon) / std::pow((1 + epsilon) * links, 1 / epsilon);
    std::vector<double> lengths(links, delta);
    std::vector<double> flow(links, 0.0);
    std::vector<double> routed(commodities.size(), 0.0);
    double volume = links * delta; // sum of length * capacity
    PathFinder finder(*this, lengths);
    std::vector<uint32_t> path;

    double feasibleLambda = 0; // from a flow scaled to fit capacities
    double dualLambda = std::numeric_limits<double>::infinity();
    bool exhausted = false;
    while (!exhausted && result.phases < maxPhases)
    {
        for (size_t k = 0; k < commodities.size() && !exhausted; ++k)
        {
            const auto& commodity = commodities[k];
            for (double remaining = commodity.demand; remaining > 0; remaining -= 1)
            {
                if (volume >= 1)
                {
                    exhausted = true;
                    break;
                }
                finder.Shortest(commodity.source, commodity.destination, commodity.spray, &path);
                const double amount = std::min(remaining, 1.0);
                for (auto link : path)
                {
                    flow[link] += amount;
                    volume += lengths[link] * epsilon * amount;
                    lengths[link] *= 1 + epsilon * amount;
                }
                routed[k] += amount;
            }
        }
        ++result.phases;

        // Any flow divided by its busiest link's load is feasible.
        const double busiest = *std::max_element(flow.begin(), flow.end());
        double least = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < commodities.size(); ++k)
        {
            least = std::min(least, routed[k] / commodities[k].demand);
        }
        feasibleLambda = std::max(feasibleLambda, least / busiest);

        // LP duality: lambda* <= sum(length * capacity) / sum(demand * distance).
        double weighted = 0;
        for (const auto& commodity : commodities)
        {
            weighted +=
                commodity.demand *
                finder.Shortest(commodity.source, commodity.destination, commodity.spray, nullptr);
        }
        const double exactVolume = std::accumulate(lengths.begin(), lengths.end(), 0.0);
        dualLambda = std::min(dualLambda, exactVolume / weighted);

        if (feasibleLambda >= (1 - tolerance) * dualLambda)
        {
            break;
        }
    }
    result.lower = 1 / dualLambda;
    result.upper = 1 / feasibleLambda;
    return result;
}

} // namespace rng
