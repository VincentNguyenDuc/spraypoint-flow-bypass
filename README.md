# CSE 561: Shortest-Path Bypass for Small Flows in Spraypoint

This project tests whether routing small flows on shortest paths, instead of spraying them, lowers their tail latency on the fabric from *RNG: Flat Datacenter Networks at Scale*, and what that costs other traffic. See the [proposal](PROJECT_PROPOSAL.md).

The repository has an ns-3 simulator with two experiments:

- **Paper reproduction:** checks the routing against the paper.
- **TCP experiment:** compares flow completion times under default Spraypoint and the small-flow bypass.

The pinned official ns-3 dependency is downloaded automatically on first run.
