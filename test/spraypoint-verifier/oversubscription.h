#ifndef RNG_OVERSUBSCRIPTION_H
#define RNG_OVERSUBSCRIPTION_H

#include "spraypoint/matching.h"
#include "spraypoint/spraypoint.h"

#include <cstdint>
#include <vector>

namespace rng
{

// Which bytes skip Spraypoint and take a shortest path instead, as in tcp-bypass.
enum class BypassMode
{
    NONE,
    ADJACENT, // only between adjacent routers, on the direct link
    SHORTEST, // between every pair, hashed over shortest paths
};

struct Bypass
{
    BypassMode mode{BypassMode::NONE};
    double share{0}; // f: the share of each bypassed pair's bytes in small flows
};

// One pair of a matching under ECMP splitting: the busiest link that its small
// (bypassed) bytes and its large (sprayed) bytes cross, in units of the link's
// capacity, with every ToR sending its full rate.  A pair can send at most
// 1/bottleneck of its full rate.  0 when the pair sends no bytes of that kind.
struct PairLoad
{
    uint32_t source{0};
    uint32_t destination{0};
    double smallBottleneck{0};
    double largeBottleneck{0};
};

// Oversubscription r for one traffic matrix: every ToR can send at least 1/r
// of its full rate (d links) without exceeding any link capacity (§9.1).
struct OversubscriptionResult
{
    double ecmp{0};  // r when each hop splits traffic evenly over its ECMP next hops
    double lower{0}; // r with the best split over Spraypoint paths is at least this...
    double upper{0}; // ...and at most this
    uint32_t phases{0};
};

// Solves the paper's multi-commodity flow LP as a maximum concurrent flow with
// the Garg-Koenemann algorithm, restricted to Spraypoint paths: one spray hop
// to any neighbor of the source, then the destination's pointing graph.  With a
// bypass, each pair's small bytes are a separate commodity restricted to the
// shortest paths between the pair.
class OversubscriptionSolver
{
  public:
    explicit OversubscriptionSolver(const Spraypoint& routing);

    // Thread-safe; stops once the bracket [lower, upper] is within the
    // relative tolerance or after maxPhases.
    OversubscriptionResult Solve(const std::vector<uint32_t>& matching,
                                 double epsilon,
                                 double tolerance,
                                 uint32_t maxPhases) const;
    OversubscriptionResult Solve(const std::vector<uint32_t>& matching,
                                 const Bypass& bypass,
                                 double epsilon,
                                 double tolerance,
                                 uint32_t maxPhases) const;

    // r with ECMP splitting alone, without the LP; fills pairs when given.
    double Ecmp(const std::vector<uint32_t>& matching,
                const Bypass& bypass,
                std::vector<PairLoad>* pairs) const;

    // Shortest-path hops from source to destination.
    uint32_t Distance(uint32_t source, uint32_t destination) const;
    // c: edge-disjoint shortest paths from source to destination.
    uint32_t ShortestEdgeDisjointPaths(uint32_t source, uint32_t destination) const;

  private:
    // Pointing graph toward one destination, with arcs stored as link ids.
    struct PointingDag
    {
        std::vector<uint32_t> farthestFirst; // non-destination nodes in topological order
        std::vector<uint32_t> arcStart;      // arcs of node u: [arcStart[u], arcStart[u+1])
        std::vector<uint32_t> arcLink;
        std::vector<uint32_t> arcTarget;
    };

    class PathFinder;

    uint32_t Link(uint32_t from, uint32_t to) const;
    // f for one pair: the share of its bytes that skip Spraypoint.
    double SmallShare(uint32_t source, uint32_t destination, const Bypass& bypass) const;
    // Splits mass at each node evenly over its arcs toward the destination, calling
    // visit(link, amount) for every arc that carries some.
    template <typename Visit>
    void Spread(const PointingDag& dag, std::vector<double>& mass, Visit visit) const;

    const Spraypoint& m_routing;
    uint32_t m_nodes;
    uint32_t m_degree;
    std::vector<PointingDag> m_dags;              // Spraypoint pointing, indexed by destination
    std::vector<PointingDag> m_shortestDags;      // shortest paths, indexed by destination
    std::vector<std::vector<uint8_t>> m_distance; // hops, indexed as [destination][node]
};

} // namespace rng

#endif // RNG_OVERSUBSCRIPTION_H
