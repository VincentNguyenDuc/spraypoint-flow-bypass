#include "tcp/traffic-pattern.h"

#include "spraypoint/hash.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void
Expect(const char* name, bool condition)
{
    if (!condition)
    {
        if (failures < 20)
        {
            std::printf("  FAIL: %s\n", name);
        }
        ++failures;
    }
}

template <class Exception = std::invalid_argument, class Function>
void
Reject(const char* name, Function function)
{
    try
    {
        function();
        Expect(name, false);
    }
    catch (const Exception&)
    {
    }
    catch (const std::exception& error)
    {
        std::printf("  %s: wrong exception: %s\n", name, error.what());
        ++failures;
    }
}

void
CheckUniformCompatibility()
{
    for (const uint32_t hosts : {2U, 7U, 1000U})
    {
        for (const uint64_t seed : {0ULL, 561ULL, 562ULL})
        {
            rng::TrafficPattern pattern(hosts, {});
            rng::TrafficListenerPorts ports(hosts);
            std::vector<uint32_t> legacyPorts(hosts, 10000);
            std::mt19937_64 actual(rng::Key(seed, 0xf10, 0xb07));
            std::mt19937_64 legacy = actual;
            std::uniform_int_distribution<uint32_t> pickHost(0, hosts - 1);
            std::uniform_real_distribution<double> size(0.0, 1.0);
            const double meanBytes = 0.9 * 10000 + (1 - 0.9) * 1000000;
            const double capacityBytesPerSecond = hosts * 100.0 * 1e9 / 8;
            const double oldRate = 0.1 * capacityBytesPerSecond / meanBytes;
            const auto rate = rng::TrafficArrivalRate(hosts, 100.0, 0.1, meanBytes);
            Expect("uniform arrival-rate arithmetic is unchanged", rate == oldRate);
            std::exponential_distribution<double> newGap(rate);
            std::exponential_distribution<double> oldGap(oldRate);
            double newTime = 0;
            double oldTime = 0;
            for (unsigned int flow = 0; flow < 10000; ++flow)
            {
                newTime += newGap(actual);
                oldTime += oldGap(legacy);
                const auto pair = pattern.Select(actual);
                const auto source = pickHost(legacy);
                uint32_t destination;
                do
                {
                    destination = pickHost(legacy);
                } while (destination == source);
                Expect("uniform source matches legacy", pair.sourceHost == source);
                Expect("uniform destination matches legacy", pair.destinationHost == destination);
                Expect("uniform size draw matches legacy", size(actual) == size(legacy));
                Expect("uniform arrival time matches legacy", newTime == oldTime);
                Expect("uniform listener port matches legacy",
                       ports.Allocate(pair.destinationHost) == legacyPorts[destination]++);
                Expect("uniform leaves identical RNG state", actual == legacy);
            }
        }
    }
}

void
CheckPermutation()
{
    for (const uint32_t hosts : {2U, 17U})
    {
        for (uint32_t shift = 1; shift < hosts; ++shift)
        {
            rng::TrafficPattern pattern(hosts,
                                        {.pattern = "permutation", .permutationShift = shift});
            std::mt19937_64 random(561);
            std::vector<uint32_t> destinations(hosts, hosts);
            for (unsigned int flow = 0; flow < 2000; ++flow)
            {
                const auto pair = pattern.Select(random);
                Expect("permutation endpoints are distinct",
                       pair.sourceHost != pair.destinationHost);
                Expect("permutation follows configured shift",
                       pair.destinationHost == (pair.sourceHost + shift) % hosts);
                destinations[pair.sourceHost] = pair.destinationHost;
            }
            std::vector<bool> reached(hosts, false);
            for (const auto destination : destinations)
            {
                Expect("every permutation source sampled", destination < hosts);
                if (destination < hosts)
                {
                    Expect("permutation is one-to-one", !reached[destination]);
                    reached[destination] = true;
                }
            }
        }
    }

    const auto hosts = std::numeric_limits<uint32_t>::max();
    rng::TrafficPattern large(hosts, {.pattern = "permutation", .permutationShift = hosts - 1});
    std::mt19937_64 random(561);
    for (unsigned int flow = 0; flow < 1000; ++flow)
    {
        const auto pair = large.Select(random);
        Expect("permutation addition does not overflow",
               pair.destinationHost == (pair.sourceHost == 0 ? hosts - 1 : pair.sourceHost - 1));
    }
    Expect("listener capacity multiplication does not overflow",
           large.MaximumFlows() == uint64_t{hosts} * 55535);
}

