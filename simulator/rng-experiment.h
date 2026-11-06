#ifndef RNG_EXPERIMENT_H
#define RNG_EXPERIMENT_H

#include "spray-header.h"
#include "spraypoint.h"

#include "ns3/address.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rng
{

// Builds the ns-3 fabric for a Spraypoint topology, sends sample traffic, and
// prints the metrics parsed by scripts/run_rng_experiments.py.
class RngExperiment
{
  public:
    RngExperiment(uint32_t nodes,
                  uint32_t degree,
                  uint32_t waypoints,
                  uint32_t nextHops,
                  uint32_t flows,
                  uint32_t samples,
                  uint64_t seed);

    void Run();

  private:
    void BuildNs3Fabric();
    void ScheduleFlows();
    void SendInitial(uint32_t source, uint32_t destination, uint32_t flow);
    void Send(uint32_t from, uint32_t to, const SprayHeader& header);
    bool Receive(uint32_t current,
                 ns3::Ptr<ns3::NetDevice>,
                 ns3::Ptr<const ns3::Packet> received,
                 uint16_t,
                 const ns3::Address&);
    static double Percentile(std::vector<uint64_t> values, double quantile);
    void PrintResults();

    uint32_t m_nodes;
    uint32_t m_degree;
    uint32_t m_flows;
    uint32_t m_samples;
    uint64_t m_seed;
    Spraypoint m_routing;
    ns3::NodeContainer m_routers;
    std::vector<std::unordered_map<uint32_t, ns3::Ptr<ns3::NetDevice>>> m_devices;
    std::vector<ns3::Time> m_started;
    std::vector<uint32_t> m_hops;
    std::vector<uint64_t> m_latencies;
    uint32_t m_droppedForHopLimit{0};
};

} // namespace rng

#endif // RNG_EXPERIMENT_H
