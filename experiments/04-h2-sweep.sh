#!/usr/bin/env bash
# Run one workload independently so the analysis scripts can compare its modes.
# Usage: 04-h2-sweep.sh <scenario> <baseline|bypass>
set -euo pipefail
scenario=${1:?specify an H2 scenario; see README}
mode=${2:?specify baseline or bypass}
case "$scenario" in
    fraction95) workload=(--smallFraction=0.95 --smallBytes=10000 --smallThreshold=10000) ;;
    fraction99) workload=(--smallFraction=0.99 --smallBytes=10000 --smallThreshold=10000) ;;
    size50k) workload=(--smallFraction=0.9 --smallBytes=50000 --smallThreshold=50000) ;;
    size100k) workload=(--smallFraction=0.9 --smallBytes=100000 --smallThreshold=100000) ;;
    web10k|web100k|web1m)
        case "$scenario" in web10k) cutoff=10000 ;; web100k) cutoff=100000 ;; web1m) cutoff=1000000 ;; esac
        workload=(--flowSizeCdf=experiments/workloads/websearch.cdf "--smallThreshold=$cutoff") ;;
    hadoop10k|hadoop100k|hadoop1m)
        case "$scenario" in hadoop10k) cutoff=10000 ;; hadoop100k) cutoff=100000 ;; hadoop1m) cutoff=1000000 ;; esac
        workload=(--flowSizeCdf=experiments/workloads/hadoop.cdf "--smallThreshold=$cutoff") ;;
    *) echo "Unknown scenario: $scenario" >&2; exit 2 ;;
esac
case "$mode" in
    baseline) modes=(none) ;;
    bypass) PROGRAM_NAME=tcp-bypass; modes=(shortest) ;;
    *) echo "Unknown mode: $mode" >&2; exit 2 ;;
esac
EXPERIMENT="04-h2-$scenario-$mode"
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/03-load-sweep-settings.sh"
# Restrict the workload sweep to the loads most useful for detecting congestion.
LOADS=(0.3 0.5 0.7)
read -r -a LOADS <<< "${EXPERIMENT_LOADS:-${LOADS[*]}}"
read -r -a SEEDS <<< "${EXPERIMENT_SEEDS:-${SEEDS[*]}}"
for bypass in "${modes[@]}"; do
    for seed in "${SEEDS[@]}"; do
        for load in "${LOADS[@]}"; do
            name="load-${load}_seed-$seed"
            options=()
            if [[ $bypass != none ]]; then
                name="${bypass}_$name"
                options=("--bypass=$bypass")
            fi
            run_experiment "$name" "${NETWORK[@]}" "${WORKLOAD[@]}" "${workload[@]}" \
                "--load=$load" "--seed=$seed" ${options[@]+"${options[@]}"}
        done
    done
done
finish_runs
