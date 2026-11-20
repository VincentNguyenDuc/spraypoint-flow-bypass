#!/usr/bin/env bash
# Flow completion times with no bypass, with more hosts per router than 03-load-sweep.
source "$(dirname "$0")/common.sh"
source "$ROOT/experiments/04-oversubscription-settings.sh"

for hosts in "${HOSTS_PER_ROUTER[@]}"; do
    for seed in "${SEEDS[@]}"; do
        for load in "${LOADS[@]}"; do
            run_experiment "hosts-${hosts}_load-${load}_seed-$seed" "${NETWORK[@]}" "${WORKLOAD[@]}" \
                "--hostsPerRouter=$hosts" "--load=$load" "--seed=$seed"
        done
    done
done
finish_runs
