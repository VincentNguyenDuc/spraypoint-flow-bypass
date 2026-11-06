#include "checks.h"

#include "flow-size-cdf.h"
#include "hash.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <queue>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

namespace rng
{

namespace
{

// Longest pointing path from node to destination, or nullopt on a forwarding loop.
// state: 0 unvisited, 1 on the current DFS path, 2 done.
std::optional<uint32_t>
LongestPointing(const Spraypoint& routing,
                uint32_t destination,
                uint32_t node,
                std::vector<uint8_t>& state,
                std::vector<uint32_t>& hops)
{
    if (node == destination)
    {
        return 0;
    }
    if (state[node] == 2)
    {
        return hops[node];
    }
    if (state[node] == 1)
    {
        return std::nullopt;
    }
    state[node] = 1;
    uint32_t longest = 0;
    for (auto next : routing.GetPointing(destination, node))
    {
        auto sub = LongestPointing(routing, destination, next, state, hops);
        if (!sub)
        {
            return std::nullopt;
        }
        longest = std::max(longest, *sub + 1);
    }
    state[node] = 2;
    hops[node] = longest;
    return longest;
}

} // namespace

StructureReport
CheckStructure(const Spraypoint& routing)
{
    StructureReport report;
    auto fail = [&report](std::string message) {
        if (report.examples.size() < 5)
        {
            report.examples.push_back(std::move(message));
        }
        ++report.violations;
    };

    const auto& graph = routing.GetGraph();
    const auto n = static_cast<uint32_t>(graph.size());
    const auto d = graph[0].size();
    const int ell = routing.GetLevels();
    const auto p = routing.GetWaypointCount();
    const auto h = routing.GetNextHopCount();

    // Simple, symmetric, d-regular graph.
    for (uint32_t u = 0; u < n; ++u)
    {
        const auto& neighbors = graph[u];
        if (neighbors.size() != d)
        {
            fail("node " + std::to_string(u) + " has degree " + std::to_string(neighbors.size()));
        }
        if (std::adjacent_find(neighbors.begin(), neighbors.end()) != neighbors.end() ||
            routing.Adjacent(u, u))
        {
            fail("node " + std::to_string(u) + " has a parallel edge or self-loop");
        }
        for (auto v : neighbors)
        {
            if (!routing.Adjacent(v, u))
            {
                fail("edge " + std::to_string(u) + "-" + std::to_string(v) + " is one-way");
            }
        }
    }

    report.meanLevelSize.assign(ell + 3, 0.0);
    std::vector<int> level(n);
    std::vector<int> toInner(n);
    std::vector<uint8_t> state(n);
    std::vector<uint32_t> hops(n);
    for (uint32_t t = 0; t < n; ++t)
    {
        const auto where = " (t=" + std::to_string(t) + ")";
        for (uint32_t u = 0; u < n; ++u)
        {
            level[u] = routing.GetLevel(t, u);
        }
        if (level[t] != -1)
        {
            fail("destination is not marked as such" + where);
        }

        // Level membership: WP0 = Nbr(t); WPl nodes are neighbors of WP(l-1);
        // IR = neighbors of WPell not in earlier levels; OR = everything else.
        std::vector<uint64_t> size(ell + 3, 0);
        auto hasNeighborAt = [&](uint32_t u, int target) {
            return std::any_of(graph[u].begin(), graph[u].end(), [&](uint32_t v) {
                return level[v] == target;
            });
        };
        for (uint32_t u = 0; u < n; ++u)
        {
            const int l = level[u];
            if (u == t)
            {
                continue;
            }
            if (l < 0 || l > ell + 2)
            {
                fail("node " + std::to_string(u) + " has invalid level" + where);
                continue;
            }
            ++size[l];
            if ((l == 0) != routing.Adjacent(u, t))
            {
                fail("WP0 is not exactly Nbr(t) at node " + std::to_string(u) + where);
            }
            if (l >= 1 && l <= ell && !hasNeighborAt(u, l - 1))
            {
                fail("waypoint " + std::to_string(u) + " has no parent-level neighbor" + where);
            }
            if (l == ell + 1 && !hasNeighborAt(u, ell))
            {
                fail("inner-ring node " + std::to_string(u) + " is not next to WPell" + where);
            }
            if (l == ell + 2 && hasNeighborAt(u, ell))
            {
                fail("outer-ring node " + std::to_string(u) + " is next to WPell" + where);
            }
        }
        for (int l = 1; l <= ell; ++l)
        {
            if (size[l] > p * size[l - 1])
            {
                fail("WP" + std::to_string(l) + " has more than p*|WP(l-1)| nodes" + where);
            }
        }
        for (int l = 0; l <= ell + 2; ++l)
        {
            report.meanLevelSize[l] += static_cast<double>(size[l]) / n;
        }

        // Hop distance to the inner ring, for outer-ring nodes with no inner-ring neighbor.
        std::fill(toInner.begin(), toInner.end(), -1);
        std::queue<uint32_t> bfs;
        for (uint32_t u = 0; u < n; ++u)
        {
            if (level[u] == ell + 1)
            {
                toInner[u] = 0;
                bfs.push(u);
            }
        }
        while (!bfs.empty())
        {
            auto u = bfs.front();
            bfs.pop();
            for (auto v : graph[u])
            {
                if (toInner[v] < 0)
                {
                    toInner[v] = toInner[u] + 1;
                    bfs.push(v);
                }
            }
        }

        // Pointing rules: WP0 -> t; WPl -> WP(l-1); IR -> WPell; OR -> IR, each
        // choosing min(h, eligible) distinct neighbors.
        for (uint32_t u = 0; u < n; ++u)
        {
            const int l = level[u];
            if (u == t || l < 0 || l > ell + 2)
            {
                continue;
            }
            std::vector<uint32_t> eligible;
            if (l == 0)
            {
                eligible.push_back(t);
            }
            else
            {
                const int target = l <= ell ? l - 1 : (l == ell + 1 ? ell : ell + 1);
                for (auto v : graph[u])
                {
                    if (level[v] == target)
                    {
                        eligible.push_back(v);
                    }
                }
                if (l == ell + 2 && eligible.empty())
                {
                    ++report.deepOuterNodes;
                    for (auto v : graph[u])
                    {
                        if (toInner[v] >= 0 && toInner[v] < toInner[u])
                        {
                            eligible.push_back(v);
                        }
                    }
                }
            }
            const auto& next = routing.GetPointing(t, u);
            if (eligible.empty())
            {
                fail("node " + std::to_string(u) + " has no eligible next hop" + where);
                continue;
            }
            if (next.size() != std::min<size_t>(h, eligible.size()))
            {
                fail("node " + std::to_string(u) + " has " + std::to_string(next.size()) +
                     " next hops" + where);
            }
            auto sorted = next;
            std::sort(sorted.begin(), sorted.end());
            if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
            {
                fail("node " + std::to_string(u) + " repeats a next hop" + where);
            }
            for (auto v : next)
            {
                if (!std::binary_search(eligible.begin(), eligible.end(), v))
                {
                    fail("node " + std::to_string(u) + " points to ineligible " +
                         std::to_string(v) + where);
                }
            }
        }

        // Loop freedom; non-deep nodes at level l reach t in exactly l+1 hops.
        std::fill(state.begin(), state.end(), 0);
        for (uint32_t u = 0; u < n; ++u)
        {
            if (u == t)
            {
                continue;
            }
            auto longest = LongestPointing(routing, t, u, state, hops);
            if (!longest)
            {
                fail("forwarding loop through node " + std::to_string(u) + where);
                break;
            }
            report.maxPathHops = std::max(report.maxPathHops, *longest + 1);
            if (level[u] <= ell + 1 && *longest != static_cast<uint32_t>(level[u]) + 1)
            {
                fail("node " + std::to_string(u) + " takes " + std::to_string(*longest) +
                     " pointing hops" + where);
            }
        }
    }
    return report;
}

std::vector<double>
PathLengthDistribution(const Spraypoint& routing)
{
    const auto& graph = routing.GetGraph();
    const auto n = static_cast<uint32_t>(graph.size());
    const auto d = static_cast<double>(graph[0].size());
    constexpr uint32_t kMaxHops = 16;

    std::vector<double> total(kMaxHops + 1, 0.0);
    std::vector<uint8_t> state(n);
    std::vector<uint32_t> hops(n);
    std::vector<uint32_t> order(n);
    // remaining[u][k]: probability that pointing from u reaches t in k hops.
    std::vector<std::vector<double>> remaining(n, std::vector<double>(kMaxHops, 0.0));
    for (uint32_t t = 0; t < n; ++t)
    {
        std::fill(state.begin(), state.end(), 0);
        for (uint32_t u = 0; u < n; ++u)
        {
            if (!LongestPointing(routing, t, u, state, hops))
            {
                throw std::runtime_error("forwarding loop while measuring path lengths");
            }
            if (u != t && hops[u] >= kMaxHops)
            {
                throw std::runtime_error("pointing path too long to measure");
            }
        }
        hops[t] = 0;
        for (uint32_t u = 0; u < n; ++u)
        {
            order[u] = u;
        }
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return hops[a] < hops[b];
        });

