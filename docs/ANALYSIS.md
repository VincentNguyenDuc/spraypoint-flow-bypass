# Analysis: First Results

The small-flow bypass does what H1 predicts. Small flows between adjacent routers drop from 3.3 fabric hops to 1, their median FCT falls by about 45%, their p99 without timeouts falls by 20–33%, and half as many of them wait for a timeout. The shortest-path bypass also speeds up every other small flow, by about 24% at the median. Up to load 0.5, neither bypass shows the cost that H2 predicts. Large-flow throughput, fan-in and the busiest links barely move, because small flows carry only about 8% of the bytes. So H2 is still untested, not refuted.

These are M2 results. [TODO.md](TODO.md) lists what M3 adds.

## Data

- **Batch:** `experiments/results/remote/2026-11-07_120750_5770a94/`, run on the course server by `./REMOTE.sh` with the simulator as of commit `5770a94`. Results are not committed. `./REMOTE.sh fetch` copies them, and `./REPRODUCE.sh` regenerates them.
- **Main sweep:** `03-load-sweep`, with 256 routers of degree 16 and 4 hosts each. This keeps the proportions of the full-scale design (*n* = 1,000, *d* = 64), including the 6% of router pairs that are adjacent. It covers loads 0.1, 0.3 and 0.5, seeds 561–563, and all three modes, for 27 runs and about 1.27 million flows per mode.
- **Oversubscription sweep:** `04-oversubscription`, in batch `experiments/results/remote/2026-11-12_221557_cf0fc72/`, run with the same simulator at commit `cf0fc72`. It is the load sweep's network at load 0.5 with 6 and 8 hosts per router instead of 4, for seeds 561–563 and all three modes, which is 18 runs. More hosts make the fabric busier while each host link stays at load 0.5. Its numbers come from the same notebooks run with `SWEEP=04-oversubscription`, whose executed copies are in that batch's `analysis/` folder.
- **Second sweep:** `02-small-sweep`, with 64 routers of degree 8. It is the only sweep that reaches load 0.7. It has twice the share of adjacent pairs (11% of flows), covers 5 seeds, and has 60 runs.
- **Workload:** 90% of flows are 10 KB and 10% are 1 MB, between uniform random hosts. Each flow opens a new TCP connection, with a 1 ms minimum RTO. See the [README](../README.md) for the other settings.
- **Health:** every flow finished in every run, with no TTL or no-route drops, and all three modes run the same flows.
- **Where each number comes from:** the numbers below come from the committed notebooks [01-baseline.ipynb](../experiments/analysis/01-baseline.ipynb) and [02-bypass-comparison.ipynb](../experiments/analysis/02-bypass-comparison.ipynb), which were run on the load sweep. Numbers marked *small sweep* come from the same notebooks run with `SWEEP=02-small-sweep`.

## Reading the tail

A small flow that loses no packet finishes in under 300 µs. One that loses a packet waits at least the 1 ms minimum RTO, and a lost SYN or SYN-ACK costs the 10 ms handshake timeout. Each class's p99 therefore jumps from tens of microseconds to about 1,000 µs as soon as more than 1% of its flows hit a timeout.

The raw p99 is mostly a threshold test on the timeout rate. In the small sweep at load 0.5, the baseline p99 of small adjacent flows is 1,030, 68, 1,040, 68 and 1,030 µs across the five seeds. So we report three numbers:

- **Timeouts:** the share of small flows that waited for any timeout.
- **Clean p99:** the p99 of the flows that did not wait, which is queueing alone.
- **Raw p99:** shown alongside, because the proposal asks for it.

Lost handshakes account for 30–50% of small-flow timeouts.

## H1: The bypass cuts small-flow latency

**Load sweep, load 0.5**, pooled over 3 seeds:

