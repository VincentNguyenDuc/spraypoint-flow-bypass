#!/usr/bin/env bash
# Reproduces everything from a fresh clone: builds the programs, runs all tests, runs
# every experiment, and regenerates the analysis notebooks.
#
# It stops at the first failure.  Rerunning is safe and continues where it stopped,
# since finished experiment runs are skipped, unless the code changed or NEW_BATCH=1
# is set: then it starts a new batch of results (see experiments/batch.sh).  The first build also fetches and builds
# ns-3, and the load sweeps take hours one run at a time.  Set JOBS to run several at
# once, after checking that the machine has the memory: see each run's resources.log.
set -euo pipefail

cd "$(dirname "$0")"
ROOT=$PWD
# Every experiment below writes to this batch: see experiments/batch.sh.
source experiments/batch.sh
echo "Results: ${BATCH_DIR#"$ROOT/"}"

step() {
    echo
    echo "=== [$(date '+%H:%M:%S')] $1"
}

step "Build"
cmake -B build
cmake --build build --parallel

step "Tests"
ctest --test-dir build --output-on-failure

# Add new experiments here.
step "Experiments"
experiments/01-smoke-baseline.sh
experiments/01-smoke-bypass.sh
experiments/02-small-sweep-baseline.sh
experiments/02-small-sweep-bypass.sh
experiments/03-load-sweep-baseline.sh
experiments/03-load-sweep-bypass.sh
# The 8-host and load-0.7 runs need 6-8 GB each, so at most 4 run at once.
BIG_JOBS=$((${JOBS:-1} < 4 ? ${JOBS:-1} : 4))
JOBS=$BIG_JOBS experiments/04-oversubscription-baseline.sh
JOBS=$BIG_JOBS experiments/04-oversubscription-bypass.sh
# Each run of the model check already uses every core.
JOBS=1 experiments/05-bypass-model.sh
experiments/06-permutation-baseline.sh
experiments/06-permutation-bypass.sh
JOBS=$BIG_JOBS experiments/07-share-sweep-baseline.sh
JOBS=$BIG_JOBS experiments/07-share-sweep-bypass.sh

step "Analysis"
if [ ! -x experiments/analysis/.venv/bin/jupyter ]; then
    python3 -m venv experiments/analysis/.venv
    experiments/analysis/.venv/bin/pip install --quiet -r experiments/analysis/requirements.txt
fi
# run_notebook <sweep> <notebook>: executes a notebook on one sweep of this batch.
run_notebook() {
    RESULTS_DIR=$BATCH_DIR SWEEP=$1 experiments/analysis/.venv/bin/jupyter nbconvert \
        --to notebook --execute --ExecutePreprocessor.timeout=3600 --output-dir "$BATCH_DIR/analysis" \
        --output "$1-$(basename "$2")" "$2"
}
for sweep in 02-small-sweep 03-load-sweep 04-oversubscription 06-permutation 07-share-sweep; do
    run_notebook "$sweep" experiments/analysis/01-baseline.ipynb
    run_notebook "$sweep" experiments/analysis/02-bypass-comparison.ipynb
done
# Compares 06-permutation with the flow-level check in 05-bypass-model.
run_notebook 06-permutation experiments/analysis/03-model-check.ipynb

step "Done"
echo "Results and the executed notebooks are in ${BATCH_DIR#"$ROOT/"}/."
