#ifndef RNG_OVERSUBSCRIPTION_H
#define RNG_OVERSUBSCRIPTION_H

#include "spraypoint.h"

#include <cstdint>
#include <vector>

namespace rng
{

// Oversubscription r for one traffic matrix: every ToR can send at least 1/r
// of its full rate (d links) without exceeding any link capacity (§9.1).
struct OversubscriptionResult
{
    double ecmp{0};  // r when each hop splits traffic evenly over its ECMP next hops
    double lower{0}; // r with the best split over Spraypoint paths is at least this...
    double upper{0}; // ...and at most this
    uint32_t phases{0};
};

// Random ToR-level permutation with no fixed points: ToR s sends to matching[s].
std::vector<uint32_t> RandomMatching(uint32_t nodes, uint64_t seed);

// Solves the paper's multi-commodity flow LP as a maximum concurrent flow with
// the Garg-Koenemann algorithm, restricted to Spraypoint paths: one spray hop
// to any neighbor of the source, then the destination's pointing graph.
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
    double EcmpOversubscription(const std::vector<uint32_t>& matching) const;

    const Spraypoint& m_routing;
    uint32_t m_nodes;
    uint32_t m_degree;
    std::vector<PointingDag> m_dags; // indexed by destination
};

} // namespace rng

#endif // RNG_OVERSUBSCRIPTION_H
