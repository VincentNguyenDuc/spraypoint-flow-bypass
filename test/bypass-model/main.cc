// Checks the bypass models in BYPASS_MODELLING.md against the routing tables, with
// no ns-3:
//
// - Model B.1: shortest distances between routers, the hops a bypassed flow takes.
// - Model B.2: c, the edge-disjoint shortest paths between a pair.
// - Model 3: oversubscription under random matchings as the share f of each pair's
//   bytes on the bypass grows, both with ECMP splitting and with the best split
//   (the LP), and which pairs become the bottleneck.
//
// It writes CSV files and summary.txt to --outputDir, so experiments/common.sh can
// run it like the simulators.

#include "checks.h"
#include "oversubscription.h"
#include "paper-models.h"
#include "spraypoint/spraypoint.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <sstream>
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
    uint32_t matchings = 10;
    uint32_t lpMatchings = 10;
    std::vector<double> shares{0, 0.01, 0.02, 0.05, 0.1, 0.2, 0.3};
    double epsilon = 0.05;
    double tolerance = 0.01;
    uint32_t maxPhases = 10000;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::string outputDir = ".";
};

std::vector<double>
ParseShares(const std::string& text)
{
    std::vector<double> shares;
    std::stringstream stream(text);
    for (std::string item; std::getline(stream, item, ',');)
    {
        const double share = std::stod(item);
        if (share < 0 || share > 1)
        {
            throw std::runtime_error("shares must be between 0 and 1");
        }
        shares.push_back(share);
    }
    // Share 0 is the no-bypass baseline, which every other share is compared with.
    if (std::find(shares.begin(), shares.end(), 0.0) == shares.end())
    {
        shares.insert(shares.begin(), 0.0);
    }
    std::sort(shares.begin(), shares.end());
    return shares;
}

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
        {"matchings", "random matchings for oversubscription"},
        {"lpMatchings", "how many of those also get the slower LP (0 skips it)"},
        {"shares", "comma-separated bypassed shares f, each 0 to 1"},
        {"epsilon", "Garg-Koenemann step size"},
        {"tolerance", "stop once the oversubscription bracket is this tight"},
        {"maxPhases", "Garg-Koenemann phase limit per matching"},
        {"threads", "worker threads"},
        {"outputDir", "write the CSV files and summary.txt here"},
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
                std::fprintf(stderr, "  --%-12s %s\n", option.c_str(), text.c_str());
            }
            std::exit(arg == "--help" ? 0 : 2);
        }
        const auto value = arg.substr(equals + 1);
        if (name == "epsilon")
        {
            options.epsilon = std::stod(value);
        }
        else if (name == "tolerance")
        {
            options.tolerance = std::stod(value);
        }
        else if (name == "seed")
        {
            options.seed = std::stoull(value);
        }
        else if (name == "shares")
        {
            options.shares = ParseShares(value);
        }
        else if (name == "outputDir")
        {
            options.outputDir = value;
        }
        else
        {
            const auto number = static_cast<uint32_t>(std::stoul(value));
            std::map<std::string, uint32_t*> fields = {
                {"nodes", &options.nodes},
                {"degree", &options.degree},
                {"waypoints", &options.waypoints},
                {"nextHops", &options.nextHops},
                {"matchings", &options.matchings},
                {"lpMatchings", &options.lpMatchings},
                {"maxPhases", &options.maxPhases},
            };
            if (name == "threads")
            {
                options.threads = std::max(1u, number);
            }
            else
            {
                *fields.at(name) = number;
            }
        }
    }
    options.lpMatchings = std::min(options.lpMatchings, options.matchings);
    return options;
}

