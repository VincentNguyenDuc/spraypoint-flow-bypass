#!/usr/bin/env bash
# Flow completion times with no bypass, across loads and seeds.
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/03-load-sweep-settings.sh"
read -r -a LOADS <<< "${EXPERIMENT_LOADS:-${LOADS[*]}}"
read -r -a SEEDS <<< "${EXPERIMENT_SEEDS:-${SEEDS[*]}}"

for seed in "${SEEDS[@]}"; do
    for load in "${LOADS[@]}"; do
        run_experiment "load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
            "--load=$load" "--seed=$seed"
    done
done
finish_runs
