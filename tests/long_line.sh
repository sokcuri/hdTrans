#!/bin/sh
# Long lines translate in time linear in their length: drawn honorifics and drawn sounds, a long run
# of one kana before a nickname ending or before the forms of western speech, random hiragana, one
# kanji or one drawn sound said again and again, one clause of short questions or sentences with no
# mark (ctest sound.long-line has a time limit; work per honorific, per position or per word over the
# whole line takes minutes).
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
# Katakana names before が all along one line: the favour cue after each run reads its clause once, not
# again to the end of the line from each run.
i=0
line=
while [ $i -lt 3000 ]; do
    line=${line}ヨンホが
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
line=そらと
# A long kana run after a kana word and と: the longest word that starts the run is found once, not by
# a lookup of every end of the run.
i=0
while [ $i -lt 8000 ]; do
    line=${line}ね
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# Random hiragana: short words all along one token, each placed among the words before it.
awk 'BEGIN {
    n = split("あ い う え お か き く け こ さ し す せ そ た ち つ て と な に ぬ ね の は ひ ふ へ ほ ま み む め も や ゆ よ ら り る れ ろ わ ん", k, " ")
    srand(7)
    for (i = 0; i < 4000; i++) printf "%s", k[int(rand() * n) + 1]
    printf "\n"
}' >> "$tmp/input"
# One kana run with か all along it: the stem before each か is the run walked once for the whole
# line, not again from each か.
i=0
line=
while [ $i -lt 3000 ]; do
    line=${line}かいとか
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# One kanji said again and again: each word is a copy, and its run of copies is the whole line.
i=0
line=
while [ $i -lt 8000 ]; do
    line=${line}日
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# One clause of kana before the forms of western speech (や, けん, んね): the normalize patterns
# that look back over the clause are tried at every position of the line.
i=0
line=
while [ $i -lt 6000 ]; do
    line=${line}そ
    i=$((i + 1))
done
printf '%sやけんね\n' "$line" >> "$tmp/input"
# Particles and endings all along one line: the nearest word of each item is walked once for the line.
i=0
line=
while [ $i -lt 3000 ]; do
    line=${line}ですか
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# One particle said again and again.
i=0
line=
while [ $i -lt 6000 ]; do
    line=${line}よ
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# A drawn sound said again and again (あー): the sound rewrites and the copies of a unit walk the run
# once, not again from each mark or word of it.
i=0
line=
while [ $i -lt 5000 ]; do
    line=${line}あー
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# One kanji token of a unit said again and again: each word of a copy is read from a start inside the
# unit, and the copies after it are remembered for each start.
i=0
line=
while [ $i -lt 1000 ]; do
    line=${line}漢字研究部品作業時間
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# One clause of short questions with no mark (彼が行くか): the packs layer reads the end of the
# sentence after each verb only as far as a final= value reaches, and the chain's polite test once;
# whether a word stands in a quote is read from the marks of its token, not from its text again.
i=0
line=
while [ $i -lt 4000 ]; do
    line=${line}彼が行くか
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# A clause with no mark and no word that ends a scan (じゃなかけん said again): a scan of a construction
# row walks the clause once for all its words, not again from each word.
i=0
line=
while [ $i -lt 3000 ]; do
    line=${line}じゃなかけん
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# One clause of short sentences with no mark (彼が行く): the walk back to a question word before a
# word skips, within a construction pass, the words that can neither end it nor ask.
i=0
line=
while [ $i -lt 5000 ]; do
    line=${line}彼が行く
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
# A long run of one kana before さん, whose ん is a nickname ending: each start of the name is tested as
# a person before the kana before it are read as words.
i=0
line=
while [ $i -lt 8000 ]; do
    line=${line}そ
    i=$((i + 1))
done
printf '%sさん\n' "$line" >> "$tmp/input"
# よか said again and again: every match of a dialect form reads the kana run before it from where the
# match before it stopped, not from the run start again.
i=0
line=
while [ $i -lt 8000 ]; do
    line=${line}よか
    i=$((i + 1))
done
printf '%s\n' "$line" >> "$tmp/input"
"$cli" -dicts "$dicts" -userdict '' -in "$tmp/input" > "$tmp/output"
[ "$(wc -l < "$tmp/output")" -eq "$(wc -l < "$tmp/input")" ]
printf 'PASS long lines\n'
