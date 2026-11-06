#ifndef RNG_SPRAYPOINT_ROUTING_H
#define RNG_SPRAYPOINT_ROUTING_H

#include "spraypoint.h"

#include "ns3/ipv4-routing-protocol.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rng
{

// Spraypoint as an ns-3 IPv4 routing protocol for one ToR router, so that IP
// and TCP run over the fabric.  Like the paper's two VRFs, the table is picked
// by ingress port: packets from a host are sprayed, and packets from the fabric
// follow the pointing table.  Next hops hash the 5-tuple, so all packets of a
// flow share one path and TCP sees no reordering from the routing.
//
// A host's address encodes its router as 10.<router high>.<router low>.<host+2>,
// so the destination router comes from the address and no routes are exchanged.
class SpraypointRouting : public ns3::Ipv4RoutingProtocol
{
  public:
    static ns3::TypeId GetTypeId();

    // tables must outlive this object; all routers share one copy.
    SpraypointRouting(const Spraypoint& tables, uint32_t router);

    static ns3::Ipv4Address HostAddress(uint32_t router, uint32_t host);
    static ns3::Ipv4Address GatewayAddress(uint32_t router);
    static uint32_t RouterOf(ns3::Ipv4Address address);

    void AddFabricPort(uint32_t neighbor, ns3::Ptr<ns3::NetDevice> device);
    void AddHostPort(ns3::Ipv4Address host, ns3::Ptr<ns3::NetDevice> device);

    ns3::Ptr<ns3::Ipv4Route> RouteOutput(ns3::Ptr<ns3::Packet> p,
                                         const ns3::Ipv4Header& header,
                                         ns3::Ptr<ns3::NetDevice> oif,
                                         ns3::Socket::SocketErrno& sockerr) override;
    bool RouteInput(ns3::Ptr<const ns3::Packet> p,
                    const ns3::Ipv4Header& header,
                    ns3::Ptr<const ns3::NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;
    void NotifyInterfaceUp(uint32_t interface) override;
    void NotifyInterfaceDown(uint32_t interface) override;
    void NotifyAddAddress(uint32_t interface, ns3::Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t interface, ns3::Ipv4InterfaceAddress address) override;
    void SetIpv4(ns3::Ptr<ns3::Ipv4> ipv4) override;
    void PrintRoutingTable(ns3::Ptr<ns3::OutputStreamWrapper> stream,
                           ns3::Time::Unit unit = ns3::Time::S) const override;

  private:
    void DoDispose() override;

    const Spraypoint& m_tables;
    uint32_t m_router;
    std::unordered_map<uint32_t, ns3::Ptr<ns3::NetDevice>> m_fabricPorts; // by neighbor router
    std::unordered_map<uint32_t, ns3::Ptr<ns3::NetDevice>> m_hostPorts;   // by host IPv4 address
    std::vector<bool> m_hostFacing;                                       // by device ifIndex
};

} // namespace rng

#endif // RNG_SPRAYPOINT_ROUTING_H
