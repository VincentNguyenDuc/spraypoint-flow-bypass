// Checks ShortestPaths against a separate BFS: for every ordered router pair and
// several flows, a path must follow real links and have the shortest length.

#include "spraypoint/spraypoint.h"
#include "tcp-bypass/shortest-paths.h"

#include <cstdio>
#include <exception>
#include <queue>
#include <vector>

namespace
{

int failures = 0;

void
Fail(uint32_t source, uint32_t destination, const char* reason)
{
    if (++failures <= 5)
    {
        std::printf("  %u->%u: %s\n", source, destination, reason);
    }
}

void
Check(uint32_t nodes, uint32_t degree, uint64_t seed)
{
    const rng::Spraypoint tables(nodes, degree, 4, 2, seed);
    const auto& graph = tables.GetGraph();
    const rng::ShortestPaths paths(graph, seed);

    for (uint32_t destination = 0; destination < nodes; ++destination)
    {
        std::vector<int> distance(nodes, -1);
        distance[destination] = 0;
        std::queue<uint32_t> bfs;
        bfs.push(destination);
        while (!bfs.empty())
        {
            const auto node = bfs.front();
            bfs.pop();
            for (auto neighbor : graph[node])
            {
                if (distance[neighbor] < 0)
                {
                    distance[neighbor] = distance[node] + 1;
                    bfs.push(neighbor);
                }
            }
        }

        for (uint32_t source = 0; source < nodes; ++source)
        {
            if (source == destination)
            {
                continue;
            }
            if (paths.Distance(destination, source) != static_cast<uint32_t>(distance[source]))
            {
                Fail(source, destination, "wrong distance");
            }
            for (uint32_t flow = 0; flow < 4; ++flow)
            {
                auto current = source;
                int hops = 0;
                while (current != destination && hops <= distance[source])
                {
                    const auto next = paths.NextHop(current, destination, flow);
                    if (!tables.Adjacent(current, next))
                    {
                        Fail(source, destination, "hop between routers with no link");
                    }
                    current = next;
                    ++hops;
                }
                if (current != destination || hops != distance[source])
                {
                    Fail(source, destination, "path is not a shortest path");
                }
            }
        }
    }
}

} // namespace

int
main()
{
    try
    {
        Check(64, 8, 561);
        Check(256, 16, 562);
    }
    catch (const std::exception& error)
    {
        std::printf("  error: %s\n", error.what());
        ++failures;
    }
    std::printf("[shortest paths] checked against BFS: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
