#!/usr/bin/env bash
# Flow completion times with each small-flow bypass, with more hosts per router than
# 03-load-sweep.  Compare with 04-oversubscription-baseline.sh, which runs the same
# flows with no bypass.
PROGRAM_NAME=tcp-bypass
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/04-oversubscription-settings.sh"

for hosts in "${HOSTS_PER_ROUTER[@]}"; do
    for bypass in adjacent shortest; do
        for seed in "${SEEDS[@]}"; do
            for load in "${LOADS[@]}"; do
                run_experiment "${bypass}_hosts-${hosts}_load-${load}_seed-$seed" "${NETWORK[@]}" \
                    "${WORKLOAD[@]}" "--hostsPerRouter=$hosts" "--load=$load" "--seed=$seed" \
                    "--bypass=$bypass"
            done
        done
    done
done
finish_runs
