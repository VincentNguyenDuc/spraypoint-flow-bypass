#!/usr/bin/env bash
# Flow completion times with no bypass, across loads and seeds.
#
# The network keeps the full-scale design's proportions (n=1000, d=64): the same
# share of adjacent router pairs and of routers at each pointing level.  With 4
# hosts per router, fabric utilization is roughly equal to the load.
source "$(dirname "$0")/common.sh"

NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000)

for seed in 561 562 563; do
    for load in 0.1 0.3 0.5; do
        run_tcp "load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
            "--load=$load" "--seed=$seed"
    done
done
finish_runs
