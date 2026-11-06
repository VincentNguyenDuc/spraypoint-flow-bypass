#ifndef RNG_BYPASS_ROUTING_H
#define RNG_BYPASS_ROUTING_H

#include "shortest-paths.h"
#include "tcp/spraypoint-routing.h"

namespace rng
{

enum class BypassMode
{
    NONE,     // every packet follows Spraypoint
    ADJACENT, // small flows take the direct link between neighboring routers
    SHORTEST, // small flows take shortest paths between all routers
};

// Spraypoint routing that lets small flows skip the spray step.  Unmarked packets
// always follow Spraypoint.
class BypassRouting : public SpraypointRouting
{
  public:
    static ns3::TypeId GetTypeId();

    // tables and paths must outlive this object; all routers share one copy.
    BypassRouting(const Spraypoint& tables,
                  uint32_t router,
                  BypassMode mode,
                  const ShortestPaths& paths);

  protected:
    uint32_t SelectNextHop(uint32_t destinationRouter,
                           uint32_t flow,
                           bool small,
                           bool fromHost) const override;

  private:
    BypassMode m_mode;
    const ShortestPaths& m_paths;
};

} // namespace rng

#endif // RNG_BYPASS_ROUTING_H
