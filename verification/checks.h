#ifndef RNG_CHECKS_H
#define RNG_CHECKS_H

#include "spraypoint.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rng
{

struct StructureReport
{
    uint64_t violations{0};
    std::vector<std::string> examples; // first few violations, for debugging
    std::vector<double> meanLevelSize; // indexed by level, 0..ell+2
    uint32_t maxPathHops{0};           // longest spray-plus-pointing path
    uint64_t deepOuterNodes{0};        // outer-ring nodes with no inner-ring neighbor
};

// Checks the graph and every destination's levels and pointing table against
// the rules in §5 of the paper, and that pointing is loop-free.
StructureReport CheckStructure(const Spraypoint& routing);

// Exact fraction of paths of each hop count over all ordered (s, t) pairs,
// all d spray choices, and uniform ECMP over pointing next hops.
std::vector<double> PathLengthDistribution(const Spraypoint& routing);

struct EdgeDisjointSamples
{
    std::vector<uint32_t> adjacent;
    std::vector<uint32_t> nonAdjacent;
};

EdgeDisjointSamples SampleEdgeDisjointPaths(const Spraypoint& routing,
                                            uint32_t pairs,
                                            uint64_t seed,
                                            unsigned threads);

// Checks FlowSizeCdf's sampling and mean on a small distribution written to
// path.  Returns the number of violations and fills examples with the first few.
uint64_t CheckFlowSizeCdf(const std::string& path, std::vector<std::string>& examples);

} // namespace rng

#endif // RNG_CHECKS_H
