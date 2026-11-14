# Analysis: Results

Routing small flows on the shortest path makes them faster, and under realistic traffic it costs other traffic nothing. Under worst-case traffic on a busy network, it hurts: pairs of routers with a single shortest path overload it, their small flows time out more often, and their large flows lose throughput. Our models predict when this happens and which pairs pay, and the simulations agree.

| Hypothesis | Answer | Evidence |
| --- | --- | --- |
| **H1:** the bypass cuts small-flow latency | **Yes**, while the network is not overloaded | Median −45% between neighboring routers, −25% for other small flows (shortest-path bypass), every seed and load under uniform traffic |
| **H2:** past some point, the bypass makes things worse | **Yes, under worst-case traffic.** Not under uniform traffic | Permutation traffic at load 0.3–0.4: 2–7× more timeouts for small flows between neighbors, slowest 1% of large flows lose up to 76% |
| **H3:** the shortest-path bypass reaches more flows, with more risk | **Yes** | The adjacent bypass reaches 6% of small flows; the shortest-path bypass all of them. Its worst case is worse for other traffic |
| **H4:** the overhead is negligible | Argued, not measured | Still to write: see [TODO.md](TODO.md) |

The models behind these predictions are in [bypass-modelling.tex](bypass-modelling.tex). [TODO.md](TODO.md) lists what is still open.

## How we measure

**Programs.**

- **ns-3 simulation** (`tcp-experiment`, `tcp-bypass`): real TCP flows over the Spraypoint fabric, with drop-tail queues. Every router picks next hops by hashing the flow, as ECMP does. All three modes (no bypass, adjacent, shortest-path) run the exact same flows, so each flow can be compared with itself.
- **Flow-level check** (`bypass-model`): no packets. It computes path lengths, shortest-path counts and worst-case throughput on the same routing tables, with ECMP's even split and with the best possible split (an LP). It tests the models at both our scale and the paper's.

**Traffic.**

- **Uniform:** each flow goes between two random hosts. This is the realistic case.
- **Permutation:** every router's hosts send only to one partner router. This is the worst case for throughput, and the one the models describe. The ns-3 permutation is the flow-level check's permutation 0 for the same seed, so the two can be compared pair by pair.
- **f**, the small flows' share of the bytes: 8.3% by default (90% of flows at 10 KB, 10% at 1 MB). `06-permutation` raises it to 20% and 30% by making small flows larger.

**Metrics.**

- **Small-flow latency.** A small flow that loses nothing finishes in under 300 µs. One that loses a packet waits at least the 1 ms minimum retransmission timeout, and a lost connection setup waits the 10 ms handshake timeout. So a p99 mostly shows whether more than 1% of flows timed out. We report:
  - **Timeouts:** the share of small flows that waited for any timeout.
  - **Clean p99:** the p99 of the flows that did not, which is queueing alone.
  - **Median**, and the raw p99 alongside.
- **Cost to other traffic.**
  - **Large-flow goodput:** each large flow's bytes over its completion time, as the mean and the slowest 1%.
  - **Throughput kept per pair:** under permutation traffic, each pair's large-flow goodput with the bypass over the same pair's without it. The **worst pair** is what the models predict.
- **Where load goes.** Link utilization, the links the bypass makes busiest and whether their queues fill, and fan-in into each destination.

**Reading the numbers.** Results pool 3 seeds and leave out flows in the first and last 10% of each run. A change that has the same sign in every seed is treated as real. Per pair, ns-3 varies by about ±3–5% from timing alone, so "throughput kept" between 0.97 and 1.03 means no change.

## Data

| Sweep | Traffic | Network | Varies | Batch in `experiments/results/remote/` |
| --- | --- | --- | --- | --- |
| `03-load-sweep` | uniform | 256 routers, degree 16, 4 hosts each | load 0.1, 0.3, 0.5 | `2026-11-07_120750_5770a94` |
| `02-small-sweep` | uniform | 64 routers, degree 8, 2 hosts each | load 0.1–0.7 | same |
| `04-oversubscription` | uniform | as `03`, with 6 and 8 hosts each | hosts per router, at load 0.5 | `2026-11-12_221557_cf0fc72` |
| `05-bypass-model` | permutation, flow level | 256 / 16 and 1,000 / 64 | f from 0 to 30%, 10 permutations | `2026-11-13_102950_bcb7193` |
| `06-permutation` | permutation | as `03` | f = 8.3, 20, 30%, load 0.1–0.4 | same |

