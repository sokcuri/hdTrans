#!/bin/sh
# Runs hdtrans (0.7.x) and hdtrans08 with the same arguments on the same input.
#   poc08/tools/ab.sh [-d OUTDIR] ARGS... < input
# Writes OUTDIR/a.txt (0.7.x), OUTDIR/b.txt (hdtrans08) and OUTDIR/diff.txt (changed lines,
# numbered). HDTRANS, HDTRANS08 and HDTRANS_DICTS override the binaries and the 0.7.x dictionaries.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
out=.
if [ "${1:-}" = "-d" ]; then out=$2; shift 2; fi
a=${HDTRANS:-$root/build/mac/hdtrans}
b=${HDTRANS08:-$root/build/mac/poc08/hdtrans08}
dicts=${HDTRANS_DICTS:-$root/dicts}
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/in.txt"
HOME=$tmp HDTRANS_CACHE_DIR=off "$a" --dicts "$dicts" "$@" < "$tmp/in.txt" > "$out/a.txt"
HOME=$tmp "$b" "$@" < "$tmp/in.txt" > "$out/b.txt"
paste -d '\t' "$tmp/in.txt" "$out/a.txt" "$out/b.txt" | awk -F '\t' '$2 != $3 { print NR "\t" $1 "\n\ta\t" $2 "\n\tb\t" $3 }' > "$out/diff.txt"
echo "$(grep -c '^	a	' "$out/diff.txt" || true) of $(wc -l < "$tmp/in.txt" | tr -d ' ') lines differ: $out/diff.txt"
