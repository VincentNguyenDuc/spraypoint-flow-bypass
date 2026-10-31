#ifndef RNG_SPRAYPOINT_H
#define RNG_SPRAYPOINT_H

#include <cstdint>
#include <vector>

namespace rng
{

// Random d-regular RNG topology plus Spraypoint spray and pointing tables.
class Spraypoint
{
  public:
    Spraypoint(uint32_t nodes,
               uint32_t degree,
               uint32_t waypoints,
               uint32_t nextHops,
               uint64_t seed);

    const std::vector<std::vector<uint32_t>>& GetGraph() const;
    int GetLevels() const;
    uint32_t GetWaypointCount() const;
    uint32_t GetNextHopCount() const;
    // Level of node relative to destination: 0..ell for waypoint levels WP0..WPell,
    // ell+1 for the inner ring, ell+2 for the outer ring, and -1 for the destination.
    int GetLevel(uint32_t destination, uint32_t node) const;
    const std::vector<uint32_t>& GetPointing(uint32_t destination, uint32_t node) const;

    uint32_t SprayHop(uint32_t source, uint32_t destination, uint32_t flow) const;
    uint32_t PointHop(uint32_t current, uint32_t destination, uint32_t flow) const;
    bool Adjacent(uint32_t a, uint32_t b) const;
    uint32_t EdgeDisjointPaths(uint32_t source, uint32_t destination) const;

  private:
    void BuildRandomRegularGraph();
    std::vector<uint32_t> Choose(const std::vector<uint32_t>& candidates,
                                 uint32_t count,
                                 uint32_t destination,
                                 uint32_t node,
                                 uint32_t phase) const;
    void BuildPointingTables();

    uint32_t m_nodes;
    uint32_t m_degree;
    uint32_t m_waypoints;
    uint32_t m_nextHops;
    uint64_t m_seed;
    int m_levels;
    std::vector<std::vector<uint32_t>> m_graph;
    // Indexed as [destination][current router][eligible next hops].
    std::vector<std::vector<std::vector<uint32_t>>> m_pointing;
    // Indexed as [destination][node], encoded as described for GetLevel.
    std::vector<std::vector<int8_t>> m_nodeLevel;
};

} // namespace rng

#endif // RNG_SPRAYPOINT_H
