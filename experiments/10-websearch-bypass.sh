#!/usr/bin/env bash
# Flow completion times with the shortest-path bypass on the WebSearch workload.  Compare
# with 10-websearch-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/10-websearch-settings.sh"

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
