# Reproducing RNG/Spraypoint Baseline Findings

The RNG paper does not provide a public implementation. This repository therefore uses a local implementation on top of official ns-3. The experiment runner downloads the pinned commit `c3b0d18fc6d6e61b0cc792eb45d8fe7a1db78d08` into the ignored `third_party/ns-3-dev` directory on first use.

The Git-tracked simulator implementation lives in [`simulator/`](simulator/). The experiment runner links that directory into ns-3's ignored `scratch/` directory as `scratch/rng-spraypoint/`, which ns-3 builds as one program, `scratch/rng-spraypoint/main`:

| File | Contents |
| --- | --- |
| [`main.cc`](simulator/main.cc) | `main`: command-line options |
| [`rng-experiment.h`](simulator/rng-experiment.h) / [`.cc`](simulator/rng-experiment.cc) | ns-3 fabric, traffic, packet forwarding, and printed metrics |
| [`spraypoint.h`](simulator/spraypoint.h) / [`.cc`](simulator/spraypoint.cc) | random regular graph and Spraypoint spray/pointing tables |
| [`spray-header.h`](simulator/spray-header.h) / [`.cc`](simulator/spray-header.cc) | per-packet routing header |
| [`max-flow.h`](simulator/max-flow.h) / [`.cc`](simulator/max-flow.cc) | Dinic max flow for edge-disjoint path counts |
| [`hash.h`](simulator/hash.h) | deterministic hashing for ECMP and seeding |

Exactly one file in `simulator/` may contain a `main(` function; ns-3 uses it to find the program's entry point. The checks against the paper live in [`verification/`](verification/), outside ns-3; see [Verifying against the paper](#verifying-against-the-paper).

It models the paper's baseline Spraypoint forwarding:

- a simple random `d`-regular graph of ToR routers;
- a source-side ECMP spray across all fabric neighbors;
- destination-relative `WP0`, higher waypoint, inner-ring, and outer-ring levels;
- destination-relative pointing ECMP tables with `p` waypoint selections and `h` next hops;
- ns-3 `PointToPointNetDevice` links at 100 Gbps, 500 ns propagation delay, and 100-packet DropTail queues.

The default parameters reproduce the paper's broader-evaluation configuration: `n=1000`, `d=64`, `p=4`, and `h=2`. The experiment reports:

- sampled packet path-length distribution, to compare with Model 7.2;
- sampled edge-disjoint paths, computed as unit-capacity max flow on the possible Spraypoint forwarding graph, to compare with Model 7.1;
- no-hop-limit-drop status (a check for forwarding loops in sampled traffic);
- packet latency in the uncongested baseline.

## Reproduce from a fresh clone

```bash
git clone <your-repository-url>
cd cse561-project
make reproduce-smoke
```

This requires `git`, Python 3.10+, CMake, Ninja, and a C++23-capable compiler. The first run downloads and builds ns-3; later runs reuse it. `make reproduce` performs the full five-seed baseline experiment.

To run a particular configuration directly:

```bash
python3 scripts/run_rng_experiments.py --seeds 561 --flows 20000 --samples 100
```

`--flows` controls the ns-3 traffic sample. `--samples` controls the number of source/destination pairs used for edge-disjoint-path measurements. Use a fixed `--seed` for repeatability. Pass `--no-clone-ns3` to require a pre-existing dependency checkout.

## Batch experiments

The Git-tracked Python runner configures ns-3 if needed, links the simulator source into its `scratch/` directory, builds it, runs each seed, and writes a CSV:

```bash
python3 scripts/run_rng_experiments.py \
  --seeds 561,562,563,564,565 \
  --flows 50000 --samples 500 \
  --output results/rng_baseline.csv
```

`results/*.csv` is ignored because it is generated data. To reuse an existing build, pass `--skip-build`; to force the minimal ns-3 configuration, pass `--configure`.

## Expected analytical reference points

With the default `n=1000`, `d=64`, `p=4`, `h=2`, the practical regime has one higher waypoint level (`ell=1`). Model 7.2 predicts approximately:

| Path length | Predicted fraction |
| --- | ---: |
| 1 | `1/n = 0.001` |
| 2 | `d/n = 0.064` |
| 3 | `p*d/n = 0.256` |
| 4 | remaining mass, about `0.679` |
| 5 | `exp(-p*d^2/n)`, about `7.6e-8` |

For a non-adjacent source/destination pair, Model 7.1 gives `d(1-exp(-h)) = 55.3` edge-disjoint paths. The paper derives it from a deliberately simplified Spraypoint graph (App. F), and its own measurement (§9.2) is higher: over 50 paths for almost all pairs and over 60 for half.

## Observed baseline result

The following run completed successfully on this repository:

```bash
./ns3 run 'scratch/rng-spraypoint/main --flows=50000 --samples=500 --seed=561'
```

