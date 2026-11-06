#ifndef RNG_TCP_EXPERIMENT_H
#define RNG_TCP_EXPERIMENT_H

#include "spraypoint.h"

#include "ns3/address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <vector>

namespace rng
{

struct TcpExperimentConfig
{
    uint32_t nodes;
    uint32_t degree;
    uint32_t waypoints;
    uint32_t nextHops;
    uint64_t seed;
    uint32_t hostsPerRouter;
    uint32_t flows;
    double load; // offered load as a fraction of total host link capacity
    uint32_t smallBytes;
    uint32_t largeBytes;
    double smallFraction; // fraction of flows, not of bytes, that are small
    uint32_t minRtoUs;
    uint32_t maxSimMs;
};

// Runs TCP flows between hosts over a Spraypoint fabric and prints flow
// completion times, realized path lengths, queue depths, and drops.
class TcpExperiment
{
  public:
    explicit TcpExperiment(const TcpExperimentConfig& config);

    void Run();

  private:
    enum PortKind
    {
        FABRIC,
        DOWNLINK, // router to host
        HOST_NIC, // host to router
        PORT_KINDS
    };

    struct Flow
    {
        uint32_t sourceHost;
        uint32_t destinationHost;
        uint32_t bytes;
        ns3::Time start;
        uint32_t received{0};
        ns3::Time finish;
    };

    void ConfigureTcp() const;
    void BuildNetwork();
    void ConnectPort(ns3::Ptr<ns3::NetDevice> device, PortKind kind);
    void ConnectNodeTraces();
    void ScheduleFlows();
    uint32_t RouterOfHost(uint32_t host) const;
    void OnSinkRx(uint32_t flow,
                  ns3::Ptr<const ns3::Packet> packet,
                  const ns3::Address&,
                  const ns3::Address&);
    void OnHostDeliver(const ns3::Ipv4Header& header, ns3::Ptr<const ns3::Packet>, uint32_t);
    void OnQueueLength(PortKind kind, uint32_t, uint32_t length);
    void OnTcDrop(bool atRouter, ns3::Ptr<const ns3::Packet>);
    void OnDeviceDrop(ns3::Ptr<const ns3::Packet>);
    void OnIpDrop(const ns3::Ipv4Header&,
                  ns3::Ptr<const ns3::Packet>,
                  ns3::Ipv4L3Protocol::DropReason reason,
                  ns3::Ptr<ns3::Ipv4>,
                  uint32_t);
    void PrintResults() const;

    TcpExperimentConfig m_config;
    Spraypoint m_tables;
    ns3::NodeContainer m_routers;
    ns3::NodeContainer m_hosts;
    std::vector<Flow> m_flows;
    std::vector<uint32_t> m_nextPort; // by destination host
    uint32_t m_completed{0};
    std::vector<uint64_t> m_pathLengths;
    uint32_t m_maxQueue[PORT_KINDS]{};
    uint64_t m_routerDrops{0};
    uint64_t m_hostDrops{0};
    uint64_t m_deviceDrops{0};
    uint64_t m_ttlDrops{0};
    uint64_t m_noRouteDrops{0};
    uint64_t m_otherIpDrops{0};
};

} // namespace rng

#endif // RNG_TCP_EXPERIMENT_H