void
CheckHotspot()
{
    constexpr uint32_t hosts = 9;
    constexpr uint32_t hotHosts = 3;
    for (const double fraction : {0.0, 0.7, 1.0})
    {
        rng::TrafficPattern pattern(hosts,
                                    {.pattern = "hotspot",
                                     .hotspotHosts = hotHosts,
                                     .hotspotFraction = fraction});
        std::mt19937_64 random(561);
        std::vector<uint32_t> counts(hosts, 0);
        std::vector<uint32_t> sources(hosts, 0);
        constexpr unsigned int flows = 100000;
        for (unsigned int flow = 0; flow < flows; ++flow)
        {
            const auto pair = pattern.Select(random);
            Expect("hotspot endpoints in range",
                   pair.sourceHost < hosts && pair.destinationHost < hosts);
            Expect("hotspot never creates a self-flow", pair.sourceHost != pair.destinationHost);
            ++counts[pair.destinationHost];
            ++sources[pair.sourceHost];
        }
        for (uint32_t destination = 0; destination < hosts; ++destination)
        {
            const double expected = destination < hotHosts ? fraction / hotHosts
                                                           : (1 - fraction) / (hosts - hotHosts);
            const double actual = static_cast<double>(counts[destination]) / flows;
            Expect("hotspot destination mixture", std::abs(actual - expected) < 0.005);
            const double sourceShare = static_cast<double>(sources[destination]) / flows;
            Expect("hotspot sources uniform conditional on excluding destination",
                   std::abs(sourceShare - (1 - expected) / (hosts - 1)) < 0.005);
            if (expected == 0)
            {
                Expect("zero-probability hotspot class is unreachable", counts[destination] == 0);
            }
        }
        const auto destinations =
            fraction == 0 ? hosts - hotHosts : fraction == 1 ? hotHosts : hosts;
        Expect("hotspot reachable destination capacity",
               pattern.MaximumFlows() == uint64_t{destinations} * 55535);
    }
    for (const double fraction : {0.0, 0.5, 1.0})
    {
        rng::TrafficPattern two(2, {.pattern = "hotspot", .hotspotFraction = fraction});
        std::mt19937_64 random(561);
        for (unsigned int flow = 0; flow < 100; ++flow)
        {
            const auto pair = two.Select(random);
            Expect("singleton hotspot/cold sets exclude self",
                   pair.sourceHost + pair.destinationHost == 1);
        }
    }
}

void
CheckIncast()
{
    for (const uint32_t destination : {0U, 5U, 6U})
    {
        for (const uint32_t fanIn : {0U, 1U, 3U, 6U})
        {
            rng::TrafficPattern pattern(7,
                                        {.pattern = "incast",
                                         .incastDestination = destination,
                                         .incastFanIn = fanIn});
            std::mt19937_64 random(561);
            std::vector<uint32_t> counts(7, 0);
            const auto senders = fanIn == 0 ? 6 : fanIn;
            constexpr unsigned int flows = 30000;
            for (unsigned int flow = 0; flow < flows; ++flow)
            {
                const auto pair = pattern.Select(random);
                Expect("incast uses selected destination", pair.destinationHost == destination);
                Expect("incast never creates a self-flow", pair.sourceHost != destination);
                const auto offset = (pair.sourceHost + 7 - destination) % 7;
                Expect("incast uses configured sender set", offset >= 1 && offset <= senders);
                ++counts[pair.sourceHost];
            }
            for (uint32_t offset = 1; offset <= senders; ++offset)
            {
                const auto source = (destination + offset) % 7;
                const auto actual = static_cast<double>(counts[source]) / flows;
                Expect("incast senders sampled uniformly", std::abs(actual - 1.0 / senders) < 0.01);
            }
            Expect("incast capacity is one destination", pattern.MaximumFlows() == 55535);
        }
    }
    rng::TrafficPattern two(2, {.pattern = "incast", .incastDestination = 1});
    std::mt19937_64 random(561);
    const auto pair = two.Select(random);
    Expect("two-host incast", pair.sourceHost == 0 && pair.destinationHost == 1);

    const auto hosts = std::numeric_limits<uint32_t>::max();
    rng::TrafficPattern large(hosts,
                              {.pattern = "incast",
                               .incastDestination = hosts - 1,
                               .incastFanIn = 1});
    Expect("incast cyclic sender addition does not overflow", large.Select(random).sourceHost == 0);
}

void
CheckPairing()
{
    for (const std::string name : {"uniform", "permutation", "hotspot", "incast"})
    {
        rng::TrafficPattern first(17, {.pattern = name});
        rng::TrafficPattern second(17, {.pattern = name});
        rng::TrafficListenerPorts firstPorts(17);
        rng::TrafficListenerPorts secondPorts(17);
        std::mt19937_64 firstRandom(rng::Key(561, 0xf10, 0xb07));
        std::mt19937_64 secondRandom = firstRandom;
        std::exponential_distribution<double> gap(rng::TrafficArrivalRate(17, 100, 0.1, 109000));
        std::uniform_real_distribution<double> size(0, 1);
        for (unsigned int flow = 0; flow < 1000; ++flow)
        {
            Expect("paired arrival draws", gap(firstRandom) == gap(secondRandom));
            const auto a = first.Select(firstRandom);
            const auto b = second.Select(secondRandom);
            Expect("paired endpoints",
                   a.sourceHost == b.sourceHost && a.destinationHost == b.destinationHost);
            Expect("paired flow-size draws", size(firstRandom) == size(secondRandom));
            Expect("paired listener ports",
                   firstPorts.Allocate(a.destinationHost) ==
                       secondPorts.Allocate(b.destinationHost));
            Expect("paired RNG state", firstRandom == secondRandom);
        }
    }
}