        for (auto u : order)
        {
            auto& mine = remaining[u];
            std::fill(mine.begin(), mine.end(), 0.0);
            if (u == t)
            {
                mine[0] = 1.0;
                continue;
            }
            const auto& next = routing.GetPointing(t, u);
            for (auto v : next)
            {
                for (uint32_t k = 0; k + 1 < kMaxHops; ++k)
                {
                    mine[k + 1] += remaining[v][k] / next.size();
                }
            }
        }

        // Each spray target x is reached from its neighbors s != t, each of
        // which picks x with probability 1/d.
        for (uint32_t x = 0; x < n; ++x)
        {
            const double weight = d - (x != t && routing.Adjacent(x, t) ? 1.0 : 0.0);
            for (uint32_t k = 0; k < kMaxHops; ++k)
            {
                total[k + 1] += weight * remaining[x][k];
            }
        }
    }
    const double paths = static_cast<double>(n) * (n - 1) * d;
    for (auto& value : total)
    {
        value /= paths;
    }
    return total;
}

EdgeDisjointSamples
SampleEdgeDisjointPaths(const Spraypoint& routing, uint32_t pairs, uint64_t seed, unsigned threads)
{
    const auto n = static_cast<uint32_t>(routing.GetGraph().size());
    std::mt19937_64 rng(Key(seed, 0xed9e, 0x5a3e));
    std::uniform_int_distribution<uint32_t> pick(0, n - 1);
    std::vector<std::pair<uint32_t, uint32_t>> sampled(pairs);
    for (auto& [source, destination] : sampled)
    {
        source = pick(rng);
        do
        {
            destination = pick(rng);
        } while (destination == source);
    }

    std::vector<uint32_t> paths(pairs);
    std::atomic<uint32_t> nextPair{0};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < std::max(1u, threads); ++i)
    {
        workers.emplace_back([&] {
            for (uint32_t k = nextPair++; k < pairs; k = nextPair++)
            {
                paths[k] = routing.EdgeDisjointPaths(sampled[k].first, sampled[k].second);
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    EdgeDisjointSamples samples;
    for (uint32_t k = 0; k < pairs; ++k)
    {
        const auto& [source, destination] = sampled[k];
        (routing.Adjacent(source, destination) ? samples.adjacent : samples.nonAdjacent)
            .push_back(paths[k]);
    }
    return samples;
}

uint64_t
CheckFlowSizeCdf(const std::string& path, std::vector<std::string>& examples)
{
    {
        std::ofstream file(path);
        file << "# test distribution\n\n100 0.5\n300 1\n";
    }
    const FlowSizeCdf cdf(path);
    std::filesystem::remove(path);

    uint64_t violations = 0;
    const auto expect = [&](const std::string& name, double actual, double expected) {
        if (std::abs(actual - expected) > 1e-9)
        {
            ++violations;
            examples.push_back(name + " is " + std::to_string(actual) + ", expected " +
                               std::to_string(expected));
        }
    };
    expect("Sample(0)", cdf.Sample(0), 100);
    expect("Sample(0.5)", cdf.Sample(0.5), 100);
    expect("Sample(0.75)", cdf.Sample(0.75), 200);
    expect("Sample(1)", cdf.Sample(1), 300);
    // 0.5 of the mass at 100 bytes, and 0.5 spread evenly over 100 to 300.
    expect("Mean()", cdf.Mean(), 150);
    return violations;
}

} // namespace rng
