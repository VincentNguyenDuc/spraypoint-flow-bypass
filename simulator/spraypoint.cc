#include "spraypoint.h"

#include "hash.h"
#include "max-flow.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace rng
{

Spraypoint::Spraypoint(uint32_t nodes,
                       uint32_t degree,
                       uint32_t waypoints,
                       uint32_t nextHops,
                       uint64_t seed)
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
    m_levels = std::max(
        1,
        static_cast<int>(std::ceil(std::log(static_cast<double>(nodes) / (2.0 * degree * degree)) /
                                   std::log(static_cast<double>(waypoints)))));
    BuildRandomRegularGraph();
    BuildPointingTables();
}

const std::vector<std::vector<uint32_t>>&
Spraypoint::GetGraph() const
{
    return m_graph;
}

int
Spraypoint::GetLevels() const
{
    return m_levels;
}

uint32_t
Spraypoint::GetWaypointCount() const
{
    return m_waypoints;
}

uint32_t
Spraypoint::GetNextHopCount() const
{
    return m_nextHops;
}

uint32_t
Spraypoint::SprayHop(uint32_t source, uint32_t destination, uint32_t flow) const
{
    const auto& neighbors = m_graph[source];
    return neighbors[Key(m_seed, source, destination, flow) % neighbors.size()];
}

uint32_t
Spraypoint::PointHop(uint32_t current, uint32_t destination, uint32_t flow) const
{
    const auto& choices = m_pointing[destination][current];
    if (choices.empty())
    {
        throw std::runtime_error("pointing table has an empty next-hop set");
    }
    return choices[Key(m_seed ^ 0x4d595df4d0f33173ULL, current, destination, flow) %
                   choices.size()];
}

bool
Spraypoint::Adjacent(uint32_t a, uint32_t b) const
{
    return std::binary_search(m_graph[a].begin(), m_graph[a].end(), b);
}

uint32_t
Spraypoint::EdgeDisjointPaths(uint32_t source, uint32_t destination) const
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

void
Spraypoint::BuildRandomRegularGraph()
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

std::vector<uint32_t>
Spraypoint::Choose(const std::vector<uint32_t>& candidates,
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

void
Spraypoint::BuildPointingTables()
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

} // namespace rng
