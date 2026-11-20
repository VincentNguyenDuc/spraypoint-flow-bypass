#!/usr/bin/env bash
# Complete H2 evaluation, isolated from later edits to the working tree.
# RESULTS_BATCH=<name> JOBS=8 bash experiments/run-h2.sh [--dry-run] [--memory-gb N]
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
source experiments/batch.sh
SNAPSHOT=$BATCH_DIR/source

# Planning is read-only and never builds or runs the simulator. Existing batches
# retain their frozen simulator and experiment settings, including old snapshots.
for argument in "$@"; do
    if [[ $argument == --dry-run ]]; then
        PLAN_SOURCE=$ROOT
        if [[ -f $BATCH_DIR/snapshot.ready ]]; then PLAN_SOURCE=$SNAPSHOT; fi
        exec python3 "$ROOT/experiments/h2-queue.py" "$BATCH_DIR" --source "$PLAN_SOURCE" "$@"
    fi
done
mkdir -p "$BATCH_DIR"
exec 9> "$BATCH_DIR/.setup.lock"
flock -n 9 || { echo "Another process is preparing this batch" >&2; exit 1; }

if [[ ! -f $BATCH_DIR/snapshot.ready ]]; then
    if [[ -d $SNAPSHOT ]]; then
        echo "Incomplete snapshot at $SNAPSHOT; use a new RESULTS_BATCH." >&2
        exit 1
    fi
    cmake -S . -B build
    cmake --build build --parallel 2
    ctest --test-dir build -L quick --output-on-failure
    ctest --test-dir build -R '^tcp-port-drops$' --output-on-failure
    mkdir -p "$SNAPSHOT" "$BATCH_DIR/bin"
    tar -cf - src cmake CMakeLists.txt test experiments/*.sh experiments/workloads \
        experiments/analysis/common.py experiments/analysis/h2-report.py \
        README.md docs/TODO.md docs/ANALYSIS.md docs/OVERHEAD.md | tar -xf - -C "$SNAPSHOT"
    cp build/bin/tcp-experiment build/bin/tcp-bypass "$BATCH_DIR/bin/"
    mkdir -p "$BATCH_DIR/lib"
    cp third_party/ns-3-install/lib/libns3*.so "$BATCH_DIR/lib/"
    git diff --binary HEAD > "$BATCH_DIR/working-tree.patch"
    {
        git rev-parse HEAD
        printf 'code_commit=%s\n' "$CODE_COMMIT"
        printf 'jobs=%s\n' "${JOBS:-auto}"
        uname -sm
        sha256sum "$BATCH_DIR"/bin/*
        sha256sum "$BATCH_DIR"/lib/*
        find "$SNAPSHOT" -type f -print0 | sort -z | xargs -0 sha256sum
    } > "$BATCH_DIR/manifest.txt"
    printf '%s\n' "$CODE_COMMIT" > "$BATCH_DIR/snapshot.ready"
fi
flock -u 9
exec 9>&-

PYTHON=$ROOT/experiments/analysis/.venv/bin/python
test -x "$PYTHON" || { echo "Install experiments/analysis/requirements.txt in .venv first" >&2; exit 1; }
exec python3 "$ROOT/experiments/h2-queue.py" "$BATCH_DIR" --source "$SNAPSHOT" \
    --report-python "$PYTHON" "$@"
