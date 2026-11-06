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
experiments/01-smoke-baseline.sh
experiments/01-smoke-bypass.sh
experiments/02-small-sweep-baseline.sh
experiments/02-small-sweep-bypass.sh
experiments/03-load-sweep-baseline.sh
experiments/03-load-sweep-bypass.sh

step "Analysis"
if [ ! -x experiments/analysis/.venv/bin/jupyter ]; then
    python3 -m venv experiments/analysis/.venv
    experiments/analysis/.venv/bin/pip install --quiet -r experiments/analysis/requirements.txt
fi
for notebook in experiments/analysis/*.ipynb; do
    experiments/analysis/.venv/bin/jupyter nbconvert --to notebook --execute --inplace \
        --ExecutePreprocessor.timeout=3600 "$notebook"
done

step "Done"
echo "Results are in experiments/results/, and the notebooks in experiments/analysis/ show the analysis."
