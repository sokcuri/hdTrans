#!/bin/sh
# Translates each examples/<case>.ja.txt as one document (--speech-levels auto) into
# build/examples/<case>.ko.txt and diffs it against examples/<case>.ko.txt.
#   tools/examples.sh CLI DICTS [--accept]
# The first line of each input is a `#` description of the case; it is not translated and is
# copied as the first line of the output. --accept copies the outputs over examples/*.ko.txt.
# An empty DICTS skips (77). HDTRANS_EXAMPLES_OUT replaces build/examples as the output folder.
set -eu
LC_ALL=C; export LC_ALL
[ $# -ge 2 ] || { echo "usage: tools/examples.sh CLI DICTS [--accept]" >&2; exit 2; }
cli=$1 dicts=$2 accept=${3-}
[ -n "$dicts" ] || { echo "HDTRANS_DICTS is not set; skipping examples"; exit 77; }
here=$(cd "$(dirname "$0")/.." && pwd)
src=$here/examples
out=${HDTRANS_EXAMPLES_OUT:-$here/build/examples}
mkdir -p "$out"
status=0
for ja in "$src"/*.ja.txt; do
    name=${ja##*/}; name=${name%.ja.txt}
    ko=$out/$name.ko.txt
    body=$out/$name.body.ja.txt
    head -n 1 "$ja" | grep -q '^#' || { echo "$name: first line is not a # description" >&2; exit 2; }
    tail -n +2 "$ja" > "$body"
    { head -n 1 "$ja"; "$cli" -dicts "$dicts" -speech-levels auto -in "$body"; } > "$ko" ||
        { echo "$name: CLI failed" >&2; exit 1; }
    rm "$body"
    if [ "$accept" = --accept ]; then
        cp "$ko" "$src/$name.ko.txt"
    elif ! diff "$src/$name.ko.txt" "$ko"; then
        echo "$name: output differs from examples/$name.ko.txt"
        status=1
    fi
done
exit $status
