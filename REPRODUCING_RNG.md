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

Exactly one file in `simulator/` may contain a `main(` function; ns-3 uses it to find the program's entry point.

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

For a non-adjacent source/destination pair, Model 7.1 predicts about `d(1-exp(-h)) = 55.3` edge-disjoint paths. The simulator's finite graph and deterministic waypoint/ECMP construction need not exactly equal the asymptotic model, but should be in the same range.

## Observed baseline result

The following run completed successfully on this repository:

```bash
./ns3 run 'scratch/rng-spraypoint/main --flows=50000 --samples=500 --seed=561'
```

| Metric | Observed | Paper-model reference |
| --- | ---: | ---: |
| Delivery / hop-limit drops | `50,000 / 50,000` / `0` | loop-free forwarding expected |
| 1-hop path fraction | `0.00084` | `0.001` |
| 2-hop path fraction | `0.06508` | `0.064` |
| 3-hop path fraction | `0.26044` | `0.256` |
| 4-hop path fraction | `0.67364` | about `0.679` |
| 5-hop path fraction | `0` in 50,000 samples | about `7.6e-8` |
| Non-adjacent edge-disjoint paths | `56.72` mean | `55.34` |
| Adjacent edge-disjoint paths | `57.74` mean | separate neighboring-source expression in Model 7.1 |

The observed values reproduce the paper's central routing claims at this scale: almost all baseline paths take 2--4 hops, and the forwarding graph retains roughly 56 edge-disjoint paths for non-adjacent pairs. The uncongested packet-latency p50 and p99 were both 2028 ns because the low-load sample is dominated by 4-hop paths on 500 ns links; this is a simulator sanity result, not an attempt to reproduce the paper's physical-layout latency study.

## Scope

This is a baseline reproduction, not yet the project’s small-flow bypass experiment. It uses actual ns-3 packet transmission and queues, but generates low offered load so that topology/routing properties can be checked independently of congestion. The next implementation step is to add mixed small/bulk workloads and a size-aware bypass policy, then report FCT tails, link hotspots, destination fan-in, and bulk-flow throughput.

## Sources

- Bernardi et al., [RNG: Flat Datacenter Networks at Scale](https://arxiv.org/html/2604.15261v3), especially §§5, 7, and 9.1.
- [Official ns-3 repository](https://gitlab.com/nsnam/ns-3-dev).
