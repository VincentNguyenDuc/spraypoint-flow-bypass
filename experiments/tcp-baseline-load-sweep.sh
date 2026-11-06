#!/usr/bin/env bash
# Flow completion times with no bypass, across loads and seeds.
source "$(dirname "$0")/common.sh"
source "$(dirname "$0")/load-sweep-settings.sh"

for seed in "${SEEDS[@]}"; do
    for load in "${LOADS[@]}"; do
        run_experiment "load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
            "--load=$load" "--seed=$seed"
    done
done
finish_runs
