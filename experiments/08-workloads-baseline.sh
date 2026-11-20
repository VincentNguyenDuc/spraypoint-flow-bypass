#!/usr/bin/env bash
# Flow completion times with no bypass for each workload scenario, across loads and
# seeds.  Usually run through experiments/run-workloads.sh, which admits runs by memory.
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/08-workloads-settings.sh"

for scenario in "${SCENARIOS[@]}"; do
    scenario_options "$scenario"
    for seed in "${SEEDS[@]}"; do
        for load in "${LOADS[@]}"; do
            run_experiment "${scenario}_load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
                "${SCENARIO_OPTIONS[@]}" "--load=$load" "--seed=$seed"
        done
    done
done
finish_runs
