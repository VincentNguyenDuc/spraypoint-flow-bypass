#!/usr/bin/env bash
# Flow completion times with each small-flow bypass on a small network.  Compare with
# 02-small-sweep-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$(dirname "$0")/02-small-sweep-settings.sh"

for bypass in adjacent shortest; do
    for seed in "${SEEDS[@]}"; do
        for load in "${LOADS[@]}"; do
            run_experiment "${bypass}_load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
                "--load=$load" "--seed=$seed" "--bypass=$bypass"
        done
    done
done
finish_runs
