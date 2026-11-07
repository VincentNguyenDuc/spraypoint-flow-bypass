#ifndef RNG_TCP_OPTIONS_H
#define RNG_TCP_OPTIONS_H

#include "tcp-experiment.h"

#include "ns3/command-line.h"

namespace rng
{

// Adds the options shared by every program built on TcpExperiment.  Each option's
// default is its value in config, and config must outlive the command line.
void AddTcpOptions(ns3::CommandLine& cmd, TcpExperimentConfig& config);

} // namespace rng

#endif // RNG_TCP_OPTIONS_H
