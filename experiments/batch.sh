# Chooses where results go: experiments/results/<machine>/<batch>/<script name>/<run>/,
# where a batch is named <date>_<time>_<code commit>.  Sourced by common.sh and
# REPRODUCE.sh, after they set ROOT.
#
# - <machine> is RESULTS_MACHINE: "local" unless set, and REMOTE.sh sets "remote".
#   Runs from different machines are never mixed, since the same seed can give
#   different flows on different platforms.
# - <code commit> is the last commit that changed the simulator or the experiment
#   settings, so commits to the notebooks or docs keep using the same batch.  With
#   uncommitted changes to those, it ends in "-dirty".
# - Runs continue the newest batch for the same code on this machine, so an interrupted
#   experiment picks up where it stopped.  Set NEW_BATCH=1 to start a new batch
#   anyway, or RESULTS_BATCH=<batch> to use a given one.  Uncommitted code always
#   starts a new batch, since the name can't tell two versions of it apart.

CODE_PATHS=(src cmake CMakeLists.txt 'experiments/[0-9]*.sh')
RESULTS_MACHINE=${RESULTS_MACHINE:-local}
CODE_COMMIT=$(git -C "$ROOT" log -1 --format=%h -- "${CODE_PATHS[@]}")
if [ -n "$(git -C "$ROOT" status --porcelain -- "${CODE_PATHS[@]}")" ]; then
    CODE_COMMIT="$CODE_COMMIT-dirty"
fi

if [ -z "${RESULTS_BATCH:-}" ]; then
    if [[ $CODE_COMMIT != *-dirty ]] && [ -z "${NEW_BATCH:-}" ]; then
        RESULTS_BATCH=$(
            cd "$ROOT/experiments/results/$RESULTS_MACHINE" 2> /dev/null &&
                ls -d -- *_"$CODE_COMMIT" 2> /dev/null | sort | tail -n 1
        ) || true
    fi
    RESULTS_BATCH=${RESULTS_BATCH:-$(date '+%Y-%m-%d_%H%M%S')_$CODE_COMMIT}
fi

# Exported so that every experiment a script starts writes to the same batch.
export RESULTS_MACHINE RESULTS_BATCH
BATCH_DIR=$ROOT/experiments/results/$RESULTS_MACHINE/$RESULTS_BATCH
