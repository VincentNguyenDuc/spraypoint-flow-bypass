#include "tcp-options.h"

namespace rng
{

void
AddTcpOptions(ns3::CommandLine& cmd, TcpExperimentConfig& config)
{
    cmd.AddValue("nodes", "Number of ToR routers (paper default: 1000)", config.nodes);
    cmd.AddValue("degree", "Fabric degree per ToR (paper default: 64)", config.degree);
    cmd.AddValue("waypoints",
                 "p: selected waypoint neighbors (paper default: 4)",
                 config.waypoints);
    cmd.AddValue("nextHops", "h: pointing ECMP next hops (paper default: 2)", config.nextHops);
    cmd.AddValue("flows", "Most TCP flows to generate, or 0 for no limit", config.flows);
    cmd.AddValue("seed", "Random topology and ECMP seed", config.seed);
    cmd.AddValue("hostsPerRouter", "Hosts attached to each ToR", config.hostsPerRouter);
    cmd.AddValue("load", "Offered load as a fraction of host link capacity", config.load);
    cmd.AddValue("smallBytes", "Size of a small flow", config.smallBytes);
    cmd.AddValue("largeBytes", "Size of a large flow", config.largeBytes);
    cmd.AddValue("smallFraction", "Fraction of flows that are small", config.smallFraction);
    cmd.AddValue("minRtoUs", "Minimum retransmission timeout in microseconds", config.minRtoUs);
    cmd.AddValue("maxSimMs", "Stop after this much simulated time", config.maxSimMs);
    cmd.AddValue("spanMs", "Generate flows for this much simulated time, or 0", config.spanMs);
    cmd.AddValue("pattern",
                 "Traffic pattern: uniform, or permutation (each router to one partner)",
                 config.pattern);
    cmd.AddValue("flowSizeCdf", "Flow-size CDF file, replacing the two sizes", config.flowSizeCdf);
    cmd.AddValue("smallThreshold",
                 "Flows up to this many bytes are small; default smallBytes",
                 config.smallThreshold);
    cmd.AddValue("linkGbps", "Rate of every link", config.linkGbps);
    cmd.AddValue("linkDelayNs", "Propagation delay of every link", config.linkDelayNs);
    cmd.AddValue("fabricQueuePackets", "Router transmit queue size", config.fabricQueuePackets);
    cmd.AddValue("nicQueuePackets", "Host transmit queue size", config.nicQueuePackets);
    cmd.AddValue("releaseDelayUs",
                 "Free a flow's memory this long after it finishes, or 0 to keep it",
                 config.releaseDelayUs);
    cmd.AddValue("outputDir", "Write flows.csv, links.csv, and summary.txt here", config.outputDir);
}

} // namespace rng
