#include "rng-experiment.h"

#include "ns3/core-module.h"

#include <cstdint>
#include <exception>
#include <iostream>

using namespace ns3;

int
main(int argc, char* argv[])
{
    uint32_t nodes = 1000;
    uint32_t degree = 64;
    uint32_t waypoints = 4;
    uint32_t nextHops = 2;
    uint32_t flows = 20000;
    uint32_t samples = 100;
    uint64_t seed = 561;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nodes", "Number of ToR routers (paper default: 1000)", nodes);
    cmd.AddValue("degree", "Fabric degree per ToR (paper default: 64)", degree);
    cmd.AddValue("waypoints", "p: selected waypoint neighbors (paper default: 4)", waypoints);
    cmd.AddValue("nextHops", "h: pointing ECMP next hops (paper default: 2)", nextHops);
    cmd.AddValue("flows", "Single-packet flows used to sample ns-3 path lengths", flows);
    cmd.AddValue("samples", "Source/destination pairs used for max-flow sampling", samples);
    cmd.AddValue("seed", "Random topology and ECMP seed", seed);
    cmd.Parse(argc, argv);

    try
    {
        rng::RngExperiment experiment(nodes, degree, waypoints, nextHops, flows, samples, seed);
        experiment.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
