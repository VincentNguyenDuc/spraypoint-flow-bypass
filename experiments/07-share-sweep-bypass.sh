#!/usr/bin/env bash
# Flow completion times with the shortest-path bypass under uniform traffic.  Compare
# with 07-share-sweep-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/07-share-sweep-settings.sh"

for seed in "${SEEDS[@]}"; do
    for small in "${SMALL_BYTES[@]}"; do
        for load in "${LOADS[@]}"; do
            run_experiment "shortest_small-${small}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                "${WORKLOAD[@]}" "--smallBytes=$small" "--load=$load" "--seed=$seed" \
                "--bypass=shortest"
        done
    done
done
finish_runs
