#!/bin/sh
# Long lines full of drawn honorifics and drawn sounds, and a long run of one kana before a nickname
# ending, translate in time linear in their length (ctest sound.long-line has a time limit; work per
# honorific over the whole line, or per start of the run, takes minutes).
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
i=0
line=
while [ $i -lt 2000 ]; do
    line=${line}そ
    i=$((i + 1))
done
printf '%sいいじゃん\n' "$line" >> "$tmp/input"
"$cli" -dicts "$dicts" -userdict '' -in "$tmp/input" > "$tmp/output"
[ "$(wc -l < "$tmp/output")" -eq 5 ]
printf 'PASS long lines of drawn honorifics and kana runs\n'
