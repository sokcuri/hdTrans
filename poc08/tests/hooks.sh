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
    "$b" --dicts "$dicts" $mode --poc08 none --poc08-why "$tmp/why.jsonl" --poc08-stages "$tmp/ir.jsonl" \
        --in "$tmp/in.txt" > "$tmp/c.txt" || fail=1
    cmp -s "$tmp/a.txt" "$tmp/b.txt" || { echo "FAIL: hdtrans08 ${mode:-document} differs from hdtrans"; fail=1; }
    cmp -s "$tmp/a.txt" "$tmp/c.txt" || { echo "FAIL: hdtrans08 --poc08 none with the side IR ${mode:-document} differs"; fail=1; }
    grep -q '"ko":' "$tmp/why.jsonl" || { echo "FAIL: no side IR records"; fail=1; }
done
# The hooks know every line: in --wrapped mode, and after a line longer than the engine's piece limit.
printf '猫が好きだ。\n犬も好きだ。\n' > "$tmp/w.txt"
"$b" --dicts "$dicts" --wrapped --poc08 none --poc08-stats --poc08-stages "$tmp/w.jsonl" --in "$tmp/w.txt" \
    > /dev/null 2> "$tmp/w.err" || fail=1
grep -q 'sentences=[1-9]' "$tmp/w.err" || { echo "FAIL: --wrapped reaches no sentence"; fail=1; }
python3 - "$tmp/long.txt" <<'PY'
import sys
open(sys.argv[1], "w", encoding="utf-8").write("あ" * 30000 + "。\n犬も好きだ。\n鳥も好きだ。\n")
PY
"$b" --dicts "$dicts" --poc08 none --poc08-stages "$tmp/long.jsonl" --in "$tmp/long.txt" > /dev/null || fail=1
for want in '"line":2,' '"line":3,'; do
    grep '"case"' "$tmp/long.jsonl" | grep -q "$want" || { echo "FAIL: no side IR for $want after a long line"; fail=1; }
done
# 0.8-rule leaves caption documents as 0.7.x writes them, and rewrites a quoted line of a story.
printf '(太郎)行くぞ｡\n｢待って｣\n(花子)うん｡\n' > "$tmp/cap.txt"
"$a" --dicts "$dicts" --in "$tmp/cap.txt" > "$tmp/cap.a" || fail=1
"$b" --dicts "$dicts" --poc08 rule --in "$tmp/cap.txt" > "$tmp/cap.b" || fail=1
cmp -s "$tmp/cap.a" "$tmp/cap.b" || { echo "FAIL: --poc08 rule changed a caption document"; fail=1; }
printf '彼は窓の外を見た。\n「明日も来てくださいね」\n' > "$tmp/story.txt"
"$b" --dicts "$dicts" --poc08 rule --poc08-stats --in "$tmp/story.txt" > /dev/null 2> "$tmp/story.err" || fail=1
grep -q 'hdtrans08 rule: .*dialogue=1 ' "$tmp/story.err" || { echo "FAIL: the quoted line is not dialogue"; fail=1; }
exit $fail
