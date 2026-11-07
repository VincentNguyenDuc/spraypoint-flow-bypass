#include "shortest-paths.h"

#include "spraypoint/hash.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace rng
{

namespace
{

const uint8_t UNREACHABLE = 0xff;

} // namespace

ShortestPaths::ShortestPaths(const std::vector<std::vector<uint32_t>>& graph, uint64_t seed)
    : m_graph(graph),
      m_seed(seed),
      m_distance(graph.size(), std::vector<uint8_t>(graph.size(), UNREACHABLE))
{
    for (uint32_t destination = 0; destination < graph.size(); ++destination)
    {
        auto& distance = m_distance[destination];
        distance[destination] = 0;
        std::queue<uint32_t> bfs;
        bfs.push(destination);
        while (!bfs.empty())
        {
            const auto node = bfs.front();
            bfs.pop();
            for (auto neighbor : graph[node])
            {
                if (distance[neighbor] == UNREACHABLE)
                {
                    distance[neighbor] = distance[node] + 1;
                    bfs.push(neighbor);
                }
            }
        }
        if (std::find(distance.begin(), distance.end(), UNREACHABLE) != distance.end())
        {
            throw std::runtime_error("the router graph is disconnected");
        }
    }
}

uint32_t
ShortestPaths::NextHop(uint32_t current, uint32_t destination, uint32_t flow) const
{
    const auto& distance = m_distance[destination];
    const auto closer = [&](uint32_t neighbor) { return distance[neighbor] < distance[current]; };
    const auto& neighbors = m_graph[current];
    const auto choices = std::count_if(neighbors.begin(), neighbors.end(), closer);
    auto remaining = Key(m_seed ^ 0x2545f4914f6cdd1dULL, current, destination, flow) %
                     std::max<int64_t>(choices, 1);
    for (auto neighbor : neighbors)
    {
        if (closer(neighbor) && remaining-- == 0)
        {
            return neighbor;
        }
    }
    throw std::runtime_error("no shortest-path next hop");
}

uint32_t
ShortestPaths::Distance(uint32_t a, uint32_t b) const
{
    return m_distance[a][b];
}

} // namespace rng
