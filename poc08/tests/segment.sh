#!/bin/sh
# --poc08 segment fix and keep cases: usage tests/segment.sh HDTRANS08 DICTS
# segment.cases.tsv rows: kind, input, note. kind "same": hdtrans08 --poc08 segment writes what hdtrans08 without
# parts (the 0.7.x engine) writes for the input line.
set -u
b=$1 dicts=$2
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp" HDTRANS_CACHE_DIR=off
fail=0
n=0
grep -v '^#' "$here/tests/segment.cases.tsv" | tail -n +2 > "$tmp/cases"
while IFS="$(printf '\t')" read -r kind input note; do
    n=$((n + 1))
    printf '%s\n' "$input" > "$tmp/in.txt"
    "$b" --dicts "$dicts" --in "$tmp/in.txt" > "$tmp/base.txt" || fail=1
    "$b" --dicts "$dicts" --poc08 segment --in "$tmp/in.txt" > "$tmp/seg.txt" || fail=1
    case "$kind" in
    same)
        cmp -s "$tmp/base.txt" "$tmp/seg.txt" || { echo "FAIL $n ($note): got $(cat "$tmp/seg.txt"), want $(cat "$tmp/base.txt")"; fail=1; } ;;
    *) echo "FAIL $n: unknown kind $kind"; fail=1 ;;
    esac
done < "$tmp/cases"
[ "$n" -gt 0 ] || { echo "FAIL: no cases"; fail=1; }
exit $fail
