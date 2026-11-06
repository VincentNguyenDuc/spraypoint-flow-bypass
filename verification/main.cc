// Checks the Spraypoint implementation in simulator/ against the RNG paper's
// routing rules (§5), models (§7), and benchmarks (§9.1-9.2). It needs no ns-3.

#include "checks.h"
#include "oversubscription.h"
#include "paper-models.h"
#include "spraypoint.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct Options
{
    uint32_t nodes = 1000;
    uint32_t degree = 64;
    uint32_t waypoints = 4;
    uint32_t nextHops = 2;
    uint64_t seed = 561;
    uint32_t pairs = 2000;
    uint32_t matchings = 10;
    double epsilon = 0.05;
    double tolerance = 0.01;
    uint32_t maxPhases = 10000;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
};

Options
ParseOptions(int argc, char* argv[])
{
    Options options;
    const std::map<std::string, std::string> help = {
        {"nodes", "number of ToR routers (paper default: 1000)"},
        {"degree", "fabric degree per ToR (paper default: 64)"},
        {"waypoints", "p: waypoints selected per node (paper default: 4)"},
        {"nextHops", "h: pointing ECMP next hops (paper default: 2)"},
        {"seed", "random topology seed"},
        {"pairs", "source/destination pairs sampled for edge-disjoint paths"},
        {"matchings", "random matchings for oversubscription (paper: 100; 0 skips)"},
        {"epsilon", "Garg-Koenemann step size"},
        {"tolerance", "stop once the oversubscription bracket is this tight"},
        {"maxPhases", "Garg-Koenemann phase limit per matching"},
        {"threads", "worker threads"},
    };
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto equals = arg.find('=');
        const auto name =
            arg.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);
        if (arg.rfind("--", 0) != 0 || equals == std::string::npos || !help.contains(name))
        {
            std::fprintf(stderr, "usage: %s [--name=value ...]\n", argv[0]);
            for (const auto& [option, text] : help)
            {
                std::fprintf(stderr, "  --%-10s %s\n", option.c_str(), text.c_str());
            }
            std::exit(arg == "--help" ? 0 : 2);
        }
        const auto value = arg.substr(equals + 1);
        if (name == "epsilon" || name == "tolerance")
        {
            (name == "epsilon" ? options.epsilon : options.tolerance) = std::stod(value);
        }
        else if (name == "seed")
        {
            options.seed = std::stoull(value);
        }
        else
        {
            const auto number = static_cast<uint32_t>(std::stoul(value));
            if (name == "nodes")
            {
                options.nodes = number;
            }
            else if (name == "degree")
            {
                options.degree = number;
            }
            else if (name == "waypoints")
            {
                options.waypoints = number;
            }
            else if (name == "nextHops")
            {
                options.nextHops = number;
            }
            else if (name == "pairs")
            {
                options.pairs = number;
            }
            else if (name == "matchings")
            {
                options.matchings = number;
            }
            else if (name == "maxPhases")
            {
                options.maxPhases = number;
            }
            else
            {
                options.threads = std::max(1u, number);
            }
        }
    }
    return options;
}

uint32_t
Percentile(const std::vector<uint32_t>& sorted, double quantile)
{
    const auto rank = static_cast<size_t>(quantile * sorted.size());
    return sorted[std::min(rank, sorted.size() - 1)];
}

void
PrintEdgeDisjoint(const char* name, std::vector<uint32_t> values, double model)
{
    if (values.empty())
    {
        std::printf("  %-12s no samples\n", name);
        return;
    }
    std::sort(values.begin(), values.end());
    double mean = 0;
    for (auto value : values)
    {
        mean += value;
    }
    mean /= values.size();
    auto above = [&](uint32_t threshold) {
        return static_cast<double>(values.end() -
                                   std::upper_bound(values.begin(), values.end(), threshold)) /
               values.size();
    };
    std::printf("  %-12s %6zu %6.2f %4u %4u %4u %4u %4u %6.3f %6.3f %8.2f\n",
                name,
                values.size(),
                mean,
                values.front(),
                Percentile(values, 0.10),
                Percentile(values, 0.50),
                Percentile(values, 0.90),
                values.back(),
                above(50),
                above(60),
                model);
}

} // namespace

