#!/usr/bin/env bash
# Flow completion times with the shortest-path bypass on the Hadoop workload.  Compare
# with 11-hadoop-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/11-hadoop-settings.sh"

for bypass in shortest; do
    for seed in "${SEEDS[@]}"; do
        for cutoff in "${CUTOFFS[@]}"; do
            for load in "${LOADS[@]}"; do
                run_experiment "${bypass}_cutoff-${cutoff}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                    "${WORKLOAD[@]}" "--smallThreshold=$cutoff" "--load=$load" "--seed=$seed" \
                    "--bypass=$bypass"
            done
        done
    done
done
finish_runs
