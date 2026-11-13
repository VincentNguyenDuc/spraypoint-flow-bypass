# Modelling the Spraypoint small-flow bypass

Nov 13, 2026 · @Vincent

## Summary

We can model three things about the bypass analytically, by extending §7 and Appendices E–G of the RNG paper. Latency, queueing and burstiness are left to the ns-3 simulation.

| Question | Can we model it? | Headline at the defaults |
| --- | --- | --- |
| Path length for small flows | Yes (Model 1) | Mean 3.61 hops → 1.95 with the bypass |
| Number of edge-disjoint paths | Yes (Model 2) | Spraypoint ≈ 55 per pair; bypass c = 1 (adjacent), ≈ 4.1 (2-hop) |
| Worst-case throughput (oversubscription) | Yes, two bounds (Model 3) | Ideal: free while f < c/(d·Σμ). ECMP: cost starts at f = 0 |
| Small-flow FCT and tail latency | No, sim only | — |
| Queueing below full load, bursts | No, sim only | — |

The main risk is pairs with a single shortest path (c = 1). That's every adjacent pair plus about 7% of 2-hop pairs. They hit the throughput knee at f ≈ 5%.

The paper's own models are reused unchanged. Everything about the bypass is our derivation, to be checked against the simulation.

## Setup

One unit of capacity is one fabric link. Defaults are the paper's: n = 1000, d = 64, p = 4, h = 2, so ℓ = 1.

| Symbol | Meaning | Default |
| --- | --- | --- |
| n | Routers (one ToR per rack) | 1000 |
| d | Fabric links per router | 64 |
| p | Waypoints each node picks when building the next level | 4 |
| h | Next hops each node keeps toward t | 2 |
| ℓ | Waypoint levels above WP0, ℓ = max(1, ⌈log\_p(n/2d²)⌉) | 1 |
| μᵢ | Share of a flow that fits on i-hop paths (Model 7.3) | 0.064, 0.131, 0.111, ≈ 0 |
| Σμ | μ₂ + μ₃ + μ₄ + μ₅; oversub ≈ 1/Σμ | 0.306 (3.27:1) |
| X\_sp | Worst-case rate per pair under Spraypoint, d·Σμ | ≈ 19.6 links |
| f | Share of a pair's bytes in small (bypassed) flows | swept 0–30% |
| c | Edge-disjoint shortest paths for a pair | 1 (adjacent), ≈ 4.1 (2-hop, varies) |

Assumptions, the same as the paper's:

1. The fabric is a uniform random d-regular graph.
2. Worst-case traffic is a permutation: every router sends to one partner at once.
3. n is large and d ≪ n.

Our added assumptions for the bypass:

4. Small flows take a shortest path, with ECMP over ties, and keep their class end to end, using their own routing table.
5. Spraypoint's tables are unchanged. Large flows use them exactly as in the paper.
6. Every pair has the same f.

## Model 1: Path length

The bypass cuts a small flow's mean path from 3.61 hops to 1.95. Spraypoint's own path lengths don't change, because the bypass is a separate lane.

**Spraypoint (paper, Theorem E.4 / Model 7.2).** Build rings around t: WP0 (d routers), WP1 (≈ p·d), the inner ring, and the outer ring. A sprayed packet's hop count depends on which ring the spray lands in:

```latex
P(L=i) \approx \begin{cases} 1/n & i=1 \\ p^{\,i-2}\,d/n & 2 \le i \le \ell+2 \\ \text{rest} & i=\ell+3 \\ e^{-p^{\ell} d^2/n} & i=\ell+4 \end{cases}
```

**Bypass (ours, Model B.1).** Small flows go straight to t, so the hop count is the shortest distance. s is farther than k hops away only if all d of its links miss t's (k−1)-hop neighborhood, which has about d^(k−1) routers:

```latex
P(L_{\text{byp}} \le k) \approx 1 - e^{-d^k/n}
```

