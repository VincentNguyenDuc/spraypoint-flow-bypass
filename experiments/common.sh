# Shared by the experiment scripts.  Source this file, call run_tcp once per run,
# then call finish_runs.  Results go to results/<script name>/<run name>/.
#
# Rerunning a script skips finished runs and retries failed ones.  Set JOBS to
# run several simulations at once; check memory use first, since each run has
# its own copy of the network.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
EXPERIMENT=$(basename "$0" .sh)
RESULTS=$ROOT/results/$EXPERIMENT
PROGRAM=$ROOT/build/bin/tcp-experiment
JOBS=${JOBS:-1}

mkdir -p "$RESULTS"
cd "$ROOT"

# The first build also fetches and builds ns-3, which takes several minutes.
if ! build_log=$(cmake -S . -B build 2>&1 && cmake --build build --target tcp-experiment 2>&1); then
    echo "$build_log" >&2
    echo "error: the build failed" >&2
    exit 1
fi

COMMIT=$(git rev-parse --short HEAD)
if [ -n "$(git status --porcelain)" ]; then
    COMMIT="$COMMIT (uncommitted changes)"
    echo "warning: the working tree has uncommitted changes" >&2
fi

case "$(uname)" in
    Darwin) TIME_ARGS=(-l) ;;
    *) TIME_ARGS=(-v) ;;
esac

FAILURES=$RESULTS/.failures
rm -f "$FAILURES"
touch "$FAILURES"
started=0
skipped=0

# Stops a process and everything it started.  Background jobs in a script ignore
# Ctrl-C, so without this, simulations would keep running after an interrupt.
stop_tree() {
    local child
    for child in $(pgrep -P "$1" || true); do
        stop_tree "$child"
    done
    kill "$1" 2> /dev/null || true
}

stop_runs() {
    echo "Interrupted; rerun this script to continue." >&2
    local job
    for job in $(jobs -p); do
        stop_tree "$job"
    done
    exit 130
}
trap stop_runs INT TERM

# run_tcp <run name> <program options...>
run_tcp() {
    local run=$1
    shift
    local directory=$RESULTS/$run
    local args
    args=$(printf '%s\n' "$@")

    if [ -f "$directory/summary.txt" ]; then
        if [ "$args" != "$(cat "$directory/args.txt")" ]; then
            echo "error: $directory has results from different options; move it aside" >&2
            exit 1
        fi
        echo "Skipping $run: already finished"
        skipped=$((skipped + 1))
        return
    fi

    while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
    started=$((started + 1))
    echo "Starting $run"
    (
        partial=$directory.partial
        rm -rf "$partial" "$directory.failed"
        mkdir -p "$partial"
        echo "$args" > "$partial/args.txt"
        {
            echo "commit: $COMMIT"
            echo "host: $(hostname)"
            echo "platform: $(uname -sm)"
            echo "started: $(date '+%Y-%m-%d %H:%M:%S')"
        } > "$partial/meta.txt"
        began=$SECONDS
        if /usr/bin/time "${TIME_ARGS[@]}" "$PROGRAM" "$@" "--outputDir=$partial" \
            > "$partial/output.log" 2> "$partial/resources.log"; then
            echo "wall_seconds: $((SECONDS - began))" >> "$partial/meta.txt"
            mv "$partial" "$directory"
            echo "Finished $run in $((SECONDS - began)) s"
        else
            mv "$partial" "$directory.failed"
            echo "$run" >> "$FAILURES"
            echo "FAILED $run; see the logs in $directory.failed/" >&2
        fi
    ) &
}

# Waits for all runs, reports the outcome, and exits non-zero if any failed.
finish_runs() {
    wait
    local failed
    failed=$(wc -l < "$FAILURES" | tr -d ' ')
    rm -f "$FAILURES"
    echo "$EXPERIMENT: started $started, skipped $skipped, failed $failed. Results: $RESULTS"
    [ "$failed" -eq 0 ]
}
