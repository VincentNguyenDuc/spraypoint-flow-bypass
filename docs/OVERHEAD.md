# H4: Switch-pipeline cost of the small-flow bypass

The bypass can be expressed as a small amount of classification plus a route
selection override. This makes low processing overhead plausible **if the target
has the required match, ECMP and pipeline resources**. The stronger proposal
claim that every commodity switch incurs exactly one extra lookup, with negligible
latency, is not established by this repository. No hardware pipeline was compiled
or benchmarked for this write-up.

The H2 simulator models links, queues and TCP; its route-selection functions add
no simulated processing delay. H2 therefore measures the network effect of the
changed paths, not the cost of implementing their selection in a switch.

## What the implementation requires

These requirements follow from `BypassRouting::SelectNextHop` in
[bypass-routing.cc](../src/tcp-bypass/bypass-routing.cc), with local delivery handled
first by [SpraypointRouting::RouteInput](../src/tcp/spraypoint-routing.cc).

| Condition | Required decision |
| --- | --- |
| Destination host is on this router | Deliver to that host before choosing a fabric next hop. |
| Adjacent mode: marked packet, host-facing ingress, destination router is a neighbor | Override spraying and select the direct fabric port. |
| Adjacent mode: every other packet | Preserve the existing Spraypoint spray/point decision. In particular, fabric-ingress packets do not take this source-only override. |
| Shortest mode: marked packet at any router | Select one of the next hops that reduces the shortest-path distance to the destination. |
| Shortest mode: unmarked packet | Preserve Spraypoint. |

The required inputs are the destination router/prefix, ingress port class and a
small-flow mark. The simulator derives the last input from
`(IPv4 ToS & 0xfc) == 0x28`, preserving the ECN bits. A target can represent the
classification as one metadata bit after matching DSCP. A consistent flow hash is
needed for ECMP, so packets of a flow retain one path. Requests, responses and TCP
control packets need a consistent marking policy; marking only application data
would not reproduce this simulator.

This design assumes that the end host already knows a flow's size and marks it.
It does not ask the switch to predict total flow size or maintain a per-flow byte
counter to decide when to change paths. Those are different designs, with different
state, consistency and reordering costs. The simulator's `EgressPortTag` is
measurement metadata, not an extra wire header needed by the bypass.

## A logical implementation

RNG describes two ingress-selected VRFs: server ingress sprays, fabric ingress
points, and local destinations are delivered directly. This is the baseline
selection that our override must preserve. See the paper's distributed
implementation in [RNG, §5](https://arxiv.org/html/2604.15261v3#S5).

One possible implementation performs classification and an optional bypass-route
lookup before falling back to the baseline route:

```text
parse destination and DSCP; classify ingress port
if destination is local:
    deliver locally
else if adjacent_mode and small and host_ingress:
    if neighbor_override(destination) hits:
        select the direct port
    else:
        apply baseline route
else if shortest_mode and small:
    select the shortest-path ECMP group for destination
else:
    apply baseline route
```

This is a forwarding specification, not compiled P4 or a count of physical stages.
The [P4 language specification, §14.3](https://p4.org/wp-content/uploads/sites/53/p4-spec/docs/P4-16-v1.2.4.pdf)
defines the logical execution of match-action tables. It does not supply a latency
bound for the above program on an unspecified device. An ACL/PBR override, an
additional VRF selected by classification, and a programmable-table implementation
are possible target-specific mappings, not interchangeable hardware guarantees.

If a device can fold classification into an existing table or use spare resources
in its normal traversal, an extra logical decision need not imply another complete
packet pass. A dependent lookup may instead require an extra physical stage. If
resources are exhausted, a design might not compile or might require recirculation;
that case is outside the low-overhead argument. Hardware support, table widths,
action capacity, dependencies and existing occupancy must all be checked.

## Route and ECMP state

For this address plan, one prefix identifies each router's attached hosts. The
following counts are a straightforward design bound for **additional local state
on one router**, excluding the unchanged host-delivery entries and baseline tables:

| Mode | Destination entries | Next-hop representation |
| --- | ---: | --- |
| Adjacent | At most `d` | One direct port per adjacent router. |
| Shortest | At most `n - 1` | A group per remote destination, containing its strictly closer neighbors. |

For the main sweep (`n=256`, `d=16`) that is up to 16 adjacent overrides or 255
shortest-route entries per router. At the paper-scale configuration (`n=1000`,
`d=64`) it is 64 or 999. These are entry counts, not SRAM/TCAM byte estimates or
evidence that a particular switch has available capacity.

In the current shortest-path algorithm, a destination may have as many as `d`
eligible neighbors. A direct representation thus has an `O(nd)` upper bound on
membership references per router; it is not bounded by Spraypoint's configured
`nextHops=2`. Shared groups may reduce this cost. The actual group-size and member
limits are target-dependent. PSA separates route lookup from selection among group
members; see [PSA 1.1, §§7.11–7.12](https://p4lang.github.io/p4-spec/docs/PSA-v1.1.0.html).

The simulator's all-pairs BFS and `n × n` distance matrix in
[shortest-paths.cc](../src/tcp-bypass/shortest-paths.cc) construct routes. They should
not be interpreted as per-packet switch operations or as on-chip forwarding state
that every router must store verbatim. Control-plane route calculation and updates
are separate from the steady-state packet-processing question.

## What can be concluded about latency

Let `delta_i` be the additional processing delay at the ith visited switch. The
additional one-way processing delay on a packet is the sum of these `delta_i`.
For a target with a measured upper bound `delta`, a packet crossing `k` affected
switches has a bound `k * delta`. This bound is conditional on the same packet pass
and resource assumptions. Queueing delay, packet serialization, propagation and
TCP round trips must still be accounted for separately.

For perspective within this simulator, one removed fabric link eliminates its
configured 500 ns propagation delay before considering serialization and queueing.
That is a comparison scale, not a measurement of switch overhead. Neither a
particular value of `delta` nor a bound on FCT follows from counting table lookups.
At low load queueing may be very small, so processing cost should not be dismissed
merely because queueing dominates some congested runs.

Consequently, H4's support is conditional: the design uses a destination-based
override rather than a per-flow control-plane operation, and a normal pipeline
mapping could make its cost small compared with the saved network delay. The
unconditional assertion of exactly one extra lookup and negligible overhead on
commodity switches remains unverified.

## Evidence needed for a target-specific claim

1. Name the switch architecture and compiler version, and implement the baseline
   and each bypass with the required metadata and marking policy.
2. Inspect compiler/resource reports for table widths, entry capacity, ECMP groups
   and memberships, dependency stages and recirculation. Check the intended network
   scale together with the other tables that the switch must already support.
3. Verify local delivery, adjacent source-only behavior, marked shortest routing,
   unmarked fallback and stable ECMP selection with packet tests.
4. Compare baseline/bypass latency and forwarding rate using the same packet sizes,
   ports and paths. Separate the classification cost from a change in hop count.
5. Only then insert a measured `delta` into a sensitivity model or claim hardware
   overhead is negligible for the relevant workload.

No additional H2 run or shared environment change is required to make the above
distinction. Measurements and compiled target artifacts remain future work.
