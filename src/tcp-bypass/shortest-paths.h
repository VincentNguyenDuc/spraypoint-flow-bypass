#ifndef RNG_SHORTEST_PATHS_H
#define RNG_SHORTEST_PATHS_H

#include <cstdint>
#include <vector>

namespace rng
{

// Shortest-path routing over a router graph.  Next hops hash across all
// minimum-distance neighbors, so a flow keeps one path and flows spread over all
// the shortest ones.  It needs no ns-3.
class ShortestPaths
{
  public:
    // Throws if the graph is disconnected.  graph must outlive this object.
    ShortestPaths(const std::vector<std::vector<uint32_t>>& graph, uint64_t seed);

    uint32_t NextHop(uint32_t current, uint32_t destination, uint32_t flow) const;
    uint32_t Distance(uint32_t a, uint32_t b) const;

  private:
    const std::vector<std::vector<uint32_t>>& m_graph;
    uint64_t m_seed;
    // Hops between routers, indexed as [destination][node].
    std::vector<std::vector<uint8_t>> m_distance;
};

} // namespace rng

#endif // RNG_SHORTEST_PATHS_H
