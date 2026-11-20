#!/usr/bin/env bash
# Flow completion times with no bypass as more of the flows are small, across loads and
# seeds.
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/08-small-fraction-settings.sh"

for seed in "${SEEDS[@]}"; do
    for fraction in "${FRACTIONS[@]}"; do
        for load in $(loads_for "$fraction"); do
            run_experiment "fraction-${fraction}_load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
                "--smallFraction=$fraction" "--load=$load" "--seed=$seed"
        done
    done
done
finish_runs
