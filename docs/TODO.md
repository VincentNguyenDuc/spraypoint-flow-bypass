# TODO

Follow-up work from the [report](../report/). Use the shortest-path bypass for the new policy experiments and keep all three modes in the H2 evaluation.

## Experiments

- [ ] **Bypass policies.** Test the policies the results point to, first with `bypass-model` (minutes), then in ns-3 under permutation traffic:
  - skip pairs with a single shortest path;
  - cap each pair's bypassed share at about 1%, or bypass only a flow's first *N* bytes.
- [ ] **Real workloads and H2 crossover.** Finish the H2 matrix for small-flow fractions, sizes, and WebSearch/Hadoop cutoffs, including the extended uniform-load sweep through 0.9. Review matched results for crossover candidates and test the proposed 30 KB cutoff in a follow-up. Keep memory-deferred runs pending. See [the H2 workflow](../README.md#h2-workload-sweep).
- [ ] **Traffic patterns.** Compare hotspot and asynchronous incast with the workloads already covered by the report. See [traffic-pattern semantics](../src/tcp/traffic-pattern.md).
- [ ] **Timeout sensitivity.** Rerun a slice with other `--minRtoUs` values, which also scales the handshake timeout.
- [ ] **More seeds** where a p99 rests on fewer than about 100 flows (the `beyond_p99` column).

## Decisions

- [ ] **Lost handshakes.** The early FCT-based analysis attributed 30–50% of small-flow timeouts to connection setup. Use the measured SYN/SYN-ACK retries to revisit whether to reuse connections or report them separately.
- [ ] **Headline tail metric.** Report raw p99 alongside timeout rates and p99 for flows without timeouts. Assess whether the results hold across seeds.
- [ ] **Which flows count as small.** Assess the cutoff on completed CDF results using the same population and objective across cutoffs.
- [ ] **Congestion signal.** Queues only drop when full, with no ECN, so losses set the tail. Add an ECN or DCTCP variant, or state it as a limitation?

## Simulator

- [x] **Record drops per link** in `links.csv`, separating fabric links, router-to-host downlinks and host NICs. The congested reconciliation test checks the per-port counts against the summary.
- [x] **Record retransmissions per flow** in `flows.csv`, including exact data-RTO events and SYN/SYN-ACK retries. Handshake timer expirations remain unavailable. See [TCP diagnostics](../src/tcp/tcp-diagnostics.md).

## Analysis and write-up

- [ ] Apply the [paired bootstrap](../experiments/analysis/STATISTICS.md) to completed H2 results and assess the limits from few seeds and small tail samples. Extend the intervals to timeout rates and throughput kept.
- [ ] Apply the [H3 report](../experiments/analysis/H3_REPORT.md) to completed H2 results to compare adjacent and shortest-path bypasses across all small flows.
- [x] Write up the [H4 pipeline design](OVERHEAD.md). Stage and latency costs still require target compilation and hardware measurements.
- [ ] Check the baseline's median one-way latency against the paper's 8.4 µs

## Further ideas

- [ ] **Weighted ECMP.** Plain ECMP is about 3.5× from the paper's best-split oversubscription. Split in proportion to each next hop's downstream capacity, and measure how much of the gap it recovers, with `bypass-model` first.
- [ ] **Priority queue for small flows.** Routers serve small-flow packets first, so they skip queues of large-flow packets and are dropped less.
- [ ] **Where the flow size comes from.** The simulator tells the sender each flow's size up front; real hosts often don't know it. Try app hints or predicting it from history.
- [ ] **Packet-level spraying with RDMA** on a random graph (maybe a separate project).