| Small flows between | Mode | Fabric hops | p50 (µs) | Clean p99 (µs) | Timeouts | Raw p99 (µs) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Adjacent routers | No bypass | 3.30 | 16.9 | 49.0 | 1.32% | 1,040 |
| | Adjacent bypass | 1.00 | 9.2 | 38.5 | 0.63% | 47 |
| | Shortest-path bypass | 1.00 | 9.3 | 38.9 | 0.65% | 47 |
| Other routers | No bypass | 3.64 | 18.1 | 50.3 | 1.38% | 1,050 |
| | Adjacent bypass | 3.64 | 18.0 | 50.5 | 1.34% | 1,050 |
| | Shortest-path bypass | 2.36 | 13.7 | 44.6 | 0.94% | 70 |

**Across loads**, as the mean change per seed against the baseline on the same flows. Each range runs from the smallest change to the largest.

| Flows | Bypass | Load | p50 | Clean p99 | Timeouts (points) | Seeds with fewer timeouts |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Small, adjacent routers | Either | 0.1 | −43% | −33% (−27 to −38) | −0.02 | 1 of 3 |
| | | 0.3 | −44% | −23 to −25% | −0.10 to −0.17 | 3 of 3 |
| | | 0.5 | −45% | −21% (−17 to −24) | −0.67 to −0.69 | 3 of 3 |
| Small, other routers | Shortest-path | 0.1 | −25% | −16% | 0.00 | 1 of 3 |
| | | 0.3 | −22% | −12% | −0.10 | 3 of 3 |
| | | 0.5 | −24% | −11% (−10 to −12) | −0.44 | 3 of 3 |

At load 0.1, timeouts are too rare to count, at under 0.02% of flows.

- **The gain is what H1 predicts, and it is consistent.** In every seed and at every load, the clean p99 of small adjacent flows drops by at least 17%. The bypass makes 91–100% of these flows individually faster, and the median flow takes 0.6 times its baseline time.
- **The gain comes from crossing fewer queues.** A small adjacent flow goes from 3.3 hops to 1, so it crosses one router queue instead of 2 to 5. That cuts both its queueing delay and its chance of being dropped.
- **The raw p99 overstates the gain at load 0.5.** It falls 22× (1,040 → 47 µs) because the timeout rate fell below 1%. The queueing tail falls by 21%.
- **The two bypasses treat adjacent flows the same.** Between neighbors, the shortest path is the direct link, so both modes give identical paths and nearly identical FCTs.

## H3: The shortest-path bypass reaches more flows

Only 6.2% of small flows are between adjacent routers, so the adjacent bypass hardly moves the numbers for small flows as a whole. At load 0.5, their p50 goes from 18.0 to 17.6 µs and their raw p99 from 1,048 to 1,044 µs.

The shortest-path bypass shortens the remaining 94% from 3.64 to 2.36 hops. That takes all small flows to a p50 of 13.5 µs and a raw p99 of 67 µs, because their timeout rate falls below 1%. It makes 75–95% of small flows between non-adjacent routers individually faster, with a median flow taking 0.78 times its baseline time.

H3 also predicts that this wider reach raises the hotspot risk. The next section finds no sign of that yet.

## H2: No measurable cost yet

**Load sweep, load 0.5:**

| | No bypass | Adjacent bypass | Shortest-path bypass |
| --- | ---: | ---: | ---: |
| Large-flow goodput, mean (Gbps) | 20.0 | 20.0 | 20.4 |
| Large-flow goodput, slowest 1% (Gbps) | 5.56 | 5.48 | 5.68 |
| Large-flow p99 FCT (µs) | 1,440 | 1,460 | 1,410 |
| Mean link utilization | 0.457 | 0.456 | 0.443 |
| Busiest link / mean link | 1.95 | 1.96 | 1.95 |
| Links whose queue ever filled | 44% | 44% | 41% |
| Queue drops per run | 124,000 | 123,000 | 109,000 |
| Busiest neighbor's share of a destination's traffic | 0.12 | 0.12 | 0.12 |

