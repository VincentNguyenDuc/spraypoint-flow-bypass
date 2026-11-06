#include "tcp-experiment.h"
#include "tcp-options.h"

#include "ns3/core-module.h"

#include <exception>
#include <iostream>

using namespace ns3;

int
main(int argc, char* argv[])
{
    rng::TcpExperimentConfig config;
    CommandLine cmd(__FILE__);
    rng::AddTcpOptions(cmd, config);
    cmd.Parse(argc, argv);

    try
    {
        rng::TcpExperiment(config).Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
