#!/usr/bin/env bash
# Reproduces everything from a fresh clone: builds the programs, runs all tests, runs
# every experiment, and regenerates the analysis notebooks.
#
# It stops at the first failure.  Rerunning is safe and continues where it stopped,
# since finished experiment runs are skipped.  The first build also fetches and builds
# ns-3, and the load sweeps take hours one run at a time.  Set JOBS to run several at
# once, after checking that the machine has the memory: see each run's resources.log.
set -euo pipefail

cd "$(dirname "$0")"

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
experiments/tcp-baseline-smoke.sh
experiments/tcp-bypass-smoke.sh
experiments/tcp-baseline-load-sweep.sh
experiments/tcp-bypass-load-sweep.sh

step "Analysis"
if [ ! -x analysis/.venv/bin/jupyter ]; then
    python3 -m venv analysis/.venv
    analysis/.venv/bin/pip install --quiet -r analysis/requirements.txt
fi
for notebook in analysis/*.ipynb; do
    analysis/.venv/bin/jupyter nbconvert --to notebook --execute --inplace \
        --ExecutePreprocessor.timeout=3600 "$notebook"
done

step "Done"
echo "Results are in experiments/results/, and the notebooks in analysis/ show the analysis."