int
main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);
    try
    {
        const rng::Spraypoint routing(options.nodes,
                                      options.degree,
                                      options.waypoints,
                                      options.nextHops,
                                      options.seed);
        const double n = options.nodes;
        const double d = options.degree;
        const double p = options.waypoints;
        const double h = options.nextHops;
        const int ell = routing.GetLevels();
        std::printf("RNG Spraypoint verification: n=%u d=%u p=%u h=%u ell=%d seed=%llu\n\n",
                    options.nodes,
                    options.degree,
                    options.waypoints,
                    options.nextHops,
                    ell,
                    static_cast<unsigned long long>(options.seed));

        const auto structure = rng::CheckStructure(routing);
        std::printf("[structure] graph and per-destination levels/pointing vs. paper §5\n");
        std::printf("  violations=%llu max_path_hops=%u (paper: <= ell+4 = %d) "
                    "outer_nodes_without_inner_neighbor=%llu\n",
                    static_cast<unsigned long long>(structure.violations),
                    structure.maxPathHops,
                    ell + 4,
                    static_cast<unsigned long long>(structure.deepOuterNodes));
        for (const auto& example : structure.examples)
        {
            std::printf("  violation: %s\n", example.c_str());
        }
        std::printf("  mean level sizes:");
        for (int l = 0; l <= ell + 2; ++l)
        {
            const auto label = l <= ell ? "WP" + std::to_string(l) : (l == ell + 1 ? "IR" : "OR");
            std::printf(" %s=%.1f", label.c_str(), structure.meanLevelSize[l]);
        }
        std::printf("\n\n");

        std::vector<std::string> cdfExamples;
        const auto cdfViolations = rng::CheckFlowSizeCdf(
            (std::filesystem::temp_directory_path() / "rng-verify-flow-size.cdf").string(),
            cdfExamples);
        std::printf("[flow sizes] CDF sampling and mean\n");
        std::printf("  violations=%llu\n", static_cast<unsigned long long>(cdfViolations));
        for (const auto& example : cdfExamples)
        {
            std::printf("  violation: %s\n", example.c_str());
        }
        std::printf("\n");

        const auto lengths = rng::PathLengthDistribution(routing);
        const auto modelLengths = rng::model::PathLengthFractions(n, d, p, ell);
        std::printf("[path length] exact over all pairs and spray choices vs. Model 7.2\n");
        std::printf("  %4s %12s %12s\n", "hops", "measured", "model");
        for (size_t hops = 1; hops < lengths.size(); ++hops)
        {
            const double model = hops < modelLengths.size() ? modelLengths[hops] : 0.0;
            if (lengths[hops] > 0 || model > 0)
            {
                std::printf("  %4zu %12.6g %12.6g\n", hops, lengths[hops], model);
            }
        }
        std::printf("\n");

        const auto samples =
            rng::SampleEdgeDisjointPaths(routing, options.pairs, options.seed, options.threads);
        std::printf("[edge-disjoint paths] min cut over the Spraypoint graph vs. paper §9.2\n");
        std::printf("  %-12s %6s %6s %4s %4s %4s %4s %4s %6s %6s %8s\n",
                    "pairs",
                    "count",
                    "mean",
                    "min",
                    "p10",
                    "p50",
                    "p90",
                    "max",
                    ">50",
                    ">60",
                    "model7.1");
        PrintEdgeDisjoint("nonadjacent",
                          samples.nonAdjacent,
                          rng::model::EdgeDisjointPaths(d, p, h, false));
        PrintEdgeDisjoint("adjacent",
                          samples.adjacent,
                          rng::model::EdgeDisjointPaths(d, p, h, true));
        std::printf("  paper (n=1000 d=64 p=4 h=2): over 50 for almost all pairs, "
                    "over 60 for half; Model 7.1 is a bound, not a fit\n\n");

        if (options.matchings > 0)
        {
            std::printf("[oversubscription] random matchings, full-rate demand vs. paper §9.1\n");
            const rng::OversubscriptionSolver solver(routing);
            std::vector<rng::OversubscriptionResult> results(options.matchings);
            std::vector<std::future<void>> pending;
            std::atomic<uint32_t> next{0};
            for (unsigned i = 0; i < std::min(options.threads, options.matchings); ++i)
            {
                pending.push_back(std::async(std::launch::async, [&] {
                    for (uint32_t k = next++; k < options.matchings; k = next++)
                    {
                        results[k] = solver.Solve(
                            rng::RandomMatching(options.nodes, options.seed + 1000 + k),
                            options.epsilon,
                            options.tolerance,
                            options.maxPhases);
                    }
                }));
            }
            for (auto& job : pending)
            {
                job.get();
            }
            std::printf("  %8s %10s %18s %7s\n", "matching", "ecmp_r", "optimal_r", "phases");
            rng::OversubscriptionResult worst;
            for (uint32_t k = 0; k < options.matchings; ++k)
            {
                const auto& result = results[k];
                std::printf("  %8u %10.3f   [%6.3f, %6.3f] %7u\n",
                            k,
                            result.ecmp,
                            result.lower,
                            result.upper,
                            result.phases);
                worst.ecmp = std::max(worst.ecmp, result.ecmp);
                worst.lower = std::max(worst.lower, result.lower);
                worst.upper = std::max(worst.upper, result.upper);
            }
            std::printf("  %8s %10.3f   [%6.3f, %6.3f]\n",
                        "worst",
                        worst.ecmp,
                        worst.lower,
                        worst.upper);
            const auto model = rng::model::Oversubscription(n, d, p, h, ell);
            std::printf("  paper (n=1000 d=64 p=4 h=2, worst of 100): 3.25; Model 7.3: ");
            if (model)
            {
                std::printf("%.3f\n", *model);
            }
            else
            {
                std::printf("n/a (ell != 1)\n");
            }
        }
        return structure.violations + cdfViolations == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