// Runs work(i) for i in [0, count) on the given number of threads.
template <typename Work>
void
ParallelFor(uint32_t count, unsigned threads, Work work)
{
    std::atomic<uint32_t> next{0};
    std::vector<std::future<void>> pending;
    for (unsigned i = 0; i < std::min<unsigned>(threads, count); ++i)
    {
        pending.push_back(std::async(std::launch::async, [&] {
            for (uint32_t k = next++; k < count; k = next++)
            {
                work(k);
            }
        }));
    }
    for (auto& job : pending)
    {
        job.get();
    }
}

const char*
ModeName(rng::BypassMode mode)
{
    switch (mode)
    {
    case rng::BypassMode::ADJACENT:
        return "adjacent";
    case rng::BypassMode::SHORTEST:
        return "shortest";
    default:
        return "none";
    }
}

double
Poisson(double mean, uint32_t k)
{
    return std::exp(-mean + k * std::log(mean) - std::lgamma(k + 1.0));
}

// Appends printf-style text to out and prints it.
__attribute__((format(printf, 2, 3))) void
Print(std::string& out, const char* format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    out += line;
    std::fputs(line, stdout);
}

// One oversubscription run: a matching with one bypass setting.
struct Task
{
    uint32_t matching;
    rng::Bypass bypass;
};

struct TaskResult
{
    rng::OversubscriptionResult oversubscription;
    bool lp{false};
    std::vector<rng::PairLoad> pairs;
};

} // namespace

