#ifndef RNG_PAPER_MODELS_H
#define RNG_PAPER_MODELS_H

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

// Closed-form models from §7 of Bernardi et al., "RNG: Flat Datacenter
// Networks at Scale" (arXiv:2604.15261v3).
namespace rng::model
{

// Model 7.1: edge-disjoint paths between s and t.
inline double
EdgeDisjointPaths(double d, double p, double h, bool adjacent)
{
    if (adjacent)
    {
        return std::min(d - p, d * (1.0 - std::exp(-(1.0 - p / d) * h)));
    }
    return d * (1.0 - std::exp(-h));
}

// Model 7.2: fraction of paths with i hops, returned for i = 0..ell+4.
inline std::vector<double>
PathLengthFractions(double n, double d, double p, int ell)
{
    std::vector<double> fraction(ell + 5, 0.0);
    fraction[1] = 1.0 / n;
    for (int i = 2; i <= ell + 2; ++i)
    {
        fraction[i] = std::pow(p, i - 2) * d / n;
    }
    fraction[ell + 4] = std::exp(-std::pow(p, ell) * d * d / n);
    double rest = 1.0;
    for (double value : fraction)
    {
        rest -= value;
    }
    fraction[ell + 3] = rest;
    return fraction;
}

// Model 7.3: oversubscription under a random matching. The paper states it
// for the practical regime ell = 1 only.
inline std::optional<double>
Oversubscription(double n, double d, double p, double h, int ell)
{
    if (ell != 1)
    {
        return std::nullopt;
    }
    const double mu2 = d / n;
    const double phi3 =
        std::min(p * d / n, 1.0 - d / n) * (1.0 - d / n) * (1.0 - std::pow(4.0 * d / n, h));
    const double kappa3 =
        std::pow(1.0 - phi3, 6) / 2.0 + std::pow(1.0 - phi3 * phi3, 3) / 6.0 + 1.0 / 3.0;
    const double mu3 = phi3 * kappa3;
    const double outer = std::exp(-p * d * d / n);
    const double mu4 =
        (1.0 - (p + 1.0) * d / n - outer) *
        (1.0 - std::pow(1.0 - (1.0 - 2.0 * d / n) * (1.0 - std::pow(4.0 * d / n, h)), h)) *
        (1.0 - mu2 - 2.0 * mu3) / 4.0;
    const double mu5 = outer * (1.0 - mu2 - 2.0 * mu3 - 3.0 * mu4) / 5.0;
    return 1.0 / (mu2 + mu3 + mu4 + mu5);
}

} // namespace rng::model

#endif // RNG_PAPER_MODELS_H
