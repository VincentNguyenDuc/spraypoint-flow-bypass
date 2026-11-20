#!/usr/bin/env bash
# Flow completion times with the shortest-path bypass for each workload scenario.
# Compare with 08-workloads-baseline.sh, which runs the same flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/08-workloads-settings.sh"

for bypass in shortest; do
    for scenario in "${SCENARIOS[@]}"; do
        scenario_options "$scenario"
        for seed in "${SEEDS[@]}"; do
            for load in "${LOADS[@]}"; do
                run_experiment "${bypass}_${scenario}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                    "${WORKLOAD[@]}" "${SCENARIO_OPTIONS[@]}" "--load=$load" "--seed=$seed" \
                    "--bypass=$bypass"
            done
        done
    done
done
finish_runs
