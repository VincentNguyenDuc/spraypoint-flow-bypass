#!/usr/bin/env bash
# Quick end-to-end check of each small-flow bypass on a small network.  Takes about
# 10 seconds.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"

NETWORK=(--nodes=64 --degree=8 --waypoints=4 --nextHops=2 --hostsPerRouter=2)
WORKLOAD=(--flows=0 --spanMs=1 --releaseDelayUs=500)

for bypass in adjacent shortest; do
    run_experiment "$bypass" "${NETWORK[@]}" "${WORKLOAD[@]}" --load=0.3 --seed=561 "--bypass=$bypass"
done
finish_runs
