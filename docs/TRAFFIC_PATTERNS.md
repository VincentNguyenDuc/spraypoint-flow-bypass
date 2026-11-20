# Traffic-pattern semantics

`src/tcp/traffic-pattern.h` is independent of ns-3 and routing mode. It selects endpoints
for one flow at a time; flow sizes and the Poisson arrival process remain with
the experiment. This adds workload-generation capability. It does not constitute
a completed traffic-pattern sweep or establish a performance result.

## Patterns

Host IDs range from zero to `H - 1`. Every pattern requires `H >= 2` and excludes
self-flows. Router permutation always crosses routers. Other patterns can
select different hosts on the same router.

| Pattern | Parameters and defaults | Selection |
| --- | --- | --- |
| `uniform` | None | Uniform source, then repeated uniform destination draws until distinct. |
| `permutation` | `permutationShift=1` | Uniform source `s`, destination `(s + shift) mod H`. Require `1 <= shift < H`. |
| `router-permutation` | `nodes`, `hostsPerRouter`, `seed` | Uniform source host and uniform destination host on its router's partner. Require at least two routers with equal host counts. |
| `hotspot` | `hotspotHosts=1`, `hotspotFraction=0.9` | Choose the destination uniformly from IDs `[0, K)` with probability `p`, or `[K, H)` with probability `1-p`; choose the source uniformly from all other hosts. |
| `incast` | `incastDestination=0`, `incastFanIn=0` | Fixed receiver `d`; choose a source uniformly among `(d+1) mod H` through `(d+F) mod H`. A fan-in of zero means `F=H-1`. |

The host permutation is a deterministic bijection with no fixed points. It need not
form a single cycle: any nonzero shift below `H` is valid. Source occurrences
remain random, so a finite run need not sample every source equally often.

The router permutation uses `RandomMatching(nodes, seed + 1000)`, the same
matching as index 0 in `bypass-model`. The matching uses a separate RNG. The
workload retains the source and destination draws used by earlier
`06-permutation` runs, whose recorded arguments called this pattern
`permutation`. Use `router-permutation` to reproduce those inputs on this
version.

Hotspot requires `1 <= K < H` and finite `0 <= p <= 1`. The fraction controls the
probability of targeting the hot set, including at its endpoints zero and one;
the observed finite-run fraction fluctuates. Selecting destinations before
sources preserves this meaning when the hot set contains a single host. Source
IDs are consequently not marginally uniform when destination probabilities
differ. Incast requires `0 <= d < H` and `0 <= incastFanIn < H`. Its sender set is
fixed for a run. Pattern-specific options are validated only for their pattern.

Incast here means asynchronous many-to-one destination concentration. Arrivals
are individual Poisson events. Synchronized fan-in batches and microbursts are
separate future workload models.

`CheckedTrafficHostCount(routers, hostsPerRouter)` checks multiplication in
64 bits, at least two total hosts, and the existing IPv4 plan's limits of 65,536
routers and 253 hosts per router. Topology degree constraints still belong to
the topology builder. The selector accepts a `uint32_t` host count of at least
two and allocates a partner vector only for router permutation.

## Offered load and matched routing comparisons

For every pattern, with per-host link rate `C` bytes/s and mean flow size `B`,
the arrival rate is `load * H * C / B`. `TrafficArrivalRate` retains the legacy
arithmetic order. Link rate, load, mean size, and the resulting rate must all be
finite and positive. Loads above one remain valid overload experiments.

The denominator remains total host egress capacity, including hosts that send
no traffic in a constrained incast. It is not adjusted to receiver capacity.
The expected byte rate into one hot destination is `load * H * C * p / K`;
one cold destination receives `load * H * C * (1-p) / (H-K)`. The incast receiver
sees `load * H * C`, so its offered input exceeds its link rate once
`load > 1/H`. Each of its `F` eligible senders offers `load * H * C / F` on
average. These definitions make destination concentration explicit instead of
silently lowering total offered traffic.

For fair baseline/bypass comparisons, use the same topology seed, pattern and
its parameters, load, flow limits, and size distribution. The selector never
observes routing mode, classification threshold, completion times, or simulator
RNG state. Keep the existing workload seed `Key(seed, 0xf10, 0xb07)` and draw
order: exponential gap, endpoint selection, then size. Uniform performs exactly
the old endpoint draws and retries; constructors consume no randomness. This
preserves existing uniform workloads with the same compiler/standard library.
C++ distribution algorithms are not promised to be identical across different
standard-library implementations.

Within any one pattern and configuration, baseline and bypass generate identical
flow endpoints, starts, sizes, and listener ports. Different patterns consume
different numbers of random draws; equal seeds across patterns do not imply
identical arrival times or sizes. A sweep should pair modes within each pattern.

## Listener budget

`TrafficListenerPorts` preserves ports 10000 through 65534 inclusive: exactly
55,535 unique listeners per destination. Port 65535 remains excluded for legacy
compatibility. Allocation fails with the destination and capacity in the error
message; repeated attempts cannot wrap or recycle ports. Completed-flow cleanup
does not enlarge this budget. Distinct listener ports preserve per-flow receipt
attribution and distinct connection five-tuples.

`TrafficPattern::MaximumFlows()` is an upper bound from reachable destinations:
`H * 55535` for uniform and both permutations, `55535` for incast, and the corresponding
reachable destination count for hotspot (only `K` at `p=1`, only `H-K` at `p=0`,
otherwise `H`). Random concentration can exhaust a destination sooner. When
`spanMs == 0` and the requested exact flow count exceeds this bound, reject it
before generation. With a time limit, the requested count is only a cap, so the
actual allocation check determines whether the generated workload fits. Do not
silently truncate workloads, reuse ports, or treat the bound as a guarantee that
every random workload below it will fit.

## Integration

`PatternConfig` passes the pattern-specific CLI options and the existing router
count and seed from the experiment config:

```cpp
const TrafficPatternConfig patternConfig{
    .pattern = config.pattern,
    .permutationShift = config.permutationShift,
    .hotspotHosts = config.hotspotHosts,
    .hotspotFraction = config.hotspotFraction,
    .incastDestination = config.incastDestination,
    .incastFanIn = config.incastFanIn,
    .routers = config.nodes,
    .seed = config.seed,
};
```

The constructor validates the host count, selected pattern and exact-flow-count
capacity. `GenerateFlows` uses the selector and `TrafficListenerPorts` before
scheduling applications, preserving the exponential-gap/endpoint/size draw order.
`TrafficArrivalRate` supplies the same total-host-capacity normalization. Printed
summaries record all pattern parameters. The pure `traffic-pattern` library is a
dependency of `tcp`, with its test registered in `test/traffic-pattern`.

The pure test can also run without configuring ns-3 or using the shared build:

```sh
g++ -std=c++23 -O0 -Wall -Wextra -Werror -pedantic -I src \
    src/tcp/traffic-pattern.cc src/spraypoint/matching.cc \
    test/traffic-pattern/traffic-pattern-test.cc \
    -o /tmp/cse561-traffic-pattern-test
/tmp/cse561-traffic-pattern-test
```

It compares 90,000 uniform flows against the original generation operations,
including RNG state and arrival times, and checks deterministic replay,
host permutation bijection, router matching and draw compatibility,
destination and sender distributions, two-host edge
cases, integer overflow, invalid parameters, normalization, and complete port
exhaustion. These are generation tests; TCP performance sweeps remain pending.
