#include "oversubscription.h"

#include "spraypoint/hash.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace rng
{

std::vector<uint32_t>
RandomMatching(uint32_t nodes, uint64_t seed)
{
    std::vector<uint32_t> matching(nodes);
    std::iota(matching.begin(), matching.end(), 0);
    std::mt19937_64 rng(Key(seed, 0x3a7c, 0x41e6));
    std::shuffle(matching.begin(), matching.end(), rng);
    // Swapping a fixed point's target with any other node's removes it without
    // creating a new one.
    for (uint32_t s = 0; s < nodes; ++s)
    {
        if (matching[s] == s)
        {
            std::swap(matching[s], matching[(s + 1) % nodes]);
        }
    }
    return matching;
}

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

    // Returns the path length; fills path with link ids when given.
    double Shortest(uint32_t source, uint32_t destination, std::vector<uint32_t>* path)
    {
        ++m_epoch;
        m_dag = &m_solver.m_dags[destination];
        m_destination = destination;
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

double
OversubscriptionSolver::EcmpOversubscription(const std::vector<uint32_t>& matching) const
{
    const double demand = m_degree;
    std::vector<double> load(static_cast<size_t>(m_nodes) * m_degree, 0.0);
    std::vector<double> mass(m_nodes);
    for (uint32_t s = 0; s < m_nodes; ++s)
    {
        const auto t = matching[s];
        const auto& dag = m_dags[t];
        std::fill(mass.begin(), mass.end(), 0.0);
        const auto& neighbors = m_routing.GetGraph()[s];
        for (uint32_t k = 0; k < neighbors.size(); ++k)
        {
            load[s * m_degree + k] += demand / neighbors.size();
            mass[neighbors[k]] += demand / neighbors.size();
        }
        for (auto u : dag.farthestFirst)
        {
            if (mass[u] == 0)
            {
                continue;
            }
            const auto arcs = dag.arcStart[u + 1] - dag.arcStart[u];
            for (auto arc = dag.arcStart[u]; arc < dag.arcStart[u + 1]; ++arc)
            {
                load[dag.arcLink[arc]] += mass[u] / arcs;
                mass[dag.arcTarget[arc]] += mass[u] / arcs;
            }
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
    OversubscriptionResult result;
    result.ecmp = EcmpOversubscription(matching);

    // Each ToR sends its full rate, d unit-capacity links, to matching[s].
    // lambda is the fraction of that demand every ToR can send; r = 1/lambda.
    const double demand = m_degree;
    const size_t links = static_cast<size_t>(m_nodes) * m_degree;
    const double delta = (1 + epsilon) / std::pow((1 + epsilon) * links, 1 / epsilon);
    std::vector<double> lengths(links, delta);
    std::vector<double> flow(links, 0.0);
    std::vector<double> routed(m_nodes, 0.0);
    double volume = links * delta; // sum of length * capacity
    PathFinder finder(*this, lengths);
    std::vector<uint32_t> path;

    double feasibleLambda = 0; // from a flow scaled to fit capacities
    double dualLambda = std::numeric_limits<double>::infinity();
    bool exhausted = false;
    while (!exhausted && result.phases < maxPhases)
    {
        for (uint32_t s = 0; s < m_nodes && !exhausted; ++s)
        {
            for (double remaining = demand; remaining > 0; remaining -= 1)
            {
                if (volume >= 1)
                {
                    exhausted = true;
                    break;
                }
                finder.Shortest(s, matching[s], &path);
                const double amount = std::min(remaining, 1.0);
                for (auto link : path)
                {
                    flow[link] += amount;
                    volume += lengths[link] * epsilon * amount;
                    lengths[link] *= 1 + epsilon * amount;
                }
                routed[s] += amount;
            }
        }
        ++result.phases;

        // Any flow divided by its busiest link's load is feasible.
        const double busiest = *std::max_element(flow.begin(), flow.end());
        const double least = *std::min_element(routed.begin(), routed.end());
        feasibleLambda = std::max(feasibleLambda, least / demand / busiest);

        // LP duality: lambda* <= sum(length * capacity) / sum(demand * distance).
        double weighted = 0;
        for (uint32_t s = 0; s < m_nodes; ++s)
        {
            weighted += demand * finder.Shortest(s, matching[s], nullptr);
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
