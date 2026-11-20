#!/usr/bin/env bash
# Flow completion times with the shortest-path bypass as the small flows get larger.
# Compare with 09-small-size-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/09-small-size-settings.sh"

for bypass in shortest; do
    for seed in "${SEEDS[@]}"; do
        for small in "${SMALL_BYTES[@]}"; do
            for load in "${LOADS[@]}"; do
                run_experiment "${bypass}_small-${small}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                    "${WORKLOAD[@]}" "--smallBytes=$small" "--smallThreshold=$small" "--load=$load" \
                    "--seed=$seed" "--bypass=$bypass"
            done
        done
    done
done
finish_runs
