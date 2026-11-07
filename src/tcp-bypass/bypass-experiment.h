#ifndef RNG_BYPASS_EXPERIMENT_H
#define RNG_BYPASS_EXPERIMENT_H

#include "bypass-routing.h"
#include "shortest-paths.h"
#include "tcp/tcp-experiment.h"

namespace rng
{

// The TCP experiment with a small-flow bypass on every router.  With the same
// seed and options, every mode runs the same flows over the same topology.
class BypassExperiment : public TcpExperiment
{
  public:
    BypassExperiment(const TcpExperimentConfig& config, BypassMode mode);

  protected:
    ns3::Ptr<SpraypointRouting> CreateRouting(uint32_t router) override;

  private:
    BypassMode m_mode;
    ShortestPaths m_paths;
};

} // namespace rng

#endif // RNG_BYPASS_EXPERIMENT_H
