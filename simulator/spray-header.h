#ifndef RNG_SPRAY_HEADER_H
#define RNG_SPRAY_HEADER_H

#include "ns3/header.h"

#include <cstdint>
#include <ostream>

namespace rng
{

class SprayHeader : public ns3::Header
{
  public:
    static ns3::TypeId GetTypeId();
    ns3::TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(ns3::Buffer::Iterator i) const override;
    uint32_t Deserialize(ns3::Buffer::Iterator i) override;
    void Print(std::ostream& os) const override;

    void Set(uint32_t source, uint32_t destination, uint32_t flow, uint32_t hops);
    void IncrementHops();
    uint32_t GetDestination() const;
    uint32_t GetFlow() const;
    uint32_t GetHops() const;

  private:
    uint32_t m_source{0};
    uint32_t m_destination{0};
    uint32_t m_flow{0};
    uint32_t m_hops{0};
};

} // namespace rng

#endif // RNG_SPRAY_HEADER_H
