#!/bin/sh
# --poc08 attach as the packs layer's attachment provider: usage tests/attach.sh HDTRANS08 DICTS
# Rows whose cue atoms need an attachment (obj.how, cl.tense, main.vol, an omitted subj by its class) fire with
# --poc08 attach and not without it; a row without such atoms fires in both; a clause feature that does not hold
# (cl.tense=nonpast on a past verb) fires in neither.
set -u
b=$1 dicts=$2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp" HDTRANS_CACHE_DIR=off
mkdir "$tmp/pack"
tab=$(printf '\t')
cat > "$tmp/pack/classes.tsv" <<ROWS
#! layer=pack-classes
class${tab}parent
@entity${tab}
@person${tab}@entity
ROWS
cat > "$tmp/pack/pack.tsv" <<ROWS
#! layer=pack
id${tab}slot${tab}lemma${tab}pos${tab}cond${tab}target
a.bare${tab}sense${tab}食べる${tab}v1${tab}obj.how=bare${tab}들다
a.past${tab}sense${tab}読む${tab}v5${tab}cl.tense=past${tab}낭독하다
a.person${tab}sense${tab}走る${tab}v5${tab}subj=@person subj.how=omitted${tab}뛰다
a.vol${tab}sense${tab}行く${tab}v5${tab}main.vol=+${tab}다니다
a.nonpast${tab}sense${tab}書く${tab}v5${tab}cl.tense=nonpast${tab}적다
a.plain${tab}sense${tab}飲む${tab}v5${tab}*${tab}들이켜다
ROWS
cat > "$tmp/in.txt" <<LINES
ご飯食べた？
昨日、本を読んだ。
「公園まで走ろう」と彼は言った。手紙書いたよ。
明日、学校に行く。お茶飲む？
LINES
fail=0
"$b" --dicts "$dicts,$tmp/pack" --poc08 attach -why --in "$tmp/in.txt" > "$tmp/on.txt" 2>&1 || fail=1
"$b" --dicts "$dicts,$tmp/pack" -why --in "$tmp/in.txt" > "$tmp/off.txt" 2>&1 || fail=1
fired() { grep "^WHY-PACK" "$1" | cut -f7 | sort -u | tr '\n' ' '; }
on=$(fired "$tmp/on.txt") off=$(fired "$tmp/off.txt")
[ "$on" = "a.bare a.past a.person a.plain a.vol " ] || { echo "FAIL with attach: $on"; fail=1; }
[ "$off" = "a.plain " ] || { echo "FAIL without attach: $off"; fail=1; }
# without packs the provider is never asked: --poc08 attach writes what the 0.7.x engine writes
"$b" --dicts "$dicts" --in "$tmp/in.txt" > "$tmp/base.txt" || fail=1
"$b" --dicts "$dicts" --poc08 attach --in "$tmp/in.txt" > "$tmp/nopack.txt" || fail=1
cmp -s "$tmp/base.txt" "$tmp/nopack.txt" || { echo "FAIL: --poc08 attach without packs changed the output"; fail=1; }
exit $fail
