#!/usr/bin/env bash
# Runs REPRODUCE.sh on a server over SSH, using the settings in .env (see .env.example).
#
#   ./REMOTE.sh start    send the current commit to the server and start REPRODUCE.sh there
#   ./REMOTE.sh start --new    the same, but in a new batch of results
#   ./REMOTE.sh status   show whether it is running, and the end of its log
#   ./REMOTE.sh logs     follow its log; Ctrl-C stops following, not the run
#   ./REMOTE.sh stop     stop the run; start again later to continue where it stopped
#   ./REMOTE.sh fetch    copy the server's results into experiments/results/remote/
#   ./REMOTE.sh shell    open a shell in the project folder on the server
#
# Only committed code is sent, so every result matches a commit.  The server's results
# go to experiments/results/remote/<batch>/ (see experiments/batch.sh), so they never
# mix with local ones.  The run keeps going after you disconnect, and its log is
# experiments/results/reproduce.log on the server.
set -euo pipefail

cd "$(dirname "$0")"

if [ ! -f .env ]; then
    echo "error: no .env; copy .env.example to .env and fill it in" >&2
    exit 1
fi
set -a
source .env
set +a

: "${REMOTE_HOST:?set REMOTE_HOST in .env}"
: "${REMOTE_USER:?set REMOTE_USER in .env}"
REMOTE_PORT=${REMOTE_PORT:-22}
REMOTE_PASSWORD=${REMOTE_PASSWORD:-}
REMOTE_DIR=${REMOTE_DIR:-cse561-project}
REMOTE_JOBS=${REMOTE_JOBS:-1}
REMOTE_SETUP=${REMOTE_SETUP:-}

SSH_OPTIONS=(-p "$REMOTE_PORT" -o StrictHostKeyChecking=accept-new -o ServerAliveInterval=60)
SSH=(ssh "${SSH_OPTIONS[@]}")
if [ -n "$REMOTE_PASSWORD" ]; then
    if ! command -v sshpass > /dev/null; then
        echo "error: a password in .env needs sshpass: brew install sshpass" >&2
        exit 1
    fi
    # sshpass reads the password from SSHPASS, which keeps it out of the process list.
    export SSHPASS=$REMOTE_PASSWORD
    SSH=(sshpass -e "${SSH[@]}")
    SSH_OPTIONS+=(-o PreferredAuthentications=password,keyboard-interactive -o PubkeyAuthentication=no)
fi
TARGET=$REMOTE_USER@$REMOTE_HOST

# remote <script>: runs a bash script in the project folder on the server.
remote() {
    "${SSH[@]}" "$TARGET" bash -s <<EOF
set -eo pipefail
$REMOTE_SETUP
set -u
dir=$(printf %q "$REMOTE_DIR")
dir=\${dir/#\~/\$HOME}
mkdir -p "\$dir"
cd "\$dir"
pidfile=experiments/results/.reproduce.pid
log=experiments/results/reproduce.log
running() { [ -f "\$pidfile" ] && kill -0 "\$(cat "\$pidfile")" 2> /dev/null; }
# A block is read in full before it runs, so the run started below can't disturb the
# reading of this script from stdin.
{
$1
}
exit
EOF
}

start() {
    local new_batch=
    case "${1:-}" in
        "") ;;
        --new) new_batch=1 ;;
        *)
            echo "error: unknown option $1" >&2
            exit 1
            ;;
    esac
    if [ -n "$(git status --porcelain)" ]; then
        echo "warning: uncommitted changes are not sent; the server runs $(git rev-parse --short HEAD)" >&2
    fi
    bundle=$(mktemp)
    trap 'rm -f "$bundle"' EXIT
    git bundle create --quiet "$bundle" HEAD

    echo "Sending $(git rev-parse --short HEAD) to $TARGET:$REMOTE_DIR"
    remote "
if running; then
    echo 'error: a run is already going; stop it first' >&2
    exit 1
fi
missing=
for tool in git cmake make c++ python3; do
    command -v \$tool > /dev/null || missing=\"\$missing \$tool\"
done
if [ -n \"\$missing\" ]; then
    echo \"error: the server is missing:\$missing\" >&2
    exit 1
fi

base64 -d > .push.bundle <<'BUNDLE'
$(base64 < "$bundle")
BUNDLE
[ -d .git ] || git init --quiet
git fetch --quiet .push.bundle HEAD
git checkout --quiet --force --detach FETCH_HEAD
rm .push.bundle

mkdir -p experiments/results
RESULTS_MACHINE=remote NEW_BATCH=$new_batch JOBS=$(printf %q "$REMOTE_JOBS") \\
    nohup ./REPRODUCE.sh >> \"\$log\" 2>&1 < /dev/null &
echo \$! > \"\$pidfile\"
echo \"Started on \$(hostname) with JOBS=$REMOTE_JOBS; follow it with ./REMOTE.sh logs\"
"
}

status() {
    remote '
if running; then
    echo "Running (pid $(cat "$pidfile"))"
else
    echo "Not running"
fi
if [ -f "$log" ]; then
    echo "--- end of $log"
    tail -n 20 "$log"
fi
'
}

logs() {
    remote 'touch "$log"; exec tail -n 50 -f "$log"'
}

stop() {
    remote '
if ! running; then
    echo "Not running"
    exit 0
fi
stop_tree() {
    local child
    for child in $(pgrep -P "$1" || true); do
        stop_tree "$child"
    done
    kill "$1" 2> /dev/null || true
}
stop_tree "$(cat "$pidfile")"
echo "Stopped; ./REMOTE.sh start continues where it stopped"
'
}

fetch() {
    local dir=$REMOTE_DIR
    case "$dir" in
        /*) ;;
        "~/"*) dir=${dir#\~/} ;;
    esac
    # A plain space join, not printf %q: macOS's openrsync splits this on spaces without
    # removing shell quoting, so an escaped comma would reach ssh as a backslash.
    local rsh="ssh ${SSH_OPTIONS[*]}"
    [ -n "$REMOTE_PASSWORD" ] && rsh="sshpass -e $rsh"
    # Runs still in progress are left out.
    mkdir -p experiments/results/remote
    rsync -az --exclude '*.partial/' --exclude .failures -e "$rsh" \
        "$TARGET:$dir/experiments/results/remote/" experiments/results/remote/
    echo "Copied the server's results into experiments/results/remote/"
}

shell() {
    local dir
    dir=$(printf %q "$REMOTE_DIR")
    "${SSH[@]}" -t "$TARGET" "cd ${dir/#\\~/\$HOME} && exec \$SHELL -l"
}

case "${1:-}" in
    start) start "${@:2}" ;;
    status | logs | stop | fetch | shell) "$1" ;;
    *)
        sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//' >&2
        exit 1
        ;;
esac
