#!/usr/bin/env bash
# Flow completion times with no bypass under uniform traffic, across the small flows'
# share of bytes, loads and seeds.
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/07-share-sweep-settings.sh"

for seed in "${SEEDS[@]}"; do
    for small in "${SMALL_BYTES[@]}"; do
        for load in "${LOADS[@]}"; do
            run_experiment "small-${small}_load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
                "--smallBytes=$small" "--load=$load" "--seed=$seed"
        done
    done
done
finish_runs
