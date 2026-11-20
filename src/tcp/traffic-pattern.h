#ifndef RNG_TRAFFIC_PATTERN_H
#define RNG_TRAFFIC_PATTERN_H

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace rng
{

struct TrafficPatternConfig
{
    std::string pattern{"uniform"};
    uint32_t permutationShift{1}; // destination = (source + shift) modulo host count
    uint32_t hotspotHosts{1};     // hotspot destinations are host IDs [0, hotspotHosts)
    double hotspotFraction{0.9};  // probability that a flow's destination is in that set
    uint32_t incastDestination{0};
    uint32_t incastFanIn{0}; // next N cyclic host IDs after destination; 0 means all others
    uint32_t routers{0};    // router-permutation divides the hosts evenly over these routers
    uint64_t seed{0};       // topology seed used to choose router partners
};

struct TrafficPair
{
    uint32_t sourceHost;
    uint32_t destinationHost;
};

// Pure endpoint selection, with no ns-3 or routing-mode dependency. Construction
// consumes no random draws. Each call selects one flow and never returns a self-flow.
class TrafficPattern
{
  public:
    TrafficPattern(uint32_t hosts, const TrafficPatternConfig& config);

    // Uniform preserves the original source draw followed by destination draws
    // until they differ. Keep the caller's gap/endpoint/size draw order unchanged.
    TrafficPair Select(std::mt19937_64& random);

    // Upper bound from reachable destinations and the listener-port budget.
    // Random concentration can exhaust a destination earlier. Use for preflight
    // only when an exact flow count is required (not a cap combined with spanMs).
    uint64_t MaximumFlows() const;

  private:
    enum class Kind
    {
        UNIFORM,
        PERMUTATION,
        ROUTER_PERMUTATION,
        HOTSPOT,
        INCAST,
    };

    uint32_t m_hosts;
    TrafficPatternConfig m_config;
    Kind m_kind{Kind::UNIFORM};
    uint32_t m_incastFanIn{0};
    uint32_t m_hostsPerRouter{0};
    std::vector<uint32_t> m_routerPartners;
    std::uniform_int_distribution<uint32_t> m_pickHost;
};

// Check the current experiment's IPv4 address-plan limits (65536 routers,
// 253 hosts/router) and return their product without uint32_t overflow.
uint32_t CheckedTrafficHostCount(uint32_t routers, uint32_t hostsPerRouter);

// All patterns use load * total host egress capacity / mean flow bytes. This
// deliberately does not renormalize by hot destinations or incast sender count.
double TrafficArrivalRate(uint32_t hosts, double linkGbps, double load, double meanBytes);

// One listener per generated flow, retaining the legacy range 10000..65534.
// Ports are never recycled, even if the flow would finish during the workload.
class TrafficListenerPorts
{
  public:
    static constexpr uint32_t FIRST_PORT = 10000;
    static constexpr uint32_t LAST_PORT = 65534;
    static constexpr uint32_t PORTS_PER_DESTINATION = LAST_PORT - FIRST_PORT + 1;

    explicit TrafficListenerPorts(uint32_t hosts);
    uint16_t Allocate(uint32_t destinationHost);

  private:
    std::vector<uint32_t> m_nextPort;
};

} // namespace rng

#endif // RNG_TRAFFIC_PATTERN_H
