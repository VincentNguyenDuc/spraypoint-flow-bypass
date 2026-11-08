# TODO

## Decisions to make

- [ ] **Headline tail metric.** The p99 mostly shows whether more than 1% of flows lost a packet and waited for a timeout, since one timeout (≥ 1 ms) dwarfs queueing (tens of µs). Proposal: report the timeout rate and the p99 of flows with no timeout ("clean p99"), with the raw p99 alongside.
- [ ] **Lost SYNs.** About 40% of small-flow timeouts are lost handshakes waiting for the 10 ms connection timeout, because every flow opens a new connection. Keep this, reuse connections, or report handshake losses separately?
- [ ] **Congestion signal.** Queues only drop when full, with no ECN, so losses dominate the tail. Add an ECN or DCTCP variant, or state this as a limitation?
- [ ] **Which flows count as small.** Sweep the bypass cutoff (`--smallThreshold`) on a workload with many flow sizes (`--flowSizeCdf`) to find the best one.

## Experiments to add (M3)

- [ ] **Make H2 testable.** Small flows are only 8% of bytes, and small flows between adjacent routers only 1%, so the bypass moves too little traffic to create hotspots. Sweep `--smallFraction`, `--smallBytes` and `--smallThreshold`, and try the published workloads with `--flowSizeCdf=experiments/workloads/websearch.cdf` and `hadoop.cdf`.
- [ ] **Find the crossover.** `03-load-sweep` stops at load 0.5; add 0.7 and higher.
- [ ] **Timeout sensitivity.** Rerun a slice with other `--minRtoUs` values, which also scales the handshake timeout.
- [ ] **Traffic pattern.** The proposal promises a pattern sweep, but `--pattern` only supports `uniform` so far.
- [ ] **More seeds** where a p99 rests on fewer than about 100 flows (the `beyond_p99` column).

## Simulator changes

- [ ] **Record drops per link** in `links.csv`, separating fabric links from the downlinks to hosts. We only count drops in total, so we can't tell where flows lose packets, for example why bypassed adjacent flows still time out 0.7% of the time at load 0.5.
- [ ] **Record retransmissions per flow** in `flows.csv`, instead of inferring timeouts from FCT. The inference works for small flows but not for large ones.

## Analysis

- [ ] Bootstrap confidence intervals for the p99s and p99 ratios
- [ ] H3: compare the adjacent and shortest bypasses over all small flows, once the shortest-path runs are in
- [ ] H4: write up the switch-pipeline argument for the overhead
- [ ] Check the baseline against the paper: path lengths, fan-in, and the 8.4 µs median one-way latency

## Further ideas

- [ ] **Priority queue for small flows.** Routers serve small-flow packets on a priority queue, so they skip queues of large-flow packets and get dropped less.
- [ ] **Where the flow size comes from.** The simulator tells the sender each flow's size up front; real hosts often don't know it. Try app hints or predicting it from history, and compare with knowing it up front.
- [ ] **Packet-level Spraypoint + random graph + RDMA** (maybe a different project). Spray each packet instead of each flow, running RDMA on random graph?
