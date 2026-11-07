#include "bypass-experiment.h"
#include "tcp/tcp-options.h"

#include "ns3/core-module.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ns3;

namespace
{

rng::BypassMode
ParseMode(const std::string& name)
{
    if (name == "none")
    {
        return rng::BypassMode::NONE;
    }
    if (name == "adjacent")
    {
        return rng::BypassMode::ADJACENT;
    }
    if (name == "shortest")
    {
        return rng::BypassMode::SHORTEST;
    }
    throw std::runtime_error("unknown bypass " + name + "; use none, adjacent, or shortest");
}

} // namespace

int
main(int argc, char* argv[])
{
    rng::TcpExperimentConfig config;
    std::string bypass = "none";
    CommandLine cmd(__FILE__);
    rng::AddTcpOptions(cmd, config);
    cmd.AddValue("bypass", "Small-flow bypass: none, adjacent, or shortest", bypass);
    cmd.Parse(argc, argv);

    try
    {
        rng::BypassExperiment(config, ParseMode(bypass)).Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
