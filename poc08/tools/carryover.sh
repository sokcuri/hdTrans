#!/bin/sh
# Carry-over of the 0.7.x rule cases (tests/rules/cases, every row passes on 0.7.x) when 0.8 parts
# replace 0.7.x stages (PIVOT E2 gate: >= 90%).
#   poc08/tools/carryover.sh PARTS [OUTDIR]     e.g. segment,normalize
# Prints the pass share and the failures per layer; OUTDIR/carryover.txt keeps the failing rows.
# HDTRANS08 and HDTRANS_DICTS override the binary and the dictionaries.
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
parts=${1:?parts}
out=${2:-.}
cli=${HDTRANS08:-$root/build/mac/poc08/hdtrans08}
dicts=${HDTRANS_DICTS:-$root/dicts}
mkdir -p "$out"
HOME=$(mktemp -d) HDTRANS_CACHE_DIR=off "$root/tools/rule-cases.sh" "$cli" "$dicts" -- --poc08 "$parts" > "$out/carryover.txt" 2>&1
tail -n 1 "$out/carryover.txt" | awk -v parts="$parts" '
    match($0, /: [0-9]+ rows, [0-9]+ failed/) {
        split(substr($0, RSTART + 2, RLENGTH - 2), f, " ")
        printf "carry-over %s: %d of %d rows pass (%.2f%%)\n", parts, f[1] - f[3], f[1], 100 * (f[1] - f[3]) / f[1]
    }'
grep '^== layer' "$out/carryover.txt" | sort -t: -k2 -n -r | head -n 12