- **Large flows don't slow down.** The shortest-path bypass raises mean large-flow goodput by 1.8–2.1% in every seed at load 0.5. Bypassed packets cross fewer links, so the fabric carries about 3% fewer bytes for the same offered load, with fewer full queues and drops.
- **Fan-in doesn't change.** A destination still receives traffic from all 16 neighbors, and its busiest neighbor carries 12% of it in every mode.
- **No hotspots form.** The most any link's utilization rises under a bypass is 6 points (adjacent) or 4.7 points (shortest). The 1% of links that gain the most go from 0.47 to 0.50 utilization (adjacent) and from 0.42 to 0.44 (shortest). Their queues fill a little more often, 48% → 50% and 33% → 38% of them, but they stay far below the baseline's busiest link, at 0.89.
- **Two weak signs of cost, both with the adjacent bypass:**
  - In the load sweep at load 0.5, the slowest 1% of large flows lose 0.7–2.9% of their goodput in every seed.
  - In the *small sweep* at load 0.5, small flows that are not bypassed wait for a timeout slightly more often (1.12% → 1.20%), in all 5 seeds. That network has twice the share of adjacent pairs, and the load sweep shows the opposite (−3%).

  Both are consistent with direct links getting a little busier, but both are small.
- **No crossover so far.** By every measure here, the bypass is no worse than the baseline at every load, nor with the busier fabric of `04-oversubscription` (below).

**Why H2 is untested, not refuted.** The bypass only moves small flows, which carry 8% of the bytes, and small flows between adjacent routers carry 0.5%. That is too little traffic to create the hotspot the paper warns about. Testing H2 needs workloads where small flows carry more of the bytes, as [TODO.md](TODO.md) lists.

## High load: the small sweep at 0.7

At load 0.7, more than 2% of small flows wait for a timeout in every mode, and more than 1% wait for the 10 ms handshake timeout. So the raw p99 of every small-flow class is about 10,000 µs regardless of mode. The bypass still cuts timeouts:

- Small flows between adjacent routers go from 2.86% to 2.16% (adjacent bypass) and 2.39% (shortest).
- Other small flows go from 3.21% to 2.63% (shortest).

The bypass still lowers the clean p99 by 7–12%. At this load the tail is set by losses, so the bypass can't rescue it.

## A busier fabric: 6 and 8 hosts per router

`04-oversubscription` keeps every host link at load 0.5 and adds hosts, so the fabric between routers carries more. The 4-host column is the load sweep at load 0.5. All numbers pool 3 seeds.

| | 4 hosts | 6 hosts | 8 hosts |
| --- | ---: | ---: | ---: |
| Mean fabric link utilization | 0.46 | 0.65 | 0.77 |
| Links whose queue ever filled | 44% | 77% | 89% |
| Queue drops per run | 124,000 | 558,000 | 1,560,000 |

At 8 hosts the fabric is saturated. The busiest links run at 0.99, and the network takes 44–49 ms to drain flows that stopped arriving at 4 ms, against 14 ms with 6 hosts. This is past any realistic operating point, so the sweep runs from a moderately busy fabric to an overloaded one.

**Small flows: the bypass helps more as the fabric gets busier.**

| Small flows between | Mode | Timeouts, 4 / 6 / 8 hosts | Clean p99 (µs), 4 / 6 / 8 hosts |
| --- | --- | --- | --- |
| Adjacent routers | No bypass | 1.32 / 4.08 / 8.97% | 49 / 58 / 77 |
| | Adjacent bypass | 0.63 / 1.25 / 2.53% | 39 / 32 / 28 |
| | Shortest-path bypass | 0.65 / 1.22 / 2.39% | 39 / 33 / 29 |
| Other routers | No bypass | 1.38 / 4.58 / 9.96% | 50 / 62 / 83 |
| | Adjacent bypass | 1.34 / 4.53 / 9.78% | 51 / 62 / 83 |
| | Shortest-path bypass | 0.94 / 2.55 / 5.52% | 45 / 47 / 55 |