| Hops | Spraypoint (paper) | Bypass, shortest distance (ours) |
| --- | --- | --- |
| 1 | 0.1% | 6.4% |
| 2 | 6.4% | 92.2% |
| 3 | 25.6% | 1.4% |
| 4 | 67.9% | ≈ 0 |
| 5 | ≈ 0 | — |
| **Mean** | **3.61** | **1.95** |

The bypass length depends only on n and d. p, h and ℓ drop out. The adjacent-only variant helps just 6.4% of pairs, so its mean falls only to about 3.45.

Across all traffic, the network sees the byte-weighted blend: (1 − f)·3.61 + f·1.95.

## Model 2: Number of edge-disjoint paths

Spraypoint gives every pair about 55 separate lanes. The bypass gives small flows only c lanes, and c is smallest for the closest pairs.

**Spraypoint (paper, Appendix F / Model 7.1).** Each lane needs its own front door into t, one of t's d neighbors. So the lane count is a matching: pair s's sprayed neighbors with different doors. Each sprayed neighbor reaches h random doors, so it's balls into bins, and about e^(−h) of the doors stay empty:

```latex
\mathrm{EDP}(s,t) \approx \begin{cases} d\,(1-e^{-h}) & s \notin \mathrm{Nbr}(t) \\ \min\!\big(d-p,\ d\,(1-e^{-(1-p/d)h})\big) & s \in \mathrm{Nbr}(t) \end{cases}
```

At the defaults that's about 55 for a typical pair and about 54 for an adjacent pair. The simulation median is above 60, so the formula runs low.

**Bypass (ours, Model B.2).** The bypass uses only shortest paths, so c is the number of edge-disjoint shortest paths:

| Pair distance | Share of pairs | c | Note |
| --- | --- | --- | --- |
| 1 (adjacent) | 6.4% | 1 | Direct link only |
| 2 | 92% | Poisson(d²/n) given ≥ 1, mean ≈ 4.1 | About 7% of these have c = 1, and about 14% have c = 2 |
| 3 | 1.4% | up to ≈ 63 | Many shortest paths, so never a bottleneck |

**How the two interact.** The bypass adds no links, so Spraypoint's lane count is unchanged. For a 2-hop pair, the bypass paths s→w→t *are* Spraypoint lanes. Small flows just take c of the pair's own lanes. **For one pair on its own, the bypass costs nothing.** The cost only appears when other pairs need the same links, which is Model 3.

## Model 3: Oversubscription with the bypass

In the paper's ideal model the bypass is free until the small flows outgrow their c shortest paths. With ECMP hashing the cost starts at f = 0. Either way, pairs with c = 1 break first, at about f = 5%.

**Paper (Appendix G / Model 7.3).** Everyone sends to a partner. Fill 2-hop paths first, then 3-, 4- and 5-hop paths with the capacity that's left. Divide by how many paths share each link. The result is oversub ≈ 1/(μ₂+μ₃+μ₄+μ₅) ≈ 3.27. Lemma G.2 adds a lower bound: oversub ≥ the average hop count, because a byte on an i-hop path uses i links.

**Bypass, ideal version (ours).** The greedy fill already uses each pair's 2-hop paths (μ₂·d = d²/n of them) at full capacity, and those are exactly the bypass's paths. So small flows take seats that were going to be filled anyway. The only new limit is that f·x has to fit on c links:

```latex
\text{oversub}_{\text{ideal}} = \max\!\Big(\frac{1}{\Sigma\mu},\ \frac{f\,d}{c}\Big), \qquad \text{free while } f < \frac{c}{d\,\Sigma\mu}
```

**Bypass, ECMP version (ours, what ns-3 does).** Hashing spreads Spraypoint traffic evenly over all d neighbors instead of preferring short paths. Every link carries background load, so the bypass load adds to it:

```latex
\text{oversub}_{\text{ECMP}} \approx O_{\text{ecmp}}\,(1-f) + \frac{f\,d}{c}, \qquad O_{\text{ecmp}} \gtrsim 3.6 \ \text{(average hop count, Lemma G.2)}
```

