#!/usr/bin/env bash
# check-bypass.sh <tcp-experiment program> <tcp-bypass program>
# Checks what each bypass mode does to real packets, using the per-class path
# lengths the programs report.
set -uo pipefail

TCP=$1
BYPASS=$2
DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
NETWORK=(--nodes=64 --degree=8 --hostsPerRouter=2 --load=0.2 --seed=561)
failures=0

fail() {
    echo "FAILED: $1"
    failures=$((failures + 1))
}

# The hop-count fractions on one class's line in a program's output.
fractions() {
    grep "^path_length_fraction_$2 " "$1" | cut -d' ' -f2-
}

# The mean fabric hops of one class.
mean_hops() {
    fractions "$1" "$2" | tr ' ' '\n' | awk -F= '{ sub("L", "", $1); sum += $1 * $2 } END { print sum }'
}

# run <name> <program> <options...>
run() {
    local name=$1 program=$2
    shift 2
    "$program" "${NETWORK[@]}" "$@" "--outputDir=$DIR/$name" > "$DIR/$name.txt" 2>&1 \
        || fail "$name exited with an error"
    grep -q "ttl=0 no_route=0" "$DIR/$name.txt" || fail "$name dropped packets for routing reasons"
    grep -q "unfinished_flows small=0 small_adjacent=0 large=0" "$DIR/$name.txt" \
        || fail "$name left flows unfinished"
}

# close_to <name> <a> <b>: a and b are within 5% of each other.
close_to() {
    awk -v a="$2" -v b="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= 0.05 * b) }' \
        || fail "$1: $2 is not close to $3"
}

ALL_SMALL=(--flows=500 --smallFraction=1)
MIXED=(--flows=500 --smallFraction=0.9 --largeBytes=100000)
ONE_HOP="L0=0 L1=1 L2=0 L3=0 L4=0 L5=0 L6=0 L7=0"
NO_PACKETS="L0=0 L1=0 L2=0 L3=0 L4=0 L5=0 L6=0 L7=0"

# With no bypass, the program must be identical to the TCP experiment.
"$TCP" "${NETWORK[@]}" "${MIXED[@]}" > "$DIR/tcp.txt" 2>&1 || fail "tcp-experiment exited with an error"
"$BYPASS" "${NETWORK[@]}" "${MIXED[@]}" --bypass=none > "$DIR/none.txt" 2>&1 || fail "none exited with an error"
cmp -s "$DIR/tcp.txt" "$DIR/none.txt" || fail "--bypass=none differs from tcp-experiment"

for mode in none adjacent shortest; do
    run "small-$mode" "$BYPASS" "${ALL_SMALL[@]}" "--bypass=$mode"
    run "mixed-$mode" "$BYPASS" "${MIXED[@]}" "--bypass=$mode"
    # Every packet of a small flow is marked, including handshakes and ACKs.
    [ "$(fractions "$DIR/small-$mode.txt" large)" = "$NO_PACKETS" ] \
        || fail "$mode: some packets of small flows are unmarked"
done

# Both bypass modes put adjacent small flows on the direct link.
for mode in adjacent shortest; do
    [ "$(fractions "$DIR/small-$mode.txt" small_adjacent)" = "$ONE_HOP" ] \
        || fail "$mode: adjacent small flows don't all take 1 hop"
done
[ "$(fractions "$DIR/small-none.txt" small_adjacent)" != "$ONE_HOP" ] \
    || fail "none: adjacent small flows should be sprayed"

# adjacent leaves other small flows on Spraypoint, while shortest shortens them.
none=$(mean_hops "$DIR/small-none.txt" small_other)
adjacent=$(mean_hops "$DIR/small-adjacent.txt" small_other)
shortest=$(mean_hops "$DIR/small-shortest.txt" small_other)
close_to "adjacent small_other hops" "$adjacent" "$none"
awk -v a="$shortest" -v b="$none" 'BEGIN { exit !(a < b - 0.5) }' \
    || fail "shortest: small flows average $shortest hops, not well below $none"

# Neither mode moves large flows off Spraypoint.
none=$(mean_hops "$DIR/mixed-none.txt" large)
for mode in adjacent shortest; do
    close_to "$mode large hops" "$(mean_hops "$DIR/mixed-$mode.txt" large)" "$none"
done

# Every mode runs the same flows: same endpoints, size, and start.
for mode in adjacent shortest; do
    cmp -s <(cut -d, -f1-7 "$DIR/small-none/flows.csv") <(cut -d, -f1-7 "$DIR/small-$mode/flows.csv") \
        || fail "$mode runs different flows than none"
done

if "$BYPASS" --nodes=16 --degree=4 --flows=10 --bypass=typo > /dev/null 2>&1; then
    fail "an unknown bypass mode was accepted"
fi

[ "$failures" -eq 0 ] && echo "all bypass checks passed"
exit "$((failures > 0))"
