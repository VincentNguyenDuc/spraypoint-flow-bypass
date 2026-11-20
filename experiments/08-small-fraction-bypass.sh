#!/usr/bin/env bash
# Flow completion times with the shortest-path bypass as more of the flows are small.
# Compare with 08-small-fraction-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/08-small-fraction-settings.sh"

for bypass in shortest; do
    for seed in "${SEEDS[@]}"; do
        for fraction in "${FRACTIONS[@]}"; do
            for load in $(loads_for "$fraction"); do
                run_experiment "${bypass}_fraction-${fraction}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                    "${WORKLOAD[@]}" "--smallFraction=$fraction" "--load=$load" "--seed=$seed" \
                    "--bypass=$bypass"
            done
        done
    done
done
finish_runs
