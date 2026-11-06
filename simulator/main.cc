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
    std::string experiment = "reproduction";
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
    double spanMs = 0;
    std::string pattern = "uniform";
    std::string flowSizeCdf;
    uint32_t smallThreshold = 0;
    double linkGbps = 100;
    uint32_t linkDelayNs = 500;
    uint32_t fabricQueuePackets = 100;
    uint32_t nicQueuePackets = 1000;
    uint32_t releaseDelayUs = 0;
    std::string outputDir;

    CommandLine cmd(__FILE__);
    cmd.AddValue("experiment", "reproduction (hop-count sample) or tcp (TCP flows)", experiment);
    cmd.AddValue("nodes", "Number of ToR routers (paper default: 1000)", nodes);
    cmd.AddValue("degree", "Fabric degree per ToR (paper default: 64)", degree);
    cmd.AddValue("waypoints", "p: selected waypoint neighbors (paper default: 4)", waypoints);
    cmd.AddValue("nextHops", "h: pointing ECMP next hops (paper default: 2)", nextHops);
    cmd.AddValue("flows",
                 "reproduction: single-packet flows sampling path lengths; "
                 "tcp: most TCP flows to generate, or 0 for no limit",
                 flows);
    cmd.AddValue("samples", "reproduction: source/destination pairs for max-flow sampling", samples);
    cmd.AddValue("seed", "Random topology and ECMP seed", seed);
    cmd.AddValue("hostsPerRouter", "tcp: hosts attached to each ToR", hostsPerRouter);
    cmd.AddValue("load", "tcp: offered load as a fraction of host link capacity", load);
    cmd.AddValue("smallBytes", "tcp: size of a small flow", smallBytes);
    cmd.AddValue("largeBytes", "tcp: size of a large flow", largeBytes);
    cmd.AddValue("smallFraction", "tcp: fraction of flows that are small", smallFraction);
    cmd.AddValue("minRtoUs", "tcp: minimum retransmission timeout in microseconds", minRtoUs);
    cmd.AddValue("maxSimMs", "tcp: stop after this much simulated time", maxSimMs);
    cmd.AddValue("spanMs", "tcp: generate flows for this much simulated time, or 0", spanMs);
    cmd.AddValue("pattern", "tcp: traffic pattern (uniform)", pattern);
    cmd.AddValue("flowSizeCdf", "tcp: flow-size CDF file, replacing the two sizes", flowSizeCdf);
    cmd.AddValue("smallThreshold",
                 "tcp: flows up to this many bytes are small; default smallBytes",
                 smallThreshold);
    cmd.AddValue("linkGbps", "tcp: rate of every link", linkGbps);
    cmd.AddValue("linkDelayNs", "tcp: propagation delay of every link", linkDelayNs);
    cmd.AddValue("fabricQueuePackets", "tcp: router transmit queue size", fabricQueuePackets);
    cmd.AddValue("nicQueuePackets", "tcp: host transmit queue size", nicQueuePackets);
    cmd.AddValue("releaseDelayUs",
                 "tcp: free a flow's memory this long after it finishes, or 0 to keep it",
                 releaseDelayUs);
    cmd.AddValue("outputDir", "tcp: write flows.csv, links.csv, and summary.txt here", outputDir);
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
                                                        .spanMs = spanMs,
                                                        .load = load,
                                                        .pattern = pattern,
                                                        .smallBytes = smallBytes,
                                                        .largeBytes = largeBytes,
                                                        .smallFraction = smallFraction,
                                                        .flowSizeCdf = flowSizeCdf,
                                                        .smallThreshold = smallThreshold,
                                                        .linkGbps = linkGbps,
                                                        .linkDelayNs = linkDelayNs,
                                                        .fabricQueuePackets = fabricQueuePackets,
                                                        .nicQueuePackets = nicQueuePackets,
                                                        .minRtoUs = minRtoUs,
                                                        .maxSimMs = maxSimMs,
                                                        .releaseDelayUs = releaseDelayUs,
                                                        .outputDir = outputDir})
                .Run();
        }
        else if (experiment == "reproduction")
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
