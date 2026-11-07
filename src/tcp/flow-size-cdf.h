#ifndef RNG_FLOW_SIZE_CDF_H
#define RNG_FLOW_SIZE_CDF_H

#include <cstdint>
#include <string>
#include <vector>

namespace rng
{

// Flow-size distribution read from a text file with one "<bytes> <cumulative
// probability>" pair per line, ascending and ending at probability 1.  Blank
// lines and lines starting with # are ignored.  Sizes are interpolated linearly
// between points.
class FlowSizeCdf
{
  public:
    explicit FlowSizeCdf(const std::string& path);

    // Maps u in [0, 1] to a flow size of at least one byte.
    uint32_t Sample(double u) const;
    double Mean() const;

  private:
    struct Point
    {
        double bytes;
        double probability;
    };

    std::vector<Point> m_points;
};

} // namespace rng

#endif // RNG_FLOW_SIZE_CDF_H