Every ns-3 sweep covers seeds 561–563 (561–565 for `02`) and all three modes. Every flow finished in every run, with no TTL or no-route drops, and all modes ran the same flows. The 256-router network keeps the paper's path lengths and share of neighboring pairs (6%). Results are not committed: `./REMOTE.sh fetch` copies them, and each batch's `analysis/` folder holds the executed notebooks.

## H1: The bypass cuts small-flow latency

**Uniform traffic, load 0.5** (`03-load-sweep`):

| Small flows between | Mode | Fabric hops | Median (µs) | Clean p99 (µs) | Timeouts |
| --- | --- | ---: | ---: | ---: | ---: |
| Neighboring routers | No bypass | 3.30 | 16.9 | 49.0 | 1.32% |
| | Adjacent bypass | 1.00 | 9.2 | 38.5 | 0.63% |
| | Shortest-path bypass | 1.00 | 9.3 | 38.9 | 0.65% |
| Other routers | No bypass | 3.64 | 18.1 | 50.3 | 1.38% |
| | Shortest-path bypass | 2.36 | 13.7 | 44.6 | 0.94% |

- **Consistent at every load.** At loads 0.1, 0.3 and 0.5, the median falls 43–45% between neighbors and 22–25% for other small flows, and the clean p99 falls 11–33%, in every seed.
- **It comes from crossing fewer queues:** one router queue instead of two to five.
- **The gain grows as the fabric fills** (`04-oversubscription`). With 8 hosts per router the fabric runs at 77% and drops 1.6 million packets per run. Even so, the bypass cuts timeouts between neighbors from 9.0% to 2.4%, and for other small flows from 10.0% to 5.5% (shortest-path bypass).
- **Under permutation traffic at low load, the same.** At load 0.1, the median between neighbors halves at every f (for example 22 → 13 µs at f = 30%).

## H2: The cost to other traffic

### Uniform traffic: no cost

In `03` and `04`, every measure of cost is flat or better with the bypass, at every load and up to 8 hosts per router:

| | 4 hosts | 6 hosts | 8 hosts |
| --- | ---: | ---: | ---: |
| Large-flow goodput, change with the shortest-path bypass | +2% | +5% | +8% |
| Other small flows' timeouts, adjacent bypass vs none | 1.38 → 1.34% | 4.58 → 4.53% | 9.96 → 9.78% |
| Fan-in: busiest neighbor's share of a destination's traffic | 0.12, unchanged | 0.11, unchanged | 0.11, unchanged |

Shorter paths put fewer bytes on the fabric, so it drops 12–18% fewer packets. The bypass does load some links more (the top 1% go from 0.68 to 0.75 utilization at 8 hosts), but under uniform traffic each pair carries little, and the network gains more than these links lose.

### Permutation traffic: the crossover

`06-permutation` concentrates each router's traffic on one partner. Once the fabric is busy, the bypass hurts.

**Small flows between neighboring routers,** timeouts:

| f | Load | No bypass | Adjacent bypass | Shortest-path bypass |
| ---: | ---: | ---: | ---: | ---: |
| 8.3% | 0.2 | 0.06% | 0.14% | 0.07% |
| | 0.4 | 1.8% | 3.9% | 3.9% |
| 20% | 0.3 | 0.6% | 2.4% | 2.1% |
| | 0.4 | 1.8% | 8.1% | 7.2% |
| 30% | 0.3 | 0.4% | 2.8% | 2.4% |
| | 0.4 | 1.5% | 11.3% | 9.5% |

At f = 30% and load 0.4 their median also rises, from 35 to 45 µs. All of a neighboring pair's small flows share one direct link, which fills and drops packets. The bypass hurts the flows it was meant to help.

**Other small flows,** shortest-path bypass, load 0.4: timeouts rise from 1.3% to 1.4% (f = 8.3%), 1.4% to 3.0% (20%) and 1.2% to 4.6% (30%). The adjacent bypass leaves them unchanged.

**Large flows.** Mean goodput moves by under 1.5% in every case: the cost falls on a minority of pairs.

| f, load 0.4 | Slowest 1% of large flows, adjacent bypass | Same, shortest-path bypass |
| ---: | ---: | ---: |
| 8.3% | +2% | −7% |
| 20% | −18% | −35% |
| 30% | −33% | −76% |

**Who pays.** Throughput kept per pair at f = 30% and load 0.4:

| Pairs | Adjacent bypass: mean / worst | Shortest-path bypass: mean / worst |
| --- | --- | --- |
| Neighboring (one shortest path) | 0.89 / 0.70 | 0.93 / 0.68 |
| 2 hops, one shortest path | 1.00 / 0.92 | 0.98 / 0.63 |
| 2 hops, two shortest paths | 1.00 / 0.93 | 1.00 / 0.84 |
| 2 hops, three or more | 1.00 / 0.94 | 1.03 / 0.90 |
| 3 or more hops | 1.00 / 0.94 | 1.04 / 0.80 |

