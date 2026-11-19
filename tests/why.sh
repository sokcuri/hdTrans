#!/bin/sh
set -eu
export HDTRANS_CACHE_DIR=off
cli=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-why.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/dicts" "$tmp/ehnd" "$tmp/home"
export HOME=$tmp/home
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tid\n学生\t학생\tN110\t\t\tGN\t0\t1\n' > "$tmp/dicts/base.tsv"
printf '魔導具\t学生\t1\t0\n' > "$tmp/ehnd/PreFilter_test.txt"
printf '학생\t마도구\t1\t0\n' > "$tmp/ehnd/PostFilter_test.txt"
printf '魔導具' > "$tmp/input"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd "$tmp/ehnd" -in "$tmp/input" > "$tmp/plain" 2> "$tmp/plain-err"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd "$tmp/ehnd" -why -in "$tmp/input" > "$tmp/why" 2> "$tmp/logs"
cmp "$tmp/plain" "$tmp/why"
[ "$(cat "$tmp/why")" = 마도구 ]
[ ! -s "$tmp/plain-err" ]
grep -F '[REQUEST] 魔導具' "$tmp/logs" > /dev/null
grep -F 'PreFilter : [PreFilter_test.txt:0]' "$tmp/logs" > /dev/null
grep -F '[PRE] 学生' "$tmp/logs" > /dev/null
grep -F '[TRANS] 학생' "$tmp/logs" > /dev/null
grep -F 'PostFIlter : [PostFilter_test.txt:0]' "$tmp/logs" > /dev/null
grep -F '[POST] 마도구' "$tmp/logs" > /dev/null
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd "$tmp/ehnd" -why -stages "$tmp/stages" -in "$tmp/input" > "$tmp/staged" 2> "$tmp/staged-logs"
cmp "$tmp/plain" "$tmp/staged"
grep -F 'PreFilter : [PreFilter_test.txt:0]' "$tmp/staged-logs" > /dev/null
# The split note (spec/words.md, LEX-6722): one WHY-SPLIT line per Japanese token with the
# cost table, none without the table or without -why, and the same output either way.
printf 'ビザ\t비자\tN110\t\t\tGN\t0\t2\n' >> "$tmp/dicts/base.tsv"
printf 'ビザー' > "$tmp/kana"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd '' -why -in "$tmp/kana" > "$tmp/kana-bare" 2> "$tmp/kana-bare-logs"
! grep -F 'WHY-SPLIT' "$tmp/kana-bare-logs" > /dev/null
cp "$2" "$tmp/dicts/seg-costs.tsv"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd '' -in "$tmp/kana" > "$tmp/kana-plain" 2> "$tmp/kana-plain-logs"
"$cli" -dicts "$tmp/dicts" -userdict '' -ehnd '' -why -in "$tmp/kana" > "$tmp/kana-why" 2> "$tmp/kana-logs"
cmp "$tmp/kana-plain" "$tmp/kana-why"
cmp "$tmp/kana-bare" "$tmp/kana-why"
[ ! -s "$tmp/kana-plain-logs" ]
[ "$(grep -c '^WHY-SPLIT' "$tmp/kana-logs")" = 1 ]
grep '^WHY-SPLIT	1	0	ビザー	search [0-9]*: ビザ/N110=비자 (word [0-9]*) | ー/' "$tmp/kana-logs" > /dev/null
printf 'PASS CLI -why filter logs, split notes and unchanged stdout\n'
