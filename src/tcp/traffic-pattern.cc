#include "traffic-pattern.h"

#include "spraypoint/matching.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace rng
{

TrafficPattern::TrafficPattern(uint32_t hosts, const TrafficPatternConfig& config)
    : m_hosts(hosts),
      m_config(config),
      m_pickHost(0, hosts == 0 ? 0 : hosts - 1)
{
    if (hosts < 2)
    {
        throw std::invalid_argument("traffic patterns need at least two hosts");
    }
    if (config.pattern == "uniform")
    {
        m_kind = Kind::UNIFORM;
    }
    else if (config.pattern == "permutation")
    {
        m_kind = Kind::PERMUTATION;
        if (config.permutationShift == 0 || config.permutationShift >= hosts)
        {
            throw std::invalid_argument("permutationShift must be between 1 and hosts - 1");
        }
    }
    else if (config.pattern == "router-permutation")
    {
        if (config.routers < 2 || config.routers > hosts || hosts % config.routers != 0)
        {
            throw std::invalid_argument("router-permutation needs at least two routers with an "
                                        "equal, positive number of hosts per router");
        }
        m_kind = Kind::ROUTER_PERMUTATION;
        m_hostsPerRouter = hosts / config.routers;
        // Use matching 0 of bypass-model without consuming any workload RNG draws.
        m_routerPartners = RandomMatching(config.routers, config.seed + 1000);
    }
    else if (config.pattern == "hotspot")
    {
        m_kind = Kind::HOTSPOT;
        if (config.hotspotHosts == 0 || config.hotspotHosts >= hosts)
        {
            throw std::invalid_argument("hotspotHosts must be between 1 and hosts - 1");
        }
        if (!std::isfinite(config.hotspotFraction) || config.hotspotFraction < 0 ||
            config.hotspotFraction > 1)
        {
            throw std::invalid_argument("hotspotFraction must be finite and between 0 and 1");
        }
    }
    else if (config.pattern == "incast")
    {
        m_kind = Kind::INCAST;
        if (config.incastDestination >= hosts)
        {
            throw std::invalid_argument("incastDestination must be less than hosts");
        }
        if (config.incastFanIn >= hosts)
        {
            throw std::invalid_argument(
                "incastFanIn must be 0 (all other hosts) or less than hosts");
        }
        m_incastFanIn = config.incastFanIn == 0 ? hosts - 1 : config.incastFanIn;
    }
    else
    {
        throw std::invalid_argument("unknown pattern " + config.pattern +
                                    "; use uniform, permutation, router-permutation, "
                                    "hotspot, or incast");
    }
}

TrafficPair
TrafficPattern::Select(std::mt19937_64& random)
{
    if (m_kind == Kind::UNIFORM)
    {
        TrafficPair pair;
        pair.sourceHost = m_pickHost(random);
        do
        {
            pair.destinationHost = m_pickHost(random);
        } while (pair.destinationHost == pair.sourceHost);
        return pair;
    }
    if (m_kind == Kind::PERMUTATION)
    {
        const auto source = m_pickHost(random);
        const auto destination = static_cast<uint32_t>(
            (uint64_t{source} + m_config.permutationShift) % m_hosts);
        return {source, destination};
    }
    if (m_kind == Kind::ROUTER_PERMUTATION)
    {
        const auto source = m_pickHost(random);
        const auto partner = m_routerPartners[source / m_hostsPerRouter];
        std::uniform_int_distribution<uint32_t> pickLocalHost(0, m_hostsPerRouter - 1);
        return {source, partner * m_hostsPerRouter + pickLocalHost(random)};
    }
    if (m_kind == Kind::HOTSPOT)
    {
        std::uniform_real_distribution<double> fraction(0.0, 1.0);
        const bool hot = fraction(random) < m_config.hotspotFraction;
        std::uniform_int_distribution<uint32_t> pickDestination(
            hot ? 0 : m_config.hotspotHosts,
            hot ? m_config.hotspotHosts - 1 : m_hosts - 1);
        const auto destination = pickDestination(random);
        std::uniform_int_distribution<uint32_t> pickSource(0, m_hosts - 2);
        auto source = pickSource(random);
        if (source >= destination)
        {
            ++source;
        }
        return {source, destination};
    }

    std::uniform_int_distribution<uint32_t> pickSender(1, m_incastFanIn);
    const auto source = static_cast<uint32_t>(
        (uint64_t{m_config.incastDestination} + pickSender(random)) % m_hosts);
    return {source, m_config.incastDestination};
}

uint64_t
TrafficPattern::MaximumFlows() const
{
    uint32_t destinations = m_hosts;
    if (m_kind == Kind::INCAST)
    {
        destinations = 1;
    }
    else if (m_kind == Kind::HOTSPOT && m_config.hotspotFraction == 1)
    {
        destinations = m_config.hotspotHosts;
    }
    else if (m_kind == Kind::HOTSPOT && m_config.hotspotFraction == 0)
    {
        destinations = m_hosts - m_config.hotspotHosts;
    }
    return uint64_t{destinations} * TrafficListenerPorts::PORTS_PER_DESTINATION;
}

uint32_t
CheckedTrafficHostCount(uint32_t routers, uint32_t hostsPerRouter)
{
    const auto hosts = uint64_t{routers} * hostsPerRouter;
    if (hosts < 2 || hosts > std::numeric_limits<uint32_t>::max())
    {
        throw std::invalid_argument("host count must be between 2 and UINT32_MAX");
    }
    if (routers > 65536 || hostsPerRouter > 253)
    {
        throw std::invalid_argument(
            "address plan allows at most 65536 routers and 253 hosts per router");
    }
    return static_cast<uint32_t>(hosts);
}

double
TrafficArrivalRate(uint32_t hosts, double linkGbps, double load, double meanBytes)
{
    if (hosts < 2 || !std::isfinite(linkGbps) || linkGbps <= 0 || !std::isfinite(load) ||
        load <= 0 || !std::isfinite(meanBytes) || meanBytes <= 0)
    {
        throw std::invalid_argument("arrival rate needs at least two hosts and finite, positive "
                                    "linkGbps, load, and mean flow bytes");
    }
    // Preserve the legacy arithmetic order as well as its normalization.
    const double capacityBytesPerSecond = hosts * linkGbps * 1e9 / 8;
    const double arrivalsPerSecond = load * capacityBytesPerSecond / meanBytes;
    if (!std::isfinite(arrivalsPerSecond) || arrivalsPerSecond <= 0)
    {
        throw std::invalid_argument("derived flow arrival rate must be finite and positive");
    }
    return arrivalsPerSecond;
}

TrafficListenerPorts::TrafficListenerPorts(uint32_t hosts)
{
    if (hosts < 2)
    {
        throw std::invalid_argument("listener allocation needs at least two hosts");
    }
    m_nextPort.assign(hosts, FIRST_PORT);
}

uint16_t
TrafficListenerPorts::Allocate(uint32_t destinationHost)
{
    if (destinationHost >= m_nextPort.size())
    {
        throw std::out_of_range("listener destination host is outside the host range");
    }
    auto& next = m_nextPort[destinationHost];
    if (next > LAST_PORT)
    {
        throw std::length_error("destination host " + std::to_string(destinationHost) +
                                " exhausted its " + std::to_string(PORTS_PER_DESTINATION) +
                                " listener ports (10000..65534); reduce flows/spanMs or "
                                "distribute traffic across more destinations");
    }
    return static_cast<uint16_t>(next++);
}

} // namespace rng
