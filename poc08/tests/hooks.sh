#!/bin/sh
# The hooked engine with no 0.8 part on writes what hdtrans writes, also while it lifts every
# sentence to the side IR: usage tests/hooks.sh HDTRANS HDTRANS08 DICTS
set -u
a=$1 b=$2 dicts=$3
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp" HDTRANS_CACHE_DIR=off
for f in "$here"/tests/rules/cases/0*.tsv; do grep -v '^#' "$f" | tail -n +2 | cut -f 2; done | head -n 400 > "$tmp/in.txt"
fail=0
for mode in --each ""; do
    "$a" --dicts "$dicts" $mode --in "$tmp/in.txt" > "$tmp/a.txt" || fail=1
    "$b" --dicts "$dicts" $mode --in "$tmp/in.txt" > "$tmp/b.txt" || fail=1
    "$b" --dicts "$dicts" $mode --poc08 all --poc08-why "$tmp/why.jsonl" --poc08-stages "$tmp/ir.jsonl" \
        --in "$tmp/in.txt" > "$tmp/c.txt" || fail=1
    cmp -s "$tmp/a.txt" "$tmp/b.txt" || { echo "FAIL: hdtrans08 ${mode:-document} differs from hdtrans"; fail=1; }
    cmp -s "$tmp/a.txt" "$tmp/c.txt" || { echo "FAIL: hdtrans08 --poc08 all ${mode:-document} differs"; fail=1; }
    grep -q '"ko":' "$tmp/why.jsonl" || { echo "FAIL: no side IR records"; fail=1; }
done
exit $fail
