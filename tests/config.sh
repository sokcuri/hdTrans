#!/bin/sh
set -eu
export HDTRANS_CACHE_DIR=off
cli=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-config.XXXXXX")
tmp=$(cd "$tmp" && pwd -P)
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/bin" "$tmp/settings/first" "$tmp/settings/second" "$tmp/other"
cp "$cli" "$tmp/bin/hdtrans-cli"
cli=$tmp/bin/hdtrans-cli
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n猫\t고양이\tN110\t\t\tGN\t\t\t\t\t1\n' > "$tmp/settings/first/base.tsv"
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tflags\tprio\treading\tid\n猫\t야옹이\tN110\t\t\tGN\t\t\t\t\t1\n' > "$tmp/settings/second/base.tsv"
printf 'dicts: first\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/settings/custom.yaml"
printf '猫' > "$tmp/input"
cd "$tmp/other"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
"$cli" -dicts "$tmp/settings/second" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'dicts: ../settings/second\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
mkdir -p "$tmp/bin/Ehnd"
printf '야옹이\t흰고양이\t1\t0\n' > "$tmp/bin/Ehnd/PostFilter_test.txt"
printf 'dicts: ../settings/second\nuserdict: ""\ncache: off\n' > "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'ehnd: true\n' >> "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
printf 'ehnd: false\n' >> "$tmp/bin/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
"$cli" -ehnd "$tmp/bin/Ehnd" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
printf 'dicts: second\nuserdict: ""\ncache: off\nehnd: true\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
rm "$tmp/bin/hdtrans.config.yaml"
"$cli" -dicts "$tmp/settings/second" -userdict "" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'dicts: [first, second]\nunknown: value\ncg: wrong\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out" 2> "$tmp/errors"
[ "$(cat "$tmp/out")" = 야옹이 ]
grep -F 'custom.yaml:2: unknown key: unknown' "$tmp/errors" > /dev/null
grep -F 'custom.yaml:3: cg: expected a boolean' "$tmp/errors" > /dev/null
printf 'dicts: first\nuserdict: ""\nehnd: ""\ncache: cache\n' > "$tmp/settings/custom.yaml"
HDTRANS_CACHE_DIR=off "$cli" -config "$tmp/settings/custom.yaml" -compile-dicts > "$tmp/cache-path"
case "$(cat "$tmp/cache-path")" in "$tmp/settings/cache/"*.hdsnap) ;; *) exit 1 ;; esac
status=0
"$cli" -config "$tmp/settings/custom.yaml" -no-cache -compile-dicts > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 2 ]
"$cli" -config "$tmp/settings/custom.yaml" -no-cache -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
status=0
"$cli" -config "$tmp/missing.yaml" -in "$tmp/input" > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 1 ]
mkdir -p "$tmp/bin/dicts" "$tmp/bin/hdTrans/dicts" "$tmp/bin/hdTrans/user"
cp "$tmp/settings/second/base.tsv" "$tmp/bin/dicts/"
printf '犬\t옛사전\n' > "$tmp/bin/userdict.tsv"
cp "$tmp/settings/first/base.tsv" "$tmp/bin/hdTrans/dicts/"
printf '犬\t새사전\n' > "$tmp/bin/hdTrans/user/userdict.tsv"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
printf '犬' > "$tmp/dog"
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 새사전 ]
printf 'dicts: ../../../settings/second\nuserdict: userdict.tsv\ncache: off\n' > "$tmp/bin/hdTrans/user/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
"$cli" -dicts "$tmp/settings/first" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
printf 'cache: off\n' > "$tmp/bin/hdTrans/user/hdtrans.config.yaml"
mv "$tmp/bin/hdTrans/user/userdict.tsv" "$tmp/bin/hdTrans/user/staged.tsv"
mv "$tmp/bin/hdTrans/user/staged.tsv" "$tmp/bin/hdTrans/user/USERDICT.TSV"
mv "$tmp/bin/hdTrans/user/hdtrans.config.yaml" "$tmp/bin/hdTrans/user/staged.yaml"
mv "$tmp/bin/hdTrans/user/staged.yaml" "$tmp/bin/hdTrans/user/HDTRANS.CONFIG.YAML"
mv "$tmp/bin/hdTrans/dicts" "$tmp/bin/hdTrans/staged-dicts"
mv "$tmp/bin/hdTrans/staged-dicts" "$tmp/bin/hdTrans/DICTS"
mv "$tmp/bin/hdTrans/user" "$tmp/bin/hdTrans/staged-user"
mv "$tmp/bin/hdTrans/staged-user" "$tmp/bin/hdTrans/USER"
mv "$tmp/bin/hdTrans" "$tmp/bin/staged-data"
mv "$tmp/bin/staged-data" "$tmp/bin/HDTRANS"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 새사전 ]
rm -rf "$tmp/bin/HDTRANS"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 옛사전 ]
mkdir -p "$tmp/bin/user"
printf '犬\t새사전\n' > "$tmp/bin/user/userdict.tsv"
printf 'cache: off\n' > "$tmp/bin/user/hdtrans.config.yaml"
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 새사전 ]
rm "$tmp/bin/user/userdict.tsv" "$tmp/bin/user/hdtrans.config.yaml"
printf '# comments only\n# 犬\t견본사전\n' > "$tmp/bin/user/userdict.txt"
printf 'dicts: missing\n' > "$tmp/bin/user/hdtrans.config.example.yaml"
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 옛사전 ]
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf '犬\t첫파일\n' > "$tmp/settings/first-user.txt"
printf '犬\t뒤파일\n' > "$tmp/settings/second-user.tsv"
"$cli" -userdict "$tmp/settings/first-user.txt" -userdict "$tmp/settings/second-user.tsv" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 뒤파일 ]
"$cli" -userdict "$tmp/settings/second-user.tsv" -userdict "$tmp/settings/first-user.txt" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 첫파일 ]
printf 'dicts: first\nuserdict: [first-user.txt, second-user.tsv]\ncache: off\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 뒤파일 ]
"$cli" -config "$tmp/settings/custom.yaml" -userdict "$tmp/settings/first-user.txt" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 첫파일 ]
mkdir -p "$tmp/settings/users"
printf '犬\t폴더파일\n' > "$tmp/settings/users/USERDICT_Z.TXT"
printf '猫\n' > "$tmp/settings/users/userdict-bad.tsv"
printf 'dicts: first\nuserdict: [first-user.txt, users]\ncache: off\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/dog" > "$tmp/out" 2> "$tmp/errors"
[ "$(cat "$tmp/out")" = 폴더파일 ]
grep -F 'userdict-bad.tsv:1: empty KR' "$tmp/errors" > /dev/null
mkdir -p "$tmp/settings/speech"
printf 'key\tko\tpos\tsem\tgram\tdict\trule\tid\n高\t$D3@\tH7N0\t\t\tGN\t0\t1\nい\t1다\tT500\t\t\tGN\t0\t2\n' > "$tmp/settings/speech/base.tsv"
printf '「高い」' > "$tmp/quoted"
printf 'dicts: speech\nuserdict: ""\ncache: off\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/quoted" > "$tmp/out"
[ "$(cat "$tmp/out")" = 「높다」 ]
printf 'speech: soft\n' >> "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/quoted" > "$tmp/out"
[ "$(cat "$tmp/out")" = 「높아」 ]
"$cli" -config "$tmp/settings/custom.yaml" --speech strict -in "$tmp/quoted" > "$tmp/out"
[ "$(cat "$tmp/out")" = 「높다」 ]
"$cli" -config "$tmp/settings/custom.yaml" --speech=soft -reference -in "$tmp/quoted" > "$tmp/out"
[ "$(cat "$tmp/out")" = 「높다」 ]
status=0
"$cli" -config "$tmp/settings/custom.yaml" --speech always -in "$tmp/quoted" > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 2 ]
grep -F -- '--speech takes soft or strict' "$tmp/errors" > /dev/null
printf 'dicts: speech\nuserdict: ""\ncache: off\nspeech: always\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/quoted" > "$tmp/out" 2> "$tmp/errors"
[ "$(cat "$tmp/out")" = 「높다」 ]
grep -F 'custom.yaml:4: speech: expected soft or strict' "$tmp/errors" > /dev/null
echo 'CLI config: PASS'
