#!/usr/bin/env bash
# Flow completion times with each small-flow bypass under permutation traffic.  Compare
# with 06-permutation-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/06-permutation-settings.sh"

for bypass in adjacent shortest; do
    for seed in "${SEEDS[@]}"; do
        for small in "${SMALL_BYTES[@]}"; do
            for load in "${LOADS[@]}"; do
                run_experiment "${bypass}_small-${small}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                    "${WORKLOAD[@]}" "--smallBytes=$small" "--load=$load" "--seed=$seed" \
                    "--bypass=$bypass"
            done
        done
    done
done
finish_runs
