#!/bin/sh
set -eu
export HDTRANS_CACHE_DIR=off
cli=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-config.XXXXXX")
tmp=$(cd "$tmp" && pwd -P)
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/bin/hdtrans-data/user" "$tmp/settings/first" "$tmp/settings/second" "$tmp/other"
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
printf 'dicts: ../../../settings/second\nuserdict: ""\nehnd: ""\ncache: off\n' > "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
mkdir -p "$tmp/bin/hdtrans-data/ehnd"
printf '야옹이\t흰고양이\t1\t0\n' > "$tmp/bin/hdtrans-data/ehnd/PostFilter_test.txt"
printf 'dicts: ../../../settings/second\nuserdict: ""\ncache: off\n' > "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
printf 'ehnd: true\n' >> "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
printf 'ehnd: false\n' >> "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
"$cli" -ehnd "$tmp/bin/hdtrans-data/ehnd" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
printf 'dicts: second\nuserdict: ""\ncache: off\nehnd: true\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 흰고양이 ]
rm "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
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
mkdir -p "$tmp/bin/hdtrans-data/user"
"$cli" --dicts "$tmp/settings/first" --compile-snapshot "$tmp/bin/hdtrans-data/dicts.hdsnap" > "$tmp/snapshot-path"
printf '犬\t새사전\n' > "$tmp/bin/hdtrans-data/user/userdict.tsv"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
printf '犬' > "$tmp/dog"
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 새사전 ]
printf 'dicts: ../../../settings/second\nuserdict: userdict.tsv\ncache: off\n' > "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 야옹이 ]
"$cli" -dicts "$tmp/settings/first" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
printf 'cache: off\n' > "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml"
mv "$tmp/bin/hdtrans-data/user/userdict.tsv" "$tmp/bin/hdtrans-data/user/staged.tsv"
mv "$tmp/bin/hdtrans-data/user/staged.tsv" "$tmp/bin/hdtrans-data/user/USERDICT.TSV"
mv "$tmp/bin/hdtrans-data/user/hdtrans.config.yaml" "$tmp/bin/hdtrans-data/user/staged.yaml"
mv "$tmp/bin/hdtrans-data/user/staged.yaml" "$tmp/bin/hdtrans-data/user/HDTRANS.CONFIG.YAML"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
"$cli" -in "$tmp/dog" > "$tmp/out"
[ "$(cat "$tmp/out")" = 새사전 ]
rm "$tmp/bin/hdtrans-data/user/USERDICT.TSV" "$tmp/bin/hdtrans-data/user/HDTRANS.CONFIG.YAML"
printf '# comments only\n# 犬\t견본사전\n' > "$tmp/bin/hdtrans-data/user/userdict.txt"
printf 'dicts: missing\n' > "$tmp/bin/hdtrans-data/user/hdtrans.config.example.yaml"
"$cli" -in "$tmp/input" > "$tmp/out"
[ "$(cat "$tmp/out")" = 고양이 ]
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
printf '「高い。\n' > "$tmp/context"
printf '高い。」' > "$tmp/closing"
printf 'dicts: speech\nuserdict: ""\ncache: off\nspeech: soft\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/closing" > "$tmp/out"
[ "$(cat "$tmp/out")" = 높다.」 ]
"$cli" -config "$tmp/settings/custom.yaml" --context "$tmp/context" -in "$tmp/closing" > "$tmp/out"
[ "$(cat "$tmp/out")" = 높아.」 ]
"$cli" -config "$tmp/settings/custom.yaml" -context "$tmp/context" --json -in "$tmp/closing" > "$tmp/out"
grep -F '"text":"높아.」"' "$tmp/out" > /dev/null
printf '高い\n高い\n' > "$tmp/two"
printf 'wrapped: true\n' >> "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" --context "$tmp/context" -in "$tmp/two" > "$tmp/out" 2> "$tmp/errors"
[ "$(wc -l < "$tmp/out")" -eq 2 ]
[ ! -s "$tmp/errors" ]
"$cli" -config "$tmp/settings/custom.yaml" -each -in "$tmp/two" > "$tmp/out"
[ "$(wc -l < "$tmp/out")" -eq 2 ]
document_only() {
    status=0
    "$cli" -config "$tmp/settings/custom.yaml" "$@" -in "$tmp/two" > /dev/null 2> "$tmp/errors" || status=$?
    [ "$status" -eq 2 ]
    grep -F -- '--wrapped and --context cannot be combined with --each or --raw' "$tmp/errors" > /dev/null
}
document_only --wrapped -each
document_only -wrapped --raw
document_only --context "$tmp/context" -each
printf '猫\t네코\nルル\n' > "$tmp/names"
"$cli" -dicts "$tmp/settings/first" --names "$tmp/names" -in "$tmp/input" > "$tmp/out" 2> "$tmp/errors"
[ "$(cat "$tmp/out")" = 네코 ]
grep -F 'names:2: a name needs JA and KO' "$tmp/errors" > /dev/null
# The listed name decides the word as a name row of the document (docs/packs.md); HD08_CAST_OFF drops it.
"$cli" -dicts "$tmp/settings/first" --names "$tmp/names" --why -in "$tmp/input" > /dev/null 2> "$tmp/errors"
grep -F 'pack:names:1' "$tmp/errors" > /dev/null
grep "^WHY-PACK.*$(printf '\t')names:1$(printf '\t')name$(printf '\t')네코\$" "$tmp/errors" > /dev/null
printf 'surface\n猫\n' > "$tmp/cast-off"
[ "$(HD08_CAST_OFF="$tmp/cast-off" "$cli" -dicts "$tmp/settings/first" --names "$tmp/names" -in "$tmp/input" 2> /dev/null)" = 고양이 ]
printf 'ルルさん、おはよう。\nルルさん、またね。\n' > "$tmp/called"
"$cli" -dicts "$tmp/settings/first" --names "$tmp/names" --propose-names -in "$tmp/called" > "$tmp/out" 2> "$tmp/errors"
[ "$(cat "$tmp/out")" = "$(printf 'ルル\t룰\tgiven')" ]
# One line per call reads the list too.
[ "$("$cli" -dicts "$tmp/settings/first" --names "$tmp/names" -each -in "$tmp/input" 2> /dev/null)" = 네코 ]
# The names*.txt of a user folder are a list under --names.
mkdir -p "$tmp/cast"
printf '猫\t냥코\n' > "$tmp/cast/names.txt"
[ "$("$cli" -dicts "$tmp/settings/first" --userdict "$tmp/cast" -each -in "$tmp/input" 2> /dev/null)" = 냥코 ]
[ "$("$cli" -dicts "$tmp/settings/first" --userdict "$tmp/cast" --names "$tmp/names" -in "$tmp/input" 2> /dev/null)" = 네코 ]
status=0
"$cli" -dicts "$tmp/settings/first" --names "$tmp/names" -raw -in "$tmp/input" > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 2 ]
grep -F -- '--names cannot be combined with --raw, --propose-names with --each or --raw' "$tmp/errors" > /dev/null
status=0
"$cli" -dicts "$tmp/settings/first" --propose-names -each -in "$tmp/input" > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 2 ]
status=0
"$cli" -config "$tmp/settings/custom.yaml" --context "$tmp/missing" -in "$tmp/two" > /dev/null 2> "$tmp/errors" || status=$?
[ "$status" -eq 1 ]
printf 'dicts: speech\nuserdict: ""\ncache: off\nwrapped: yes\n' > "$tmp/settings/custom.yaml"
"$cli" -config "$tmp/settings/custom.yaml" -in "$tmp/two" > "$tmp/out" 2> "$tmp/errors"
grep -F 'custom.yaml:4: wrapped: expected a boolean (true or false)' "$tmp/errors" > /dev/null
echo 'CLI config: PASS'
