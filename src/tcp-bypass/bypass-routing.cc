#include "bypass-routing.h"

using namespace ns3;

namespace rng
{

TypeId
BypassRouting::GetTypeId()
{
    static TypeId tid =
        TypeId("rng::BypassRouting").SetParent<SpraypointRouting>().SetGroupName("Internet");
    return tid;
}

BypassRouting::BypassRouting(const Spraypoint& tables,
                             uint32_t router,
                             BypassMode mode,
                             const ShortestPaths& paths)
    : SpraypointRouting(tables, router),
      m_mode(mode),
      m_paths(paths)
{
}

uint32_t
BypassRouting::SelectNextHop(uint32_t destinationRouter,
                             uint32_t flow,
                             bool small,
                             bool fromHost) const
{
    if (small && m_mode == BypassMode::SHORTEST)
    {
        return m_paths.NextHop(GetRouter(), destinationRouter, flow);
    }
    if (small && m_mode == BypassMode::ADJACENT && fromHost &&
        GetTables().Adjacent(GetRouter(), destinationRouter))
    {
        return destinationRouter;
    }
    return SpraypointRouting::SelectNextHop(destinationRouter, flow, small, fromHost);
}

} // namespace rng
