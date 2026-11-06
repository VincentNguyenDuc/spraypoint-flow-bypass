#include "rng-experiment.h"
#include "tcp-experiment.h"

#include "ns3/core-module.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

using namespace ns3;

int
main(int argc, char* argv[])
{
    std::string experiment = "baseline";
    uint32_t nodes = 1000;
    uint32_t degree = 64;
    uint32_t waypoints = 4;
    uint32_t nextHops = 2;
    uint32_t flows = 20000;
    uint32_t samples = 100;
    uint64_t seed = 561;
    uint32_t hostsPerRouter = 1;
    double load = 0.1;
    uint32_t smallBytes = 10000;
    uint32_t largeBytes = 1000000;
    double smallFraction = 0.9;
    uint32_t minRtoUs = 1000;
    uint32_t maxSimMs = 1000;

    CommandLine cmd(__FILE__);
    cmd.AddValue("experiment", "baseline (hop-count sample) or tcp (TCP flows)", experiment);
    cmd.AddValue("nodes", "Number of ToR routers (paper default: 1000)", nodes);
    cmd.AddValue("degree", "Fabric degree per ToR (paper default: 64)", degree);
    cmd.AddValue("waypoints", "p: selected waypoint neighbors (paper default: 4)", waypoints);
    cmd.AddValue("nextHops", "h: pointing ECMP next hops (paper default: 2)", nextHops);
    cmd.AddValue("flows",
                 "baseline: single-packet flows sampling path lengths; tcp: TCP flows",
                 flows);
    cmd.AddValue("samples", "baseline: source/destination pairs for max-flow sampling", samples);
    cmd.AddValue("seed", "Random topology and ECMP seed", seed);
    cmd.AddValue("hostsPerRouter", "tcp: hosts attached to each ToR", hostsPerRouter);
    cmd.AddValue("load", "tcp: offered load as a fraction of host link capacity", load);
    cmd.AddValue("smallBytes", "tcp: size of a small flow", smallBytes);
    cmd.AddValue("largeBytes", "tcp: size of a large flow", largeBytes);
    cmd.AddValue("smallFraction", "tcp: fraction of flows that are small", smallFraction);
    cmd.AddValue("minRtoUs", "tcp: minimum retransmission timeout in microseconds", minRtoUs);
    cmd.AddValue("maxSimMs", "tcp: stop after this much simulated time", maxSimMs);
    cmd.Parse(argc, argv);

    try
    {
        if (experiment == "tcp")
        {
            rng::TcpExperiment(rng::TcpExperimentConfig{.nodes = nodes,
                                                        .degree = degree,
                                                        .waypoints = waypoints,
                                                        .nextHops = nextHops,
                                                        .seed = seed,
                                                        .hostsPerRouter = hostsPerRouter,
                                                        .flows = flows,
                                                        .load = load,
                                                        .smallBytes = smallBytes,
                                                        .largeBytes = largeBytes,
                                                        .smallFraction = smallFraction,
                                                        .minRtoUs = minRtoUs,
                                                        .maxSimMs = maxSimMs})
                .Run();
        }
        else if (experiment == "baseline")
        {
            rng::RngExperiment(nodes, degree, waypoints, nextHops, flows, samples, seed).Run();
        }
        else
        {
            std::cerr << "error: unknown experiment " << experiment << "\n";
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
