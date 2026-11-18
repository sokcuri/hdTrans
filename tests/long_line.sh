#!/bin/sh
# Long lines translate in time linear in their length: drawn honorifics and drawn sounds, a long run
# of one kana before a nickname ending, random hiragana (ctest sound.long-line has a time limit; work
# per honorific, per start of a run or per word over the whole line takes minutes).
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
# Random hiragana: short words all along one token, each placed among the words before it.
awk 'BEGIN {
    n = split("あ い う え お か き く け こ さ し す せ そ た ち つ て と な に ぬ ね の は ひ ふ へ ほ ま み む め も や ゆ よ ら り る れ ろ わ ん", k, " ")
    srand(7)
    for (i = 0; i < 4000; i++) printf "%s", k[int(rand() * n) + 1]
    printf "\n"
}' >> "$tmp/input"
"$cli" -dicts "$dicts" -userdict '' -in "$tmp/input" > "$tmp/output"
[ "$(wc -l < "$tmp/output")" -eq "$(wc -l < "$tmp/input")" ]
printf 'PASS long lines\n'
