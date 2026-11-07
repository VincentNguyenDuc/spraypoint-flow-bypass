#include "bypass-experiment.h"

#include "ns3/object.h"

using namespace ns3;

namespace rng
{

BypassExperiment::BypassExperiment(const TcpExperimentConfig& config, BypassMode mode)
    : TcpExperiment(config),
      m_mode(mode),
      m_paths(GetTables().GetGraph(), config.seed)
{
}

Ptr<SpraypointRouting>
BypassExperiment::CreateRouting(uint32_t router)
{
    return CreateObject<BypassRouting>(GetTables(), router, m_mode, m_paths);
}

} // namespace rng
