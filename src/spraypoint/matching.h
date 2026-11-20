#ifndef RNG_MATCHING_H
#define RNG_MATCHING_H

#include <cstdint>
#include <vector>

namespace rng
{

// Random ToR-level permutation with no fixed points: ToR s sends to matching[s].  The
// worst-case traffic pattern of the paper's §9.1, shared by the flow-level checks and
// the simulator's permutation pattern, so that both see the same pairs.
std::vector<uint32_t> RandomMatching(uint32_t nodes, uint64_t seed);

} // namespace rng

#endif // RNG_MATCHING_H