int
main(int argc, char* argv[])
{
    try
    {
        const auto options = ParseOptions(argc, argv);
        const double n = options.nodes;
        const double d = options.degree;
        const rng::Spraypoint routing(options.nodes,
                                      options.degree,
                                      options.waypoints,
                                      options.nextHops,
                                      options.seed);
        const rng::OversubscriptionSolver solver(routing);
        const int ell = routing.GetLevels();
        std::filesystem::create_directories(options.outputDir);
        const std::filesystem::path out(options.outputDir);

        // The models' inputs: Model 7.3's oversubscription, 1/sum(mu), and the mean
        // Spraypoint path, which bounds ECMP's oversubscription from below (Lemma G.2).
        const auto modelOversubscription =
            rng::model::Oversubscription(n, d, options.waypoints, options.nextHops, ell);
        const auto lengths = rng::PathLengthDistribution(routing);
        double sprayHops = 0;
        for (size_t hops = 0; hops < lengths.size(); ++hops)
        {
            sprayHops += hops * lengths[hops];
        }

        // Distance and c over every ordered pair.
        constexpr uint32_t kMaxDistance = 8;
        std::vector<uint32_t> distance(options.nodes * options.nodes);
        std::vector<uint32_t> paths(options.nodes * options.nodes);
        ParallelFor(options.nodes, options.threads, [&](uint32_t s) {
            for (uint32_t t = 0; t < options.nodes; ++t)
            {
                if (s != t)
                {
                    distance[s * options.nodes + t] = solver.Distance(s, t);
                    paths[s * options.nodes + t] = solver.ShortestEdgeDisjointPaths(s, t);
                }
            }
        });
        std::vector<uint64_t> byDistance(kMaxDistance + 1, 0);
        std::map<std::pair<uint32_t, uint32_t>, uint64_t> byPaths;
        double meanDistance = 0;
        for (uint32_t s = 0; s < options.nodes; ++s)
        {
            for (uint32_t t = 0; t < options.nodes; ++t)
            {
                if (s != t)
                {
                    const auto k = distance[s * options.nodes + t];
                    if (k > kMaxDistance)
                    {
                        throw std::runtime_error("shortest path too long to tabulate");
                    }
                    ++byDistance[k];
                    ++byPaths[{k, paths[s * options.nodes + t]}];
                    meanDistance += k;
                }
            }
        }
        const double pairs = n * (n - 1);
        meanDistance /= pairs;

        // Model B.1: P(L <= k) = 1 - exp(-d^k / n).
        const auto modelAtMost = [&](uint32_t k) {
            return k == 0 ? 0.0 : 1 - std::exp(-std::pow(d, k) / n);
        };
        {
            std::ofstream file(out / "distances.csv");
            file << "distance,pairs,fraction,model_fraction\n";
            for (uint32_t k = 1; k <= kMaxDistance; ++k)
            {
                const double model = modelAtMost(k) - modelAtMost(k - 1);
                if (byDistance[k] > 0 || model > 1e-6)
                {
                    file << k << ',' << byDistance[k] << ',' << byDistance[k] / pairs << ','
                         << model << '\n';
                }
            }
        }
        // Model B.2: c = 1 at distance 1, and Poisson(d^2/n) given c >= 1 at distance 2.
        {
            std::ofstream file(out / "shortest-paths.csv");
            file << "distance,c,pairs,fraction_of_distance,model_fraction_of_distance\n";
            const double mean = d * d / n;
            for (const auto& [key, count] : byPaths)
            {
                const auto [k, c] = key;
                double model = std::nan("");
                if (k == 1)
                {
                    model = c == 1 ? 1.0 : 0.0;
                }
                else if (k == 2)
                {
                    model = Poisson(mean, c) / (1 - std::exp(-mean));
                }
                file << k << ',' << c << ',' << count << ','
                     << static_cast<double>(count) / byDistance[k] << ',' << model << '\n';
            }
        }

        // Model 3: one task per matching, bypass mode and share; share 0 is the
        // baseline, shared by both modes.
        std::vector<Task> tasks;
        for (uint32_t k = 0; k < options.matchings; ++k)
        {
            for (auto mode : {rng::BypassMode::ADJACENT, rng::BypassMode::SHORTEST})
            {
                for (auto share : options.shares)
                {
                    if (share == 0 && mode == rng::BypassMode::SHORTEST)
                    {
                        continue;
                    }
                    tasks.push_back({k, {share == 0 ? rng::BypassMode::NONE : mode, share}});
                }
            }
        }
        std::vector<std::vector<uint32_t>> matchings(options.matchings);
        for (uint32_t k = 0; k < options.matchings; ++k)
        {
            matchings[k] = rng::RandomMatching(options.nodes, options.seed + 1000 + k);
        }
        std::vector<TaskResult> results(tasks.size());
        ParallelFor(static_cast<uint32_t>(tasks.size()), options.threads, [&](uint32_t i) {
            const auto& task = tasks[i];
            auto& result = results[i];
            const auto& matching = matchings[task.matching];
            result.lp = task.matching < options.lpMatchings;
            if (result.lp)
            {
                result.oversubscription = solver.Solve(matching,
                                                       task.bypass,
                                                       options.epsilon,
                                                       options.tolerance,
                                                       options.maxPhases);
            }
            result.oversubscription.ecmp = solver.Ecmp(matching, task.bypass, &result.pairs);
        });

        std::ofstream oversubscription(out / "oversubscription.csv");
        oversubscription << "matching,mode,share,ecmp_r,lp_lower,lp_upper,lp_phases,min_c,"
                            "model_ideal,model_ecmp\n";
        std::ofstream pairLoads(out / "pairs.csv");
        pairLoads << "matching,mode,share,source,destination,distance,c,small_bottleneck,"
                     "large_bottleneck\n";

        // Worst case over matchings, by mode and share, for the summary.
        struct Worst
        {
            double ecmp{0}, lower{0}, upper{0}, ideal{0}, modelEcmp{0};
            bool lp{false};
        };

        std::map<std::pair<std::string, double>, Worst> worst;
        for (size_t i = 0; i < tasks.size(); ++i)
        {
            const auto& task = tasks[i];
            const auto& result = results[i];
            const auto& matching = matchings[task.matching];
            // The weakest bypassed pair sets both models' cost.
            uint32_t minPaths = std::numeric_limits<uint32_t>::max();
            for (uint32_t s = 0; s < options.nodes; ++s)
            {
                const auto t = matching[s];
                const bool bypassed = task.bypass.mode == rng::BypassMode::SHORTEST ||
                                      (task.bypass.mode == rng::BypassMode::ADJACENT &&
                                       distance[s * options.nodes + t] == 1);
                if (bypassed)
                {
                    minPaths = std::min(minPaths, paths[s * options.nodes + t]);
                }
            }
            const double f = task.bypass.share;
            const double bypassCost =
                minPaths == std::numeric_limits<uint32_t>::max() ? 0 : f * d / minPaths;
            const double ideal =
                modelOversubscription ? std::max(*modelOversubscription, bypassCost) : std::nan("");
            const double modelEcmp = sprayHops * (1 - f) + bypassCost;
            const auto& r = result.oversubscription;
            const auto mode = ModeName(task.bypass.mode);
            oversubscription << task.matching << ',' << mode << ',' << f << ',' << r.ecmp << ',';
            if (result.lp)
            {
                oversubscription << r.lower << ',' << r.upper << ',' << r.phases;
            }
            else
            {
                oversubscription << ",,";
            }
            oversubscription << ','
                             << (minPaths == std::numeric_limits<uint32_t>::max()
                                     ? std::string()
                                     : std::to_string(minPaths))
                             << ',' << ideal << ',' << modelEcmp << '\n';
            for (const auto& pair : result.pairs)
            {
                const auto index = pair.source * options.nodes + pair.destination;
                pairLoads << task.matching << ',' << mode << ',' << f << ',' << pair.source << ','
                          << pair.destination << ',' << distance[index] << ',' << paths[index]
                          << ',' << pair.smallBottleneck << ',' << pair.largeBottleneck << '\n';
            }
            auto& w = worst[{mode, f}];
            w.ecmp = std::max(w.ecmp, r.ecmp);
            w.ideal = std::max(w.ideal, ideal);
            w.modelEcmp = std::max(w.modelEcmp, modelEcmp);
            if (result.lp)
            {
                w.lp = true;
                w.lower = std::max(w.lower, r.lower);
                w.upper = std::max(w.upper, r.upper);
            }
        }

        std::string summary;
        Print(summary,
              "parameters n=%u d=%u p=%u h=%u ell=%d seed=%llu matchings=%u lp_matchings=%u\n",
              options.nodes,
              options.degree,
              options.waypoints,
              options.nextHops,
              ell,
              static_cast<unsigned long long>(options.seed),
              options.matchings,
              options.lpMatchings);
        Print(summary, "\n[distance] shortest hops over all pairs vs. Model B.1\n");
        Print(summary, "  %8s %10s %10s\n", "distance", "measured", "model");
        for (uint32_t k = 1; k <= kMaxDistance; ++k)
        {
            if (byDistance[k] > 0)
            {
                Print(summary,
                      "  %8u %10.4f %10.4f\n",
                      k,
                      byDistance[k] / pairs,
                      modelAtMost(k) - modelAtMost(k - 1));
            }
        }
        Print(summary, "  mean %.3f; Spraypoint mean %.3f\n", meanDistance, sprayHops);
        Print(
            summary,
            "\n[oversubscription] worst over matchings; model 3 uses the weakest bypassed pair\n");
        Print(summary,
              "  %-8s %6s %8s %18s %11s %10s\n",
              "mode",
              "share",
              "ecmp_r",
              "lp_r",
              "model_ideal",
              "model_ecmp");
        for (const auto& [key, w] : worst)
        {
            char lp[32] = "-";
            if (w.lp)
            {
                std::snprintf(lp, sizeof lp, "[%6.3f, %6.3f]", w.lower, w.upper);
            }
            Print(summary,
                  "  %-8s %6.3f %8.3f %18s %11.3f %10.3f\n",
                  key.first.c_str(),
                  key.second,
                  w.ecmp,
                  lp,
                  w.ideal,
                  w.modelEcmp);
        }
        // Written last, so that common.sh treats the run as finished only once every
        // file is complete.
        std::ofstream(out / "summary.txt") << summary;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
