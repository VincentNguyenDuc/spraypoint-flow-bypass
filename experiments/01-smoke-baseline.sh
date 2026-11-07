#!/usr/bin/env bash
# Quick end-to-end check of the TCP program with no bypass: a small network at
# two loads.  Takes about 10 seconds.  Run this first on a new machine.
source "$(dirname "$0")/common.sh"

NETWORK=(--nodes=64 --degree=8 --waypoints=4 --nextHops=2 --hostsPerRouter=2)
WORKLOAD=(--flows=0 --spanMs=1 --releaseDelayUs=500)

for load in 0.1 0.3; do
    run_experiment "load-$load" "${NETWORK[@]}" "${WORKLOAD[@]}" "--load=$load" --seed=561
done
finish_runs
