#!/bin/sh
# The attachment provider (docs/attachment.md): usage tests/attach.sh CLI DICTS
# The rows of tests/packs/attach whose atoms need attachment annotations fire with them and not with
# --attachment off (or HDTRANS_ATTACHMENT=off); a row without such atoms fires either way. Without a pack row
# that reads them the analyzer is not loaded and the output is what --attachment off writes.
set -u
cli=$1 dicts=$2
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp" HDTRANS_CACHE_DIR=off
unset HDTRANS_ATTACHMENT
pack="$here/packs/attach"
cat > "$tmp/in.txt" <<LINES
ご飯食べた？
昨日、本を読んだ。
「公園まで走ろう」と彼は言った。手紙書いたよ。
明日、学校に行く。お茶飲む？
LINES
fail=0
"$cli" --dicts "$dicts,$pack" -why --in "$tmp/in.txt" > "$tmp/on.txt" 2>&1 || fail=1
"$cli" --dicts "$dicts,$pack" --attachment off -why --in "$tmp/in.txt" > "$tmp/off.txt" 2>&1 || fail=1
HDTRANS_ATTACHMENT=off "$cli" --dicts "$dicts,$pack" -why --in "$tmp/in.txt" > "$tmp/env.txt" 2>&1 || fail=1
fired() { grep "^WHY-PACK" "$1" | cut -f7 | sort -u | tr '\n' ' '; }
on=$(fired "$tmp/on.txt") off=$(fired "$tmp/off.txt") env=$(fired "$tmp/env.txt")
[ "$on" = "a.bare a.past a.person a.plain a.vol " ] || { echo "FAIL with attachment: $on"; fail=1; }
[ "$off" = "a.plain " ] || { echo "FAIL with --attachment off: $off"; fail=1; }
[ "$env" = "a.plain " ] || { echo "FAIL with HDTRANS_ATTACHMENT=off: $env"; fail=1; }
"$cli" --dicts "$dicts" --in "$tmp/in.txt" > "$tmp/base.txt" || fail=1
"$cli" --dicts "$dicts" --attachment off --in "$tmp/in.txt" > "$tmp/nopack.txt" || fail=1
cmp -s "$tmp/base.txt" "$tmp/nopack.txt" || { echo "FAIL: without attachment rows the output changed"; fail=1; }
"$cli" --dicts "$dicts" --attachment sometimes --in "$tmp/in.txt" > /dev/null 2>&1 && { echo "FAIL: --attachment sometimes accepted"; fail=1; }
exit $fail
