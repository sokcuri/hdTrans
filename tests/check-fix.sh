#!/bin/sh
# tools/check-fix.sh in a small repository: a dictionary row change translates again only the lines
# that hold its key, the base comes from the cache the second time, and the packet holds the change.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cli=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-check-fix.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
repo=$tmp/repo
mkdir -p "$repo/tools" "$repo/dicts"
for f in check-fix.sh sets.py affected.py judge-packet.py; do cp "$here/tools/$f" "$repo/tools/"; done
cp "$here/tests/unit/dict/testdata/folder/base.tsv" "$here/tests/unit/dict/testdata/folder/words.tsv" "$repo/dicts/"
git -C "$repo" init -q
git -C "$repo" config user.email test@example.com
git -C "$repo" config user.name test
git -C "$repo" add .
git -C "$repo" commit -qm base
git -C "$repo" branch develop
i=0
while [ $i -lt 40 ]; do printf '学生です。\n東京へ行く。\n学生\n' >> "$tmp/corpus.txt"; i=$((i + 1)); done
sed 's/	동경	/	도쿄	/' "$repo/dicts/words.tsv" > "$tmp/words.tsv"
mv "$tmp/words.tsv" "$repo/dicts/words.tsv"
export HDTRANS_WORK=$tmp/work HDTRANS_SETS="c=$tmp/corpus.txt d=$tmp/corpus.txt@blocks=7" HDTRANS_CANARY=5
sh "$repo/tools/check-fix.sh" -n -B "$cli" t > "$tmp/first.log" 2>&1 || { cat "$tmp/first.log"; exit 1; }
[ "$(cat "$tmp/work/t/needles.txt")" = 東京 ]
grep -F 'c: 3 distinct lines translated, 40 lines hold a needle' "$tmp/first.log" > /dev/null
grep -F 'c: 40 changed lines' "$tmp/first.log" > /dev/null
grep -F 'd: 17 documents translated, 40 lines hold a needle' "$tmp/first.log" > /dev/null
grep -F 'd: 40 changed lines' "$tmp/first.log" > /dev/null
grep -F '80 lines, 1 distinct changes, 1 edits, 1 to judge' "$tmp/first.log" > /dev/null
[ "$(cut -f 2-4 "$tmp/work/t/packet/reps.pairs.tsv")" = "$(printf '東京へ行く。\t동경행.\t도쿄행.')" ] ||
    { cat "$tmp/work/t/packet/reps.pairs.tsv"; exit 1; }
# The second run takes the base from the cache and leaves no output of a set it no longer has.
HDTRANS_SETS="c=$tmp/corpus.txt" sh "$repo/tools/check-fix.sh" -n -B "$cli" t > "$tmp/second.log" 2>&1 ||
    { cat "$tmp/second.log"; exit 1; }
grep -F 'c: 120 lines from the cache' "$tmp/second.log" > /dev/null
[ ! -e "$tmp/work/t/d.pairs.tsv" ]
grep -F '40 lines, 1 distinct changes' "$tmp/second.log" > /dev/null
# A file outside dicts/ that reaches the engine translates everything again.
mkdir "$repo/src"
echo '// x' > "$repo/src/a.cpp"
sh "$repo/tools/check-fix.sh" -n -B "$cli" -c "$cli" u > "$tmp/third.log" 2>&1 || { cat "$tmp/third.log"; exit 1; }
[ "$(cat "$tmp/work/u/needles.txt")" = '*' ]
grep -E '^c: 3 distinct lines translated, [0-9.]+ s$' "$tmp/third.log" > /dev/null