On average, only pairs with a single shortest path lose, as the models predict. With the shortest-path bypass, a few other pairs also lose at worst: 3-hop pairs cross more links, including the ones the bypass made busy, which the flow-level check also shows. With the shortest-path bypass, the links it loads most go from 31% to 75% busy, and 89% of them fill their queue at some point, against 11% without it.

**When.** The cost grows with both f and load. At load 0.1 the worst pair keeps about 96% at every f, which is ns-3's noise floor. The flow-level check puts the busiest link's saturation near load 0.38 at this scale, and the harm appears from load 0.3: small flows suffer before throughput does.

## The models against the measurements

Full derivations and checks are in [bypass-modelling.tex](bypass-modelling.tex).

- **Path lengths and shortest-path counts** match the models closely at both scales. For example, small flows between non-neighbors take 2.36 hops in ns-3 against the predicted 2.39, and 6.3% of 2-hop pairs at the paper's scale have one shortest path against the predicted 6.9%.
- **Ideal split (the LP), flow level:** exact. At the paper's scale the bypass is free while f is under about 5%, then costs throughput in proportion to f.
- **ECMP, flow level:** the model matches within 2% once its baseline is measured.
- **ECMP, ns-3:** at load 0.4, the worst pair's throughput kept matches the model's prediction for the same permutation within 0–5 points:

  | Bypass | f | Model | ns-3 |
  | --- | ---: | ---: | ---: |
  | Adjacent | 8.3% | 96% | 93% |
  | | 20% | 88% | 83% |
  | | 30% | 80% | 76% |
  | Shortest-path | 8.3% | 93% | 89% |
  | | 20% | 82% | 81% |
  | | 30% | 69% | 69% |

- **Pair by pair, loosely.** Pairs the flow-level check says have busier links get less throughput in ns-3, with a rank correlation of about −0.25.
- **What the models miss:** latency. They describe throughput at saturation, but ns-3 shows small flows timing out more from load 0.3, before throughput suffers.

**Side finding: plain ECMP is about 3.5× from the paper's oversubscription.** Under worst-case permutations, Spraypoint with even ECMP splitting reaches an oversubscription of about 11.5 at the paper's scale, against the paper's 3.25, which assumes the best possible split. Each pair overloads one of its own links into the destination. Our simulations run with ECMP, so they sit on the ECMP side.

## What this means

- **Realistic workloads are safe.** In the published web search and Hadoop workloads, flows under 30 KB carry under 1% of the bytes, well below the knee. Our test mix (8.3%) is pessimistic.
- **Policy:** keep each pair's bypassed bytes to about 1% of its traffic with plain ECMP (about 4% throughput lost in the worst case), by capping the bytes per flow or per pair that may take the bypass. Don't bypass pairs with a single shortest path.
- **The adjacent bypass isn't the safe option.** It reaches only 6% of small flows, and neighboring pairs, the ones it serves, are the ones that overload. It is cheaper to build (no second routing table), which matters for H4.

## H4: Overhead

Not measured, as the proposal planned. The switch-pipeline argument is still to be written.

## Limitations

- **Drop-tail queues with no ECN.** Losses, not queueing, set the tail. A DCTCP-style signal would likely shrink timeout rates.
- **A new connection for every flow.** Lost handshakes cause 30–50% of small-flow timeouts. Persistent connections would remove most of them.
- **Timeouts are inferred from completion time,** which is reliable for small flows only.
- **ECMP only.** ns-3 has no adaptive load balancing or multipath transport, which the paper relies on to approach its 3.25.
- **Scale.** ns-3 runs at 256 routers with 3 seeds of 4 ms each. Paper-scale results come from the flow-level check only. There are no confidence intervals yet.
- **f is only varied under permutation traffic so far.** Under uniform traffic, only f = 8.3% is tested; `07-share-sweep` covers 20% and 30%.

## Reproducing

```bash
./REMOTE.sh fetch
cd experiments/analysis
RESULTS_MACHINE=remote SWEEP=03-load-sweep .venv/bin/jupyter nbconvert \
    --to notebook --execute --inplace 01-baseline.ipynb 02-bypass-comparison.ipynb
```

The committed notebooks show `03-load-sweep`. Set `SWEEP=04-oversubscription` or `SWEEP=06-permutation` for the other sweeps, and run `03-model-check.ipynb` with `SWEEP=06-permutation` for the model comparison. `./REPRODUCE.sh` writes executed copies for every sweep into each batch's `analysis/` folder.
