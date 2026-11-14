# Verification: Spraypoint Against the Paper

The Spraypoint routing tables follow every rule in §5 of the paper, with no forwarding loops, at both the paper's scale (*n* = 1,000, *d* = 64) and the scale of our main sweep (*n* = 256, *d* = 16). Path lengths and oversubscription match the paper's models and benchmarks. The one gap is edge-disjoint paths: we find over 50 for every pair, as the paper does, but over 60 for only 27% of non-adjacent pairs, where the paper reports half.

## Data

- **Program:** `spraypoint-verifier` in `test/spraypoint-verifier/`, run at commit `f6f823e`. It checks the routing tables directly and does not use ns-3.
- **Runs:** the paper's defaults (*n* = 1,000, *d* = 64, *p* = 4, *h* = 2) and the load sweep's network (*n* = 256, *d* = 16, *p* = 4, *h* = 2), both with seed 561, 2,000 sampled pairs for edge-disjoint paths, and 10 random matchings for oversubscription. The multi-seed oversubscription numbers come from the same solver in `bypass-model` (`05-bypass-model`, batch `2026-11-13_102950_bcb7193`), with no bypass.
- **Paper numbers:** from §7 (models), §9.1 (oversubscription, Figure 11) and §9.2 (edge-disjoint paths, Figure 12) of arXiv:2604.15261v3.

## Paper scale: n = 1,000, d = 64

| Check | Ours | Paper or model | Match |
| --- | --- | --- | --- |
| Routing-rule violations and loops | 0 | 0 | Yes |
| Longest path | 4 hops | At most 5 (§7.2) | Yes |
| Level sizes | WP0 = 64, WP1 = 256, IR = 679, OR = 0 | *d*, *pd*, the rest | Yes |
| Paths of 1 / 2 / 3 / 4 hops | 0.1 / 6.3 / 25.6 / 68.0% | 0.1 / 6.4 / 25.6 / 67.9% (Model 7.2) | Yes |
| Pairs with over 50 edge-disjoint paths | 100% | "almost all" | Yes |
| Pairs with over 60 edge-disjoint paths | 27% non-adjacent, 9% adjacent | "half" | No, lower |
| Edge-disjoint paths, median | 59 non-adjacent, 58 adjacent | Above 60 implied | Slightly lower |
| Oversubscription, worst matching | 3.12–3.30 (10 matchings); at most 3.30 over 30 matchings and 3 seeds | 3.25 (100 matchings); Model 7.3: 3.27 | Yes |

## Main sweep scale: n = 256, d = 16

| Check | Ours | Model | Match |
| --- | --- | --- | --- |
| Routing-rule violations and loops | 0 | 0 | Yes |
| Longest path | 5 hops | At most 5 | Yes |
| Paths of 1 / 2 / 3 / 4 / 5 hops | 0.4 / 5.9 / 25.1 / 67.9 / 0.7% | 0.4 / 6.3 / 25.0 / 66.5 / 1.8% (Model 7.2) | Close |
| Edge-disjoint paths, mean | 13.9 non-adjacent, 12.3 adjacent | 13.8 / 12.0 (Model 7.1) | Yes |
| Oversubscription, worst matching | 3.32–3.48 (10 matchings); at most 3.47 over 30 matchings and 3 seeds | 3.30 (Model 7.3) | Close |

This backs the claim in [ANALYSIS.md](ANALYSIS.md) that the 256-router network keeps the full-scale design's proportions. The paper's thresholds of 50 and 60 paths don't apply at *d* = 16, so we compare the mean with Model 7.1 instead.

## Notes

- **Edge-disjoint paths are the one gap.** The paper says Spraypoint finds over 60 paths for half the pairs; we find a median of 59. A likely cause is the topology: we build a uniform random regular graph, while the paper's fabric is quasi-random, built with ShuffleBoxes. We have checked only one seed so far.
- **The model is less exact at small scale.** At *n* = 256, 5-hop paths are 0.7% against the model's 1.8%. The paper's models assume a large network, and the paper itself notes that lower-order terms matter in some regimes.
- **Oversubscription assumes the best split.** Both the paper's 3.25 and our bracket come from a multi-commodity flow LP. If each hop instead splits traffic evenly over its ECMP next hops, the worst link carries about 11.5 times its share at the paper's scale (10.3 to 14.5 over 30 matchings; `ecmp_r` in the output). The paper doesn't report this, so it isn't a mismatch, but plain ECMP does not reach the LP's number. [bypass-modelling.tex](bypass-modelling.tex), Step 7, explains why: each pair overloads one of its own links into the destination.
- **Oversubscription is a bracket**, since the Garg–Koenemann solver is approximate. With the default step size of 0.05 the bracket is about 5% wide. `--epsilon=0.02` narrows it.

## Still to do

- Run edge-disjoint paths on more seeds, to see whether the gap is consistent.
- Run 100 matchings (`--matchings=100`) to match the paper's oversubscription setup exactly.

The bypass's own models are checked separately, by `bypass-model`: see [bypass-modelling.tex](bypass-modelling.tex).

## Reproducing

```bash
cmake -B build
cmake --build build --target spraypoint-verifier
./build/bin/spraypoint-verifier
./build/bin/spraypoint-verifier --nodes=256 --degree=16
```

`--help` lists the other options. `--matchings=0` skips oversubscription, the slowest check.