void
CheckPorts()
{
    rng::TrafficListenerPorts ports(3);
    for (uint32_t port = 10000; port <= 65534; ++port)
    {
        Expect("listener sequence retains legacy range", ports.Allocate(1) == port);
    }
    for (unsigned int attempt = 0; attempt < 3; ++attempt)
    {
        Reject<std::length_error>("exhausted listener cannot wrap or reuse",
                                  [&] { ports.Allocate(1); });
    }
    Expect("destinations have independent listener ranges", ports.Allocate(0) == 10000);
    Expect("highest destination is valid", ports.Allocate(2) == 10000);
    Reject<std::out_of_range>("out-of-range destination", [&] { ports.Allocate(3); });
    Reject("invalid listener host count", [] { rng::TrafficListenerPorts ports(0); });
}

void
CheckValidation()
{
    Reject("zero hosts", [] { rng::TrafficPattern pattern(0, {}); });
    Reject("one host", [] { rng::TrafficPattern pattern(1, {}); });
    Reject("unknown pattern", [] { rng::TrafficPattern pattern(7, {.pattern = "unknown"}); });
    Reject("zero permutation shift", [] {
        rng::TrafficPattern pattern(7, {.pattern = "permutation", .permutationShift = 0});
    });
    Reject("self permutation shift", [] {
        rng::TrafficPattern pattern(7, {.pattern = "permutation", .permutationShift = 7});
    });
    Reject("empty hotspot set", [] {
        rng::TrafficPattern pattern(7, {.pattern = "hotspot", .hotspotHosts = 0});
    });
    Reject("no background destinations", [] {
        rng::TrafficPattern pattern(7, {.pattern = "hotspot", .hotspotHosts = 7});
    });
    for (const double value : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()})
    {
        Reject("invalid hotspot probability", [value] {
            rng::TrafficPattern pattern(7, {.pattern = "hotspot", .hotspotFraction = value});
        });
    }
    Reject("invalid incast destination", [] {
        rng::TrafficPattern pattern(7, {.pattern = "incast", .incastDestination = 7});
    });
    Reject("invalid incast fan-in", [] {
        rng::TrafficPattern pattern(7, {.pattern = "incast", .incastFanIn = 7});
    });
    Expect("ordinary host count", rng::CheckedTrafficHostCount(1000, 1) == 1000);
    Expect("maximum address-plan host count", rng::CheckedTrafficHostCount(65536, 253) == 16580608);
    Reject("zero routers", [] { rng::CheckedTrafficHostCount(0, 1); });
    Reject("zero hosts per router", [] { rng::CheckedTrafficHostCount(1000, 0); });
    Reject("single total host", [] { rng::CheckedTrafficHostCount(1, 1); });
    Reject("host multiplication overflow", [] {
        rng::CheckedTrafficHostCount(std::numeric_limits<uint32_t>::max(), 2);
    });
    Reject("router address overflow", [] { rng::CheckedTrafficHostCount(65537, 1); });
    Reject("host address overflow", [] { rng::CheckedTrafficHostCount(2, 254); });

    const auto rate = rng::TrafficArrivalRate(10, 100, 0.2, 1000);
    Expect("offered load uses total host capacity", rate == 25000000);
    Expect("doubling hosts doubles arrival rate",
           rng::TrafficArrivalRate(20, 100, 0.2, 1000) == 2 * rate);
    Expect("larger mean flow reduces rate",
           rng::TrafficArrivalRate(10, 100, 0.2, 2000) == rate / 2);
    Expect("overload is a valid experiment",
           rng::TrafficArrivalRate(10, 100, 2, 1000) == 10 * rate);
    Reject("arrival rate requires at least two hosts",
           [] { rng::TrafficArrivalRate(1, 100, 0.1, 1000); });
    for (const double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()})
    {
        Reject("invalid link rate", [value] { rng::TrafficArrivalRate(10, value, 0.1, 1000); });
        Reject("invalid load", [value] { rng::TrafficArrivalRate(10, 100, value, 1000); });
        Reject("invalid mean size", [value] { rng::TrafficArrivalRate(10, 100, 0.1, value); });
    }
    Reject("derived arrival overflow", [] {
        rng::TrafficArrivalRate(10, std::numeric_limits<double>::max(), 0.1, 1000);
    });
    Reject("derived arrival underflow", [] {
        rng::TrafficArrivalRate(10, 1e-300, 1e-300, 1000);
    });
}

} // namespace

int
main()
{
    CheckUniformCompatibility();
    CheckPermutation();
    CheckHotspot();
    CheckIncast();
    CheckPairing();
    CheckPorts();
    CheckValidation();
    std::printf("[traffic patterns] endpoint selection, load, compatibility, and ports: "
                "%d failures\n",
                failures);
    return failures == 0 ? 0 : 1;
}
