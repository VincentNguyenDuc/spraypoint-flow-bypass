#include "matching.h"

#include "hash.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>

namespace rng
{

std::vector<uint32_t>
RandomMatching(uint32_t nodes, uint64_t seed)
{
    if (nodes < 2)
    {
        throw std::runtime_error("a matching needs at least two routers");
    }
    std::vector<uint32_t> matching(nodes);
    std::iota(matching.begin(), matching.end(), 0);
    std::mt19937_64 rng(Key(seed, 0x3a7c, 0x41e6));
    std::shuffle(matching.begin(), matching.end(), rng);
    // Swapping a fixed point's target with any other node's removes it without
    // creating a new one.
    for (uint32_t s = 0; s < nodes; ++s)
    {
        if (matching[s] == s)
        {
            std::swap(matching[s], matching[(s + 1) % nodes]);
        }
    }
    return matching;
}

} // namespace rng
