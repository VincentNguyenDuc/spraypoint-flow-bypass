#include "spraypoint-routing.h"

#include "spraypoint/hash.h"

#include "ns3/abort.h"
#include "ns3/ipv4-route.h"
#include "ns3/net-device.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/packet.h"

#include <ostream>

using namespace ns3;

namespace rng
{

namespace
{

const uint8_t TCP_PROTOCOL = 6;
const uint8_t UDP_PROTOCOL = 17;

// RouteInput sees the packet with its IP header already removed, so a TCP or
// UDP packet starts with its source and destination ports.
uint32_t
FlowHash(Ptr<const Packet> p, const Ipv4Header& header)
{
    uint32_t ports = 0;
    const auto protocol = header.GetProtocol();
    if ((protocol == TCP_PROTOCOL || protocol == UDP_PROTOCOL) && p->GetSize() >= 4)
    {
        uint8_t bytes[4];
        p->CopyData(bytes, 4);
        ports = (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) |
                (uint32_t{bytes[2]} << 8) | bytes[3];
    }
    return static_cast<uint32_t>(
        Key(protocol, header.GetSource().Get(), header.GetDestination().Get(), ports));
}

} // namespace

TypeId
SpraypointRouting::GetTypeId()
{
    static TypeId tid =
        TypeId("rng::SpraypointRouting").SetParent<Ipv4RoutingProtocol>().SetGroupName("Internet");
    return tid;
}

SpraypointRouting::SpraypointRouting(const Spraypoint& tables, uint32_t router)
    : m_tables(tables),
      m_router(router)
{
}

Ipv4Address
SpraypointRouting::HostAddress(uint32_t router, uint32_t host)
{
    NS_ABORT_MSG_IF(router > 0xffff || host > 252, "address plan allows 65536 routers, 253 hosts");
    return Ipv4Address((10U << 24) | (router << 8) | (host + 2));
}

Ipv4Address
SpraypointRouting::GatewayAddress(uint32_t router)
{
    return Ipv4Address((10U << 24) | (router << 8) | 1);
}

uint32_t
SpraypointRouting::RouterOf(Ipv4Address address)
{
    return (address.Get() >> 8) & 0xffff;
}

void
SpraypointRouting::AddFabricPort(uint32_t neighbor, Ptr<NetDevice> device)
{
    m_fabricPorts[neighbor].device = device;
    if (m_neighborOfPort.size() <= device->GetIfIndex())
    {
        m_neighborOfPort.resize(device->GetIfIndex() + 1, 0);
    }
    m_neighborOfPort[device->GetIfIndex()] = neighbor;
}

void
SpraypointRouting::AddHostPort(Ipv4Address host, Ptr<NetDevice> device)
{
    m_hostPorts[host.Get()] = device;
    if (m_hostFacing.size() <= device->GetIfIndex())
    {
        m_hostFacing.resize(device->GetIfIndex() + 1, false);
    }
    m_hostFacing[device->GetIfIndex()] = true;
}

const SpraypointRouting::LinkCounters&
SpraypointRouting::GetArrivals(uint32_t neighbor) const
{
    return m_fabricPorts.at(neighbor).arrivals;
}

uint32_t
SpraypointRouting::SelectNextHop(uint32_t destinationRouter,
                                 uint32_t flow,
                                 bool,
                                 bool fromHost) const
{
    return fromHost ? m_tables.SprayHop(m_router, destinationRouter, flow)
                    : m_tables.PointHop(m_router, destinationRouter, flow);
}

const Spraypoint&
SpraypointRouting::GetTables() const
{
    return m_tables;
}

uint32_t
SpraypointRouting::GetRouter() const
{
    return m_router;
}

Ptr<Ipv4Route>
SpraypointRouting::RouteOutput(Ptr<Packet>,
                               const Ipv4Header&,
                               Ptr<NetDevice>,
                               Socket::SocketErrno& sockerr)
{
    // Routers have no IP addresses and originate nothing.  The only caller is
    // ICMP reporting an expired TTL, which drops the message on a null route.
    sockerr = Socket::ERROR_NOROUTETOHOST;
    return nullptr;
}

bool
SpraypointRouting::RouteInput(Ptr<const Packet> p,
                              const Ipv4Header& header,
                              Ptr<const NetDevice> idev,
                              const UnicastForwardCallback& ucb,
                              const MulticastForwardCallback&,
                              const LocalDeliverCallback&,
                              const ErrorCallback&)
{
    const auto destination = header.GetDestination();
    const auto destinationRouter = RouterOf(destination);
    if ((destination.Get() >> 24) != 10 || destinationRouter >= m_tables.GetGraph().size())
    {
        return false;
    }

    const auto ifIndex = idev->GetIfIndex();
    const bool fromHost = ifIndex < m_hostFacing.size() && m_hostFacing[ifIndex];
    const bool small = (header.GetTos() & 0xfc) == SMALL_FLOW_TOS;
    const uint64_t bytes = p->GetSize() + header.GetSerializedSize();

    Ptr<NetDevice> port;
    if (destinationRouter == m_router)
    {
        const auto host = m_hostPorts.find(destination.Get());
        if (host == m_hostPorts.end())
        {
            return false;
        }
        port = host->second;
        if (!fromHost)
        {
            auto& arrivals = m_fabricPorts.at(m_neighborOfPort[ifIndex]).arrivals;
            (small ? arrivals.deliveredSmall : arrivals.deliveredLarge) += bytes;
        }
    }
    else
    {
        const auto next = SelectNextHop(destinationRouter, FlowHash(p, header), small, fromHost);
        const auto fabric = m_fabricPorts.find(next);
        NS_ABORT_MSG_IF(fabric == m_fabricPorts.end(),
                        "router " << m_router << " routed to non-neighbor " << next);
        port = fabric->second.device;
    }

    auto route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetSource(header.GetSource());
    route->SetGateway(Ipv4Address::GetAny());
    route->SetOutputDevice(port);
    ucb(route, p, header);
    return true;
}

void
SpraypointRouting::NotifyInterfaceUp(uint32_t)
{
}

void
SpraypointRouting::NotifyInterfaceDown(uint32_t)
{
}

void
SpraypointRouting::NotifyAddAddress(uint32_t, Ipv4InterfaceAddress)
{
}

void
SpraypointRouting::NotifyRemoveAddress(uint32_t, Ipv4InterfaceAddress)
{
}

void
SpraypointRouting::SetIpv4(Ptr<Ipv4>)
{
}

void
SpraypointRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit) const
{
    *stream->GetStream() << "SpraypointRouting router=" << m_router
                         << " fabric-ports=" << m_fabricPorts.size()
                         << " host-ports=" << m_hostPorts.size() << "\n";
}

void
SpraypointRouting::DoDispose()
{
    m_fabricPorts.clear();
    m_hostPorts.clear();
    Ipv4RoutingProtocol::DoDispose();
}

} // namespace rng
