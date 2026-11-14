#ifndef RNG_EGRESS_PORT_TAG_H
#define RNG_EGRESS_PORT_TAG_H

#include "ns3/tag.h"

namespace rng
{

// Simulator-only metadata: the router and output device selected for this hop.
class EgressPortTag : public ns3::Tag
{
  public:
    static ns3::TypeId GetTypeId()
    {
        static ns3::TypeId id = ns3::TypeId("rng::EgressPortTag")
                                    .SetParent<ns3::Tag>()
                                    .AddConstructor<EgressPortTag>();
        return id;
    }
    ns3::TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override { return 8; }
    void Serialize(ns3::TagBuffer buffer) const override
    {
        buffer.WriteU32(router);
        buffer.WriteU32(device);
    }
    void Deserialize(ns3::TagBuffer buffer) override
    {
        router = buffer.ReadU32();
        device = buffer.ReadU32();
    }
    void Print(std::ostream& out) const override { out << router << ":" << device; }

    uint32_t router{0};
    uint32_t device{0};
};

} // namespace rng
#endif
