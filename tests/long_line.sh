#!/bin/sh
# Long lines full of drawn honorifics and drawn sounds translate in time linear in their length
# (ctest sound.long-line has a time limit; work per honorific over the whole line takes minutes).
set -eu
export HDTRANS_CACHE_DIR=off
cli=$1
dicts=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-long.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/home"
export HOME=$tmp/home
: > "$tmp/input"
for unit in '田中ちゃーん、' 'おはようちゃーん' 'ちゃーんと見て、' 'がちゃーん'; do
    i=0
    line=
    while [ $i -lt 2000 ]; do
        line=$line$unit
        i=$((i + 1))
    done
    printf '%s\n' "$line" >> "$tmp/input"
done
"$cli" -dicts "$dicts" -userdict '' -in "$tmp/input" > "$tmp/output"
[ "$(wc -l < "$tmp/output")" -eq 4 ]
printf 'PASS long lines of drawn honorifics\n'
