// Checks FlowSizeCdf's sampling and mean.

#include "tcp/flow-size-cdf.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

namespace
{

int failures = 0;

void
Expect(const char* name, double actual, double expected)
{
    if (std::abs(actual - expected) > 1e-9)
    {
        std::printf("  %s is %g, expected %g\n", name, actual, expected);
        ++failures;
    }
}

} // namespace

int
main()
{
    const auto path = std::filesystem::temp_directory_path() / "rng-test-flow-size.cdf";
    {
        std::ofstream file(path);
        file << "# test distribution\n\n100 0.5\n300 1\n";
    }
    try
    {
        const rng::FlowSizeCdf cdf(path.string());
        Expect("Sample(0)", cdf.Sample(0), 100);
        Expect("Sample(0.5)", cdf.Sample(0.5), 100);
        Expect("Sample(0.75)", cdf.Sample(0.75), 200);
        Expect("Sample(1)", cdf.Sample(1), 300);
        // 0.5 of the mass at 100 bytes, and 0.5 spread evenly over 100 to 300.
        Expect("Mean()", cdf.Mean(), 150);
    }
    catch (const std::exception& error)
    {
        std::printf("  error: %s\n", error.what());
        ++failures;
    }
    std::filesystem::remove(path);
    std::printf("[flow sizes] CDF sampling and mean: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