| Metric | Observed | Paper-model reference |
| --- | ---: | ---: |
| Delivery / hop-limit drops | `50,000 / 50,000` / `0` | loop-free forwarding expected |
| 1-hop path fraction | `0.00104` | `0.001` |
| 2-hop path fraction | `0.0631` | `0.064` |
| 3-hop path fraction | `0.25406` | `0.256` |
| 4-hop path fraction | `0.6818` | about `0.679` |
| 5-hop path fraction | `0` in 50,000 samples | about `7.6e-8` |
| Non-adjacent edge-disjoint paths | `59.11` mean | `55.34` (Model 7.1) |
| Adjacent edge-disjoint paths | `58.16` mean | `54.19` (Model 7.1) |

The uncongested packet-latency p50 and p99 were both 2028 ns because the low-load sample is dominated by 4-hop paths on 500 ns links; this is a simulator sanity result, not an attempt to reproduce the paper's physical-layout latency study.

Results recorded before the random-graph fix (see below) came from an irregular graph and should not be compared with these.

## Verifying against the paper

The paper's routing benchmarks (§9.1--9.2) are graph and linear-program computations, not packet simulations. `make verify` checks them directly against the routing code, without ns-3:

```bash
make verify                          # defaults: n=1000, d=64, p=4, h=2, 10 matchings (~2.5 min)
make verify ARGS="--matchings=0"     # skip oversubscription (~2 s)
```

It reports:

- **Structure.** The graph is simple, symmetric, and `d`-regular. For every destination, the levels and pointing tables follow the four §5 rules. Pointing is loop-free, and paths have at most `ell+4` hops. The program exits non-zero on any violation.
- **Path lengths.** The exact distribution over all ordered pairs, spray choices, and ECMP choices, compared with Model 7.2.
- **Edge-disjoint paths.** The min-cut distribution over sampled pairs, compared with §9.2 and Model 7.1.
- **Oversubscription.** Results are computed over random ToR matchings in which every ToR sends at full rate (`d` links).
  - *Optimal split.* This is the paper's multi-commodity-flow LP over Spraypoint paths, solved as a maximum concurrent flow with the Garg–Könemann algorithm. It reports a certified bracket: the upper end comes from a feasible flow, the lower end from LP duality.
  - *ECMP.* Every hop splits evenly across its ECMP next hops, as hashed packets do in the ns-3 simulator.

Results for seed 561:

| Benchmark | This implementation | Paper |
| --- | ---: | ---: |
| Structural violations | 0 | — |
| Path length 1 / 2 / 3 / 4 / 5 hops | 0.0010 / 0.0631 / 0.2563 / 0.6797 / 0 | Model 7.2: 0.001 / 0.064 / 0.256 / 0.679 / 7.7e-8 |
| Edge-disjoint paths, non-adjacent: min / p10 / median / p90 | 52 / 57 / 59 / 62 | §9.2: >50 for almost all, >60 for half |
| Pairs with more than 50 / more than 60 edge-disjoint paths | 100% / 27% | almost all / about 50% |
| Oversubscription, optimal split (10 matchings, worst) | 3.12--3.30 | §9.1: 3.25 (worst of 100); Model 7.3: 3.27 |
| Oversubscription, per-hop ECMP split (worst) | 12.0 | not reported |

Three findings matter for later experiments:

1. **Edge-disjoint paths fall slightly short.** The median is 59, against the paper's "over 60". A variant that always keeps the waypoint's selecting node among its `h` next hops raises the median to 60, with 43% of pairs above 60. The paper's text describes a uniform choice instead, so this repository keeps the uniform rule. The remaining gap of one or two paths is within details the paper leaves open, such as its ShuffleBox-constrained graphs.
2. **The paper's 3.25 assumes an optimal split.** Equal ECMP splitting at every hop gives `r` of about 10--12.
   - The busiest link is always the direct link of an adjacent source/destination pair. The source `s` is in `WP0(t)`, and about 20 of its neighbors are WP1 waypoints. Each of them picks `s` as a next hop with probability about 2/5, so roughly 16% of the pair's traffic returns through `s` onto the direct link.
   - Packet-level results with hash-based ECMP will therefore see much less capacity than the paper's headline number. This direct-link hotspot is also the baseline for the bypass hotspot question (H2).
3. **A fixed random-graph bug changed earlier results.** Until this fix, the random-graph generator popped the wrong element when it recorded a matched pair. Degrees ranged from 50 to 85 instead of 64, and some pairs had more than 64 "edge-disjoint paths". The averages happened to look right, which is why the earlier baseline appeared to match. The constructor now rejects any graph that is not `d`-regular.

## Scope

This is a baseline reproduction, not yet the project’s small-flow bypass experiment. It uses actual ns-3 packet transmission and queues, but generates low offered load so that topology/routing properties can be checked independently of congestion. The next implementation step is to add mixed small/bulk workloads and a size-aware bypass policy, then report FCT tails, link hotspots, destination fan-in, and bulk-flow throughput.

## Sources

- Bernardi et al., [RNG: Flat Datacenter Networks at Scale](https://arxiv.org/html/2604.15261v3), especially §§5, 7, and 9.1.
- [Official ns-3 repository](https://gitlab.com/nsnam/ns-3-dev).