- With 8 hosts, the bypass cuts timeouts of small flows between adjacent routers 3.6×, and their clean p99 from 77 to 28 µs. Every seed agrees, at both 6 and 8 hosts.
- The shortest-path bypass nearly halves timeouts of the other small flows, and cuts their clean p99 by 25% (6 hosts) and 34% (8 hosts).
- Under the bypass, the clean p99 of adjacent flows *falls* as the fabric gets busier. That is likely survivorship, not faster queues: a busier fabric turns more of the slowest flows into timeouts, which leave the clean group.

**Other traffic: still no cost, and large flows gain.**

| | 4 hosts | 6 hosts | 8 hosts |
| --- | ---: | ---: | ---: |
| Large-flow goodput, no bypass (Gbps) | 20.0 | 13.1 | 8.0 |
| Change with the adjacent bypass | +0.2% | +0.5% | +0.8% |
| Change with the shortest-path bypass | +1.8–2.1% | +5.2–5.4% | +7.3–7.7% |
| Other small flows' timeouts, adjacent bypass vs none | 1.38 → 1.34% | 4.58 → 4.53% | 9.96 → 9.78% |

- **Large flows get faster, more so as the fabric fills.** The shortest-path bypass cuts small flows from 3.64 to 2.37 hops, so the fabric carries fewer bytes for the same traffic, and it drops 17–18% fewer packets. The gain is in every seed.
- **Small flows the adjacent bypass doesn't touch don't suffer either.** Their timeouts fall slightly, in every seed.
- **The bypass does load some links more.** At 8 hosts, the 1% of links it loads most go from 0.68 to 0.75 utilization (adjacent) and 0.69 to 0.75 (shortest), and their queues fill more often: 81% → 90% and 80% → 87%. But the network as a whole gains more than these links lose.
- **Fan-in doesn't change.** Each destination still receives from all 16 neighbors, and its busiest neighbor carries 11%.
- **The slowest 1% of large flows can't be compared here.** At 6 and 8 hosts, each mode's p1 goodput is 0.79 Gbps: a 1 MB flow that lost its handshake and waited the 10 ms timeout.

**Why still no cost?** A busier fabric isn't enough on its own. With uniform traffic and small flows at 8% of the bytes, moving them to shorter paths always removes more load than it concentrates. The flow-level models in [bypass-modelling.tex](bypass-modelling.tex) agree: on this 256-router network, the bypass only costs throughput once small flows carry about 20% of the bytes (10–20% for the shortest-path bypass), and then only under worst-case traffic. `06-permutation` tests that regime in ns-3.

## H4: Overhead

This is not measured, as the proposal planned. H4 is argued from the switch pipeline and is still to be written: see [TODO.md](TODO.md).

## Limitations

- **Drop-tail queues with no ECN.** Losses, not queueing, set the tail. A DCTCP-style signal would likely shrink the timeout rates and make the clean p99 the main tail.
- **A new connection for every flow.** This makes lost handshakes 30–50% of timeouts. Persistent connections would remove most of them.
- **Timeouts are inferred from FCT, not counted.** This is reliable for small flows, since none take between 0.5 and 1 ms without one, but it doesn't work for large flows.
- **Small scale.** The study uses 3 seeds of 4 ms each, with a 256-router network instead of 1,000 routers, and only the uniform traffic pattern. There are no confidence intervals yet, so this write-up relies on consistency across seeds.
- **A fixed flow-size mix**, with small flows at 8% of bytes. This is why H2 is still untested.

## Reproducing

To refresh the committed notebooks from a batch:

```bash
./REMOTE.sh fetch
cd experiments/analysis
RESULTS_MACHINE=remote SWEEP=03-load-sweep .venv/bin/jupyter nbconvert \
    --to notebook --execute --inplace 01-baseline.ipynb 02-bypass-comparison.ipynb
```

Set `SWEEP=02-small-sweep` for the small-sweep numbers. `./REPRODUCE.sh` also writes executed copies of both notebooks for both sweeps into the batch's `analysis/` folder.
