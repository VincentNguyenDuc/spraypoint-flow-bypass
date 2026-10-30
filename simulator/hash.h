#ifndef RNG_HASH_H
#define RNG_HASH_H

#include <cstdint>

namespace rng
{

inline uint64_t
Mix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

inline uint64_t
Key(uint64_t seed, uint32_t a, uint32_t b, uint32_t c = 0)
{
    return Mix(seed ^ (static_cast<uint64_t>(a) << 32) ^ (static_cast<uint64_t>(b) << 1) ^
               static_cast<uint64_t>(c));
}

} // namespace rng

#endif // RNG_HASH_H