In the ideal world the two parts take a max. With hashing they add.

**Throughput kept, relative to no bypass:**

| f | Adjacent, ideal | Adjacent, ECMP | 2-hop (c ≈ 4.1), ideal | 2-hop (c ≈ 4.1), ECMP |
| --- | --- | --- | --- | --- |
| 1% | 100% | 86% | 100% | 97% |
| 5% | 100% | 54% | 100% | 86% |
| 10% | 51% | 37% | 100% | 75% |
| 20% | 26% | 23% | 100% | 60% |

Knees in the ideal model: f ≈ 5.1% for c = 1 and f ≈ 20.9% for c ≈ 4.1.

**The weak-pair caveat.** Oversub is set by the worst pair. Every adjacent pair has c = 1, and so do about 7% of 2-hop pairs. So the realistic network-wide knee is about 5%, not 21%. Widening the bypass to "any path of length ≤ 2" gives adjacent pairs c ≈ 5, but it doesn't help 2-hop pairs with one common neighbor.

The interactive version of these curves is in [Spraypoint Lanes and the Bypass](https://claude.ai/artifact/CoaWVHPsZrSmxpWeFwHdw1).

## What the models can't tell us

The models give hop counts, lane counts and worst-case capacity. Anything about time and queues needs the simulation.

- **Small-flow FCT and tail latency (p99, p99.9).** Hop count gives a floor, about L × per-hop delay, but not the queueing on top.
- **Behavior below full load.** "Free" in Model 3 means the traffic fits at saturation. The extra load on bypass links can still add queueing for large flows before that point.
- **Bursts.** f is a long-run average. Bursts of small flows can briefly overload the c bypass links.
- **The real ECMP baseline.** 3.6 is only a lower bound. Hash collisions push it higher.
- **Non-uniform load.** The ECMP formula assumes every link is equally loaded. Direct links may differ.
- **Different f per pair.** Real workloads concentrate small flows on some pairs.
- **Side effects not covered by the paper's proofs.** The bypass table uses ECMP memory, which might force a smaller h. Loops are avoided only if packets keep their class at every hop.

## Predictions to test in ns-3

Each model gives a number the simulation can confirm or break.

| Prediction | Model | What to measure |
| --- | --- | --- |
| Bypassed packets average ≈ 1.95 hops vs 3.61 for sprayed ones | 1 | Hop-count histogram per class |
| 6.4% of pairs are adjacent; 92% are 2 hops apart | 1 | Distance distribution of your generated graph |
| Spraypoint gives ≈ 55 lanes per pair; bypass gives c = 1 or ≈ 4 | 2 | Edge-disjoint paths on the Spraypoint graph and on the shortest-path DAG |
| Spraypoint-only oversub with ECMP is ≥ 3.6 | 3 | Permutation traffic, highest rate each router sustains |
| Throughput loss vs f lies between the ideal and ECMP curves | 3 | Sweep f = 1, 2, 5, 10, 20% at full load |
| Pairs with c = 1 lose throughput first, near f ≈ 5% | 3 | Per-pair throughput grouped by c |
| Below the knee (load ρ < R(f)) the bypass doesn't cut large-flow throughput | 3 | Load sweep ρ = 0.1–0.9 |

f comes from the workload and the size threshold for "small": f = bytes in flows under the threshold ÷ all bytes. Pick a standard flow-size distribution and a threshold (e.g. 100 KB), report the resulting f, and also sweep f directly.

## Sources

- [RNG: Flat Datacenter Networks at Scale](https://arxiv.org/pdf/2604.15261): §5 (Spraypoint), §7 (Models 7.1–7.3), §9.1 (oversub by LP), Appendices E, F, G. Read from the full PDF.
- Namyar et al., "A throughput-centric view of the performance of datacenter topologies," SIGCOMM 2021 (RNG ref. \[37\]): the worst case is a permutation.
- Bypass models B.1, B.2 and both Model 3 extensions are our own derivation.
