# TODO

What is still open, after the results in [ANALYSIS.md](ANALYSIS.md). New sweeps should run the shortest-path bypass only: the adjacent bypass's results so far are enough for H3.

## Experiments

- [ ] **Bypass policies.** Test the policies the results point to, first with `bypass-model` (minutes), then in ns-3 under permutation traffic:
  - skip pairs with a single shortest path;
  - cap each pair's bypassed share at about 1%, or bypass only a flow's first *N* bytes.
- [ ] **f under uniform traffic.** "No cost under uniform traffic" is only shown at f = 8.3%. `07-share-sweep` runs f = 8.3, 20, 30% at loads 0.5 and 0.7, with no bypass and the shortest-path bypass; analyze it when it finishes.
- [ ] **Real workloads.** Run the web search and Hadoop distributions (`--flowSizeCdf=experiments/workloads/websearch.cdf`, `hadoop.cdf`) with "small" at 30 KB and 100 KB (`--smallThreshold`), to show the realistic case directly.
- [ ] **Timeout sensitivity.** Rerun a slice with other `--minRtoUs` values, which also scales the handshake timeout.
- [ ] **More seeds** where a p99 rests on fewer than about 100 flows (the `beyond_p99` column).

## Decisions

- [ ] **Lost handshakes.** 30–50% of small-flow timeouts are lost connection setups, because every flow opens a new connection. Reuse connections, or report them separately?
- [ ] **Congestion signal.** Queues only drop when full, with no ECN, so losses set the tail. Add an ECN or DCTCP variant, or state it as a limitation?

## Simulator

- [ ] **Record drops per link** in `links.csv`, separating fabric links from the downlinks to hosts, to see where flows lose packets.
- [ ] **Record retransmissions per flow** in `flows.csv`, instead of inferring timeouts from completion time, which only works for small flows.

## Analysis and write-up

- [ ] Bootstrap confidence intervals for the p99s, timeout rates and throughput kept
- [ ] H4: write up the switch-pipeline argument for the overhead, including the shortest-path bypass's second routing table
- [ ] Check the baseline's median one-way latency against the paper's 8.4 µs

## Further ideas

- [ ] **Weighted ECMP.** Plain ECMP is about 3.5× from the paper's best-split oversubscription. Split in proportion to each next hop's downstream capacity, and measure how much of the gap it recovers, with `bypass-model` first.
- [ ] **Priority queue for small flows.** Routers serve small-flow packets first, so they skip queues of large-flow packets and are dropped less.
- [ ] **Where the flow size comes from.** The simulator tells the sender each flow's size up front; real hosts often don't know it. Try app hints or predicting it from history.
- [ ] **Packet-level spraying with RDMA** on a random graph (maybe a separate project).
