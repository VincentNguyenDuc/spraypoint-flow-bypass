#ifndef RNG_TCP_EXPERIMENT_H
#define RNG_TCP_EXPERIMENT_H

#include "flow-size-cdf.h"
#include "spraypoint-routing.h"
#include "spraypoint/spraypoint.h"

#include "ns3/address.h"
#include "ns3/bulk-send-application.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/packet-sink.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
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
    uint32_t flows; // stop generating after this many flows; 0 means no limit
    double spanMs;  // stop generating after this much simulated time; 0 means no limit
    double load;    // offered load as a fraction of total host link capacity
    std::string pattern;
    uint32_t smallBytes;
    uint32_t largeBytes;
    double smallFraction;    // fraction of flows, not of bytes, that are small
    std::string flowSizeCdf; // replaces the two sizes above when set
    uint32_t smallThreshold; // flows up to this size are small; 0 means smallBytes
    double linkGbps;
    uint32_t linkDelayNs;
    uint32_t fabricQueuePackets; // also used for router-to-host links
    uint32_t nicQueuePackets;
    uint32_t minRtoUs;
    uint32_t maxSimMs;
    uint32_t releaseDelayUs; // free a flow's memory this long after it finishes; 0 keeps it
    std::string outputDir;   // write flows.csv, links.csv, and summary.txt here when set
};

// Runs TCP flows between hosts over a Spraypoint fabric and reports flow
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

    enum PathClass
    {
        SMALL_ADJACENT,
        SMALL_OTHER,
        LARGE,
        PATH_CLASSES
    };

    struct Flow
    {
        uint32_t sourceHost;
        uint32_t destinationHost;
        uint32_t bytes;
        uint16_t port;
        bool small;
        ns3::Time start;
        uint32_t received{0};
        ns3::Time finish;
    };

    // The applications of a flow that has started and has not been released.
    struct ActiveFlow
    {
        ns3::Ptr<ns3::PacketSink> sink;
        ns3::Ptr<ns3::BulkSendApplication> source;
    };

    // One device's transmit queue.
    struct Port
    {
        uint32_t from; // router or host index, by kind
        uint32_t to;
        PortKind kind;
        uint32_t length{0};
        uint32_t maxLength{0};
        ns3::Time lastChange;
        double packetSeconds{0}; // queue length integrated over time
    };

    void ConfigureTcp() const;
    void BuildNetwork();
    void ConnectPort(ns3::Ptr<ns3::NetDevice> device, PortKind kind, uint32_t from, uint32_t to);
    void ConnectNodeTraces();
    void GenerateFlows();
    void StartFlow(uint32_t flow);
    void MarkListener(uint32_t flow) const;
    void ReleaseFlow(uint32_t flow);
    uint32_t RouterOfHost(uint32_t host) const;
    bool IsAdjacentFlow(const Flow& flow) const;
    void OnSinkRx(uint32_t flow,
                  ns3::Ptr<const ns3::Packet> packet,
                  const ns3::Address&,
                  const ns3::Address&);
    void OnHostDeliver(const ns3::Ipv4Header& header, ns3::Ptr<const ns3::Packet>, uint32_t);
    void OnQueueLength(uint32_t port, uint32_t, uint32_t length);
    void OnTcDrop(bool atRouter, ns3::Ptr<const ns3::Packet>);
    void OnDeviceDrop(ns3::Ptr<const ns3::Packet>);
    void OnIpDrop(const ns3::Ipv4Header&,
                  ns3::Ptr<const ns3::Packet>,
                  ns3::Ipv4L3Protocol::DropReason reason,
                  ns3::Ptr<ns3::Ipv4>,
                  uint32_t);
    void PrintResults(std::ostream& out) const;
    void WriteOutputs() const;
    void WriteFlows(const std::string& path) const;
    void WriteLinks(const std::string& path) const;

    TcpExperimentConfig m_config;
    Spraypoint m_tables;
    std::optional<FlowSizeCdf> m_flowSizes;
    uint32_t m_smallThreshold;
    ns3::NodeContainer m_routers;
    ns3::NodeContainer m_hosts;
    std::vector<ns3::Ptr<SpraypointRouting>> m_routing; // by router
    std::vector<Flow> m_flows;
    std::unordered_map<uint32_t, ActiveFlow> m_active; // by flow
    uint32_t m_completed{0};
    std::vector<uint64_t> m_pathLengths[PATH_CLASSES];
    std::vector<Port> m_ports;
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
