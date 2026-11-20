#!/usr/bin/env bash
# Flow completion times with no bypass on the Hadoop workload, across small-flow cutoffs,
# loads and seeds.
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/11-hadoop-settings.sh"

for seed in "${SEEDS[@]}"; do
    for cutoff in "${CUTOFFS[@]}"; do
        for load in "${LOADS[@]}"; do
            run_experiment "cutoff-${cutoff}_load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
                "--smallThreshold=$cutoff" "--load=$load" "--seed=$seed"
        done
    done
done
finish_runs
