#!/usr/bin/env bash
# Checks the bypass models in docs/bypass-modelling.tex against the routing tables,
# with no ns-3: shortest distances, edge-disjoint shortest paths, and oversubscription
# under random matchings as the bypassed share f grows, with ECMP and with the LP.  Runs
# at the paper's scale and at the load sweeps' scale.  Each run uses every core, so
# leave JOBS at 1; the paper-scale runs take about 20 minutes each on 16 cores.
PROGRAM_NAME=bypass-model
source "$(dirname "$0")/common.sh"

# Finer steps around the knees: about 5% at the paper's scale and 10-20% at ours.
SHARES=0,0.01,0.02,0.03,0.04,0.05,0.06,0.08,0.1,0.15,0.2,0.25,0.3

for seed in 561 562 563; do
    run_experiment "n-256_d-16_seed-$seed" --nodes=256 --degree=16 --matchings=10 \
        "--shares=$SHARES" "--seed=$seed"
    run_experiment "n-1000_d-64_seed-$seed" --nodes=1000 --degree=64 --matchings=10 \
        "--shares=$SHARES" "--seed=$seed"
done
finish_runs
