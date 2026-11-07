#include "flow-size-cdf.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rng
{

FlowSizeCdf::FlowSizeCdf(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("cannot read flow-size CDF " + path);
    }
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream fields(line);
        Point point{};
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        if (!(fields >> point.bytes >> point.probability))
        {
            throw std::runtime_error("bad line in " + path + ": " + line);
        }
        if (point.bytes < 0 || point.probability < 0 || point.probability > 1 ||
            (!m_points.empty() && (point.bytes < m_points.back().bytes ||
                                   point.probability < m_points.back().probability)))
        {
            throw std::runtime_error("CDF must be ascending within [0, 1]: " + path);
        }
        m_points.push_back(point);
    }
    if (m_points.empty() || m_points.back().probability != 1)
    {
        throw std::runtime_error("CDF must end at probability 1: " + path);
    }
}

uint32_t
FlowSizeCdf::Sample(double u) const
{
    const auto upper = std::find_if(m_points.begin(), m_points.end(), [u](const Point& point) {
        return point.probability >= u;
    });
    double bytes = m_points.back().bytes;
    if (upper == m_points.begin())
    {
        bytes = upper->bytes;
    }
    else if (upper != m_points.end())
    {
        const auto& lower = *(upper - 1);
        const auto fraction = (u - lower.probability) / (upper->probability - lower.probability);
        bytes = lower.bytes + fraction * (upper->bytes - lower.bytes);
    }
    return static_cast<uint32_t>(std::max(1.0, std::round(bytes)));
}

double
FlowSizeCdf::Mean() const
{
    double mean = m_points.front().probability * m_points.front().bytes;
    for (size_t i = 1; i < m_points.size(); ++i)
    {
        mean += (m_points[i].probability - m_points[i - 1].probability) *
                (m_points[i].bytes + m_points[i - 1].bytes) / 2;
    }
    return mean;
}

} // namespace rng
