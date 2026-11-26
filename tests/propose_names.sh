#!/bin/sh
# --propose-names on the shipped dictionaries (spec LEX-8152, LEX-8154): one kanji of the name layer that the
# cues of two lines name, a surname that stands alone beside a dropped full name, nothing for a text without
# names, and no title or drawn-out call.
set -eu
export HDTRANS_CACHE_DIR=off
cli=$1
dicts=$2
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-propose.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/home"
export HOME=$tmp/home
tab=$(printf '\t')
propose() {
    printf '%s\n' "$@" > "$tmp/in"
    "$cli" --dicts "$dicts" --propose-names --in "$tmp/in"
}
[ "$(propose '宵「おはよう」' '宵「うん」' '宵君が来た。')" = "宵${tab}요이${tab}given${tab}hon" ]
[ "$(propose '栞「おはよう」' '栞「うん」' '栞さんが来た。')" = "栞${tab}시오리${tab}given${tab}hon" ]
[ "$(propose '望月「おはよう」' '望月「うん」' '望月さんが来た。' '「望月大先生が決めるだろう」')" = "望月${tab}모치즈키${tab}family" ]
[ "$(propose 'マチルダ「おはよう」' 'マチルダ「うん」' 'マチルダさんが来た。' 'マチルダーっ！')" = "マチルダ${tab}마틸다${tab}given" ]
[ "$(propose '学園長「おはよう」' '学園長「うん」' '学園長様が来た。')" = "" ]
[ "$(propose '朝、目を覚ますと雨が降っていた。' '春の宵、桜の下で人々が集まった。' '彼は栞を挟んで本を閉じた。' 'どこまで行くんだ？')" = "" ]
echo 'propose names: PASS'
