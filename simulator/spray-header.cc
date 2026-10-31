#include "spray-header.h"

using namespace ns3;

namespace rng
{

NS_OBJECT_ENSURE_REGISTERED(SprayHeader);

TypeId
SprayHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SprayHeader")
                            .SetParent<Header>()
                            .SetGroupName("Network")
                            .AddConstructor<SprayHeader>();
    return tid;
}

TypeId
SprayHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
SprayHeader::GetSerializedSize() const
{
    return 16;
}

void
SprayHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteHtonU32(m_source);
    i.WriteHtonU32(m_destination);
    i.WriteHtonU32(m_flow);
    i.WriteHtonU32(m_hops);
}

uint32_t
SprayHeader::Deserialize(Buffer::Iterator i)
{
    m_source = i.ReadNtohU32();
    m_destination = i.ReadNtohU32();
    m_flow = i.ReadNtohU32();
    m_hops = i.ReadNtohU32();
    return GetSerializedSize();
}

void
SprayHeader::Print(std::ostream& os) const
{
    os << m_source << "->" << m_destination << " flow=" << m_flow << " hops=" << m_hops;
}

void
SprayHeader::Set(uint32_t source, uint32_t destination, uint32_t flow, uint32_t hops)
{
    m_source = source;
    m_destination = destination;
    m_flow = flow;
    m_hops = hops;
}

void
SprayHeader::IncrementHops()
{
    ++m_hops;
}

uint32_t
SprayHeader::GetDestination() const
{
    return m_destination;
}

uint32_t
SprayHeader::GetFlow() const
{
    return m_flow;
}

uint32_t
SprayHeader::GetHops() const
{
    return m_hops;
}

} // namespace rng
