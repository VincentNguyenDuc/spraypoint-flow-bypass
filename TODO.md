# TODO

## Decisions to make

- [ ] **Headline tail metric.** The p99 mostly shows whether more than 1% of flows lost a packet and waited for a timeout, since one timeout (≥ 1 ms) dwarfs queueing (tens of µs). Proposal: report the timeout rate and the p99 of flows with no timeout ("clean p99"), with the raw p99 alongside.
- [ ] **Lost SYNs.** About 40% of small-flow timeouts are lost handshakes waiting for the 10 ms connection timeout, because every flow opens a new connection. Keep this, reuse connections, or report handshake losses separately?
- [ ] **Congestion signal.** Queues only drop when full, with no ECN, so losses dominate the tail. Add an ECN or DCTCP variant, or state this as a limitation?
- [ ] **Which flows count as small.** H2 sweeps the cutoff on CDF workloads. `workload_cdf.py` computes expected flow/byte coverage and `h3_report.py` describes matched routing tradeoffs. Threshold selection still needs completed results and a common population/objective across cutoffs.

## Experiments to add (M3)

- [ ] **Make H2 testable.** The matrix in `04-h2-sweep.sh` covers small-flow fractions, sizes, and WebSearch/Hadoop cutoffs. `experiments/run-h2.sh` uses a global CPU/memory-admitted queue over the frozen matrix and refreshes `analysis/H2.md` per batch. Keep open until all runs and their analysis finish; memory-deferred runs remain pending.
- [ ] **Find the crossover.** `03-load-sweep` now includes loads 0.7 and 0.9; the H2 report compares modes on identical flows and marks exploratory crossover candidates. Keep open until the extended sweep finishes and is reviewed.
- [ ] **Timeout sensitivity.** Rerun a slice with other `--minRtoUs` values, which also scales the handshake timeout.
- [ ] **Traffic pattern.** `--pattern` now supports reproducible uniform, permutation, hotspot, and asynchronous incast; pure generator/regression tests pass. The comparative pattern sweep remains pending. See `src/tcp/traffic-pattern.md` for normalization and listener limits.
- [ ] **More seeds** where a p99 rests on fewer than about 100 flows (the `beyond_p99` column).

## Simulator changes

- [x] **Record drops per link** in `links.csv`, separating fabric links, downlinks to hosts, and host NICs. The congested `tcp-port-drops` test passes with no unattributed drops and reconciles per-port counters with the summary. H2 analysis repeats this reconciliation for every completed run.
- [x] **Record retransmissions per flow** in new `flows.csv` outputs: data attempts, exact data-RTO handler events, RTO/fast-recovery/other categories, and SYN/SYN-ACK retries. The passive socket observer and stock-TCP parity/lifecycle tests pass. Handshake timer expirations remain explicitly unavailable (blank), FIN retries are excluded, and existing frozen H2 outputs cannot gain these counters. See `src/tcp/tcp-diagnostics.md`; handshake policy remains a separate decision.

## Analysis

- [ ] Bootstrap confidence intervals for the p99s and p99 ratios. Paired seed/topology-cluster tooling and synthetic tests are implemented in `experiments/analysis/bootstrap.py`; applying it to completed H2 results and assessing few-seed/tail-sample limitations remain pending.
- [ ] H3: compare the adjacent and shortest bypasses over all small flows. Validated `h3_report.py` tooling and synthetic tests are implemented; apply it to stable, completed H2 results. It reports per-seed all-small quantiles and shortest-versus-adjacent reach/gain, with no cross-cutoff optimality claim.
- [x] H4: write up the switch-pipeline argument for the overhead ([OVERHEAD.md](OVERHEAD.md)). The argument is conditional on target resources; exact stage/latency cost and hardware validation remain unmeasured.
- [ ] Check the baseline against the paper: path lengths, fan-in, and the 8.4 µs median one-way latency

## Further ideas

- [ ] **Priority queue for small flows.** Routers serve small-flow packets on a priority queue, so they skip queues of large-flow packets and get dropped less.
- [ ] **Where the flow size comes from.** The simulator tells the sender each flow's size up front; real hosts often don't know it. Try app hints or predicting it from history, and compare with knowing it up front.
- [ ] **Packet-level Spraypoint + random graph + RDMA** (maybe a different project). Spray each packet instead of each flow, running RDMA on random graph?
