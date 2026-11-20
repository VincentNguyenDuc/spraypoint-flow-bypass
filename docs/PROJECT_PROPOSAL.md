# Shortest-Path Bypass for Small Flows in Spraypoint

## Group Members

- Cheng Peng
- Vincent Nguyen
- Yusong Yan

## Summary

We will test whether letting small flows skip Spraypoint's spray step, and take the shortest path instead, lowers their tail latency on an RNG-style expander fabric without hurting anything else. We will build a packet-level simulation of an RNG topology running default Spraypoint, add a "small-flow bypass" variant, and compare the two across workloads and loads.

We will deliver three things:

- the simulator, with both routing schemes;
- small-flow latency distributions (p50, p99, p99.9);
- an analysis of what the bypass costs in large-flow throughput, fan-in, and hotspots on direct links.

## Background

RNG replaces the fat tree with a flat, quasi-random graph of ToR routers. Each router has *d* fabric links (*d* = 64 by default) and can reach most other routers in 4 hops. Spraypoint is the routing protocol that makes this graph usable on commodity switches. It routes every packet in two phases:

1. **Spray.** The source router ECMP-hashes the packet to any of its *d* neighbors. This is oblivious to the destination and to load, and it happens even when the source and destination are adjacent.
2. **Point.** For each destination *t*, every router has a level: WP0 (*t*'s neighbors), higher waypoint levels, an inner ring, or the outer ring. Each level forwards to a few chosen nodes in the level closer to *t*. Traffic therefore reaches *t* through many of its neighbors (fan-in) instead of collapsing onto the shortest path.

Spraypoint is implemented as a modified link-state protocol. The data plane uses two VRFs selected by ingress port: server-facing ports use the spray table, and fabric-facing ports use the pointing table. The default design gives a median of about 60 of 64 possible edge-disjoint paths per pair and a maximum path length of 5 hops.

## Motivation

Spraypoint is tuned for throughput, but much datacenter traffic is small, latency-sensitive messages such as RPCs, key-value lookups, and control traffic. For these, the tail of the flow completion time matters more than bandwidth.

Always spraying has three costs for a small message:

- **Extra hops.** If A and B are directly linked, a one-hop message usually takes 2 to 5 hops instead. Only about 1 in *d* sprays lands on B directly.
- **More queues.** Every extra hop is another queue where the packet can wait behind bulk traffic, and the tail is set by the worst of those queues.
- **Path variance.** The same pair sees different path lengths, which spreads out the latency distribution.

The paper reports a 15% higher median one-way latency than a fat tree (8.4 vs 7.1 us), but it does not measure small-flow tail latency. It also does not evaluate a separate path for latency-sensitive traffic. We want to know whether that gap leaves an easy win on the table.

The bypass is not obviously good, though. The paper argues that shortest-path routing between adjacent routers makes "almost all neighbors route the traffic back to s," congesting the direct link. A bypass for small flows could bring that hotspot back. Measuring this tradeoff is the point of the project.

## Research Question and Hypotheses

**Research question:** On an RNG fabric, does routing small flows on shortest paths, instead of spraying them, reduce their tail latency? What does it cost the rest of the traffic, and at what load does the cost outweigh the gain?

- **H1 (latency).** For small flows between adjacent routers, the bypass cuts p99 FCT relative to default Spraypoint, because they cross one queue instead of 2 to 5.
- **H2 (no free lunch at high load).** At low small-flow volume, large-flow throughput and destination fan-in will barely change. As the small-flow share or overall load grows, the direct links become hotspots. Past a crossover, the bypass makes tail latency worse for everyone, small flows included.
- **H3 (limited reach).** A bypass only for adjacent pairs helps few flows, since about *d*/(*n* - 1) of router pairs are adjacent (6.4% at *n* = 1,000, *d* = 64). Extending it to shortest paths for all pairs reaches more flows, but also raises the hotspot risk from H2.
- **H4 (overhead).** On commodity switches the bypass costs one extra table lookup, so the processing overhead is negligible next to queuing delay. We will argue this from the switch pipeline rather than measure it.

## Roadmap

| Milestone | Target date | What is done |
| --- | --- | --- |
| M1: Baseline working | Nov 2 | RNG topology generator and Spraypoint tables, checked in the prototype (path lengths, edge-disjoint paths, no loops). Spraypoint runs in the packet simulator with one workload, giving first FCT numbers. |
| M2: Bypass implemented, first results | Nov 16 | Bypass implemented. Small-flow p99 FCT compared with the baseline, giving a first answer to H1. |
| M3: Full evaluation | Nov 30 | Sweeps over load, small-flow share, size threshold and traffic pattern. Fan-in and hotspot analysis (H2), analysis (H3), and the crossover point where the bypass stops paying off. |

## Methods

We will use **ns-3**, a pure discrete-event network simulator, rather than Mininet and POX. Mininet's emulation of hosts, switches, and a control plane is unnecessary for the packet-level latency and queueing questions in this project, and would limit the scale and number of parameter sweeps we can run.

We will implement or adapt an ns-3 topology module for the RNG graph and add packet-forwarding models for default Spraypoint and the small-flow bypass variants. The simulation will model link rates, propagation delay, packet serialization, switch queues, and traffic generation. We will use it to collect flow completion times, large-flow throughput, destination fan-in, and per-link utilization/queue occupancy under the planned workloads and load sweeps.

Experiments will run on Ubuntu Linux 26.04 LTS using open-source ns-3 tooling. We will validate the baseline implementation against the properties reported for Spraypoint: path-length distribution, edge-disjoint-path count, absence of forwarding loops, and destination fan-in.
