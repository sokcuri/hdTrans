#!/bin/sh
# Translates the test-data documents and reports the lines that differ, either from the expected
# outputs in the data or between two builds.
#   tools/e2e.sh --expected DATA CLI DICTS [--accept] [-- CLI-OPTIONS...]
#   tools/e2e.sh A B DICTS [DATA] [-- CLI-OPTIONS...]
# DATA is the test-data folder; empty or omitted, HDTRANS_TESTDATA is used, and without either the
# run is skipped (77). Each DATA/<source>/<name>.ja.txt starts with a `#` line naming the document;
# the rest is translated as one document with default settings, or with the CLI-OPTIONS after --
# (--speech-levels keep) given to every CLI run; a snapshot run A is taken as recorded. Outputs repeat that line, then
# hold the translation: build/e2e/<source>/<name>.ko.txt for CLI or B, and
# build/e2e/base/<source>/<name>.ko.txt for A.
# --expected compares CLI against DATA/<source>/<name>.ko.txt (A in the report); --accept then
# overwrites those files with the outputs. Otherwise A and B are CLI binaries; A may instead be a
# tools/snapshot.sh run folder, from which the document set with the same input is taken, and a
# document not in the run is listed as missing. HDTRANS_DICTS_A gives A its own dictionaries.
# The report gives each changed document's count and its changed lines (file line number, A, B),
# at most 20 per document and 200 in all.
# Exit: 0 identical or accepted, 1 changed, 2 invalid arguments or data, failed CLI or nothing
# compared, 77 no test data.
set -eu
usage() {
    echo 'usage: tools/e2e.sh --expected <data> <cli> <dicts> [--accept] [-- <cli-options>...]' >&2
    echo '       tools/e2e.sh <cliA|runA> <cliB> <dicts> [data] [-- <cli-options>...]' >&2
    exit 2
}
mode=pair accept=0 data=
# The arguments are rotated: the positional ones first, then the cliOptions CLI options after --.
n=$# i=0 cliOptions=0
while [ "$i" -lt "$n" ]; do
    arg=$1; shift; i=$((i + 1))
    case $arg in
        --accept) accept=1 ;;
        --expected)
            [ "$i" -lt "$n" ] || usage
            mode=expected data=$1; shift; i=$((i + 1)) ;;
        --)
            cliOptions=$((n - i))
            while [ "$i" -lt "$n" ]; do set -- "$@" "$1"; shift; i=$((i + 1)); done ;;
        *) set -- "$@" "$arg" ;;
    esac
done
positional=$(($# - cliOptions))
if [ "$mode" = expected ]; then
    [ "$positional" -eq 2 ] || usage
    a= b=$1 dictsArg=$2
else
    [ "$accept" -eq 0 ] && { [ "$positional" -eq 3 ] || [ "$positional" -eq 4 ]; } || usage
    a=$1 b=$2 dictsArg=$3 data=
    [ "$positional" -eq 3 ] || data=$4
fi
shift "$positional"
data=${data:-${HDTRANS_TESTDATA-}}
[ -n "$data" ] || { echo 'no test data (HDTRANS_TESTDATA is not set); skipping'; exit 77; }
[ -d "$data" ] || { echo "no test data at $data" >&2; exit 2; }
data=$(cd "$data" && pwd)
here=$(cd "$(dirname "$0")/.." && pwd)
out=$here/build/e2e
for dir in "$dictsArg" "${HDTRANS_DICTS_A:-$dictsArg}"; do
    [ -d "$dir" ] || { echo "no dictionaries at $dir" >&2; exit 2; }
done
dictsB=$(cd "$dictsArg" && pwd)
dictsA=$(cd "${HDTRANS_DICTS_A:-$dictsArg}" && pwd)
[ -x "$b" ] && [ -f "$b" ] || { echo "no CLI at $b" >&2; exit 2; }
if [ "$mode" = expected ]; then
    :
elif [ -d "$a" ]; then
    [ -f "$a/sets.tsv" ] || { echo "not a snapshot run: $a" >&2; exit 2; }
elif [ ! -x "$a" ]; then
    echo "no CLI or snapshot run at $a" >&2; exit 2
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-e2e.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM
mkdir "$tmp/cacheA" "$tmp/cacheB"
export LC_ALL=C
unset HDTRANS_SNAPSHOT HDTRANS_VERIFY_DICTS HDTRANS_DRIVER_DUMP
sha() {
    if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi |
        awk '{print $1}'
}
# translate CLI DICTS CACHEDIR HEADER OUT [CLI-OPTIONS...]: the body in $tmp/body.txt, as one document.
translate() {
    cli=$1 dicts=$2 cache=$3 title=$4 target=$5; shift 5
    { printf '%s\n' "$title"; HDTRANS_CACHE_DIR=$cache "$cli" -dicts "$dicts" "$@" -in "$tmp/body.txt"; } > "$target" ||
        { echo "$name: $cli failed" >&2; exit 2; }
}
docs=0 changedSets=0 changedLines=0 printed=0 accepted=0 missing=
for ja in "$data"/*/*.ja.txt; do
    [ -f "$ja" ] || continue
    name=${ja##*/}; name=${name%.ja.txt}
    folder=${ja%/*}; folder=${folder##*/}
    header=$(head -n 1 "$ja")
    case $header in '#'*) ;; *) echo "$folder/$name.ja.txt: first line is not a # line" >&2; exit 2 ;; esac
    tail -n +2 "$ja" > "$tmp/body.txt"
    mkdir -p "$out/$folder"
    if [ "$mode" = expected ]; then
        expected=${ja%.ja.txt}.ko.txt
        [ -f "$expected" ] || expected=/dev/null
    elif [ -d "$a" ]; then
        setName=$(awk -F '\t' -v sum="$(sha "$tmp/body.txt")" '
            NR > 1 && $2 == "document" && $5 == sum {print $1; exit}' "$a/sets.tsv")
        if [ -z "$setName" ] || [ ! -f "$a/outputs/$setName.out" ]; then
            missing="$missing $name"; continue
        fi
        mkdir -p "$out/base/$folder"
        expected=$out/base/$folder/$name.ko.txt
        { printf '%s\n' "$header"; cat "$a/outputs/$setName.out"; } > "$expected"
    else
        mkdir -p "$out/base/$folder"
        expected=$out/base/$folder/$name.ko.txt
        translate "$a" "$dictsA" "$tmp/cacheA" "$header" "$expected" "$@"
    fi
    translate "$b" "$dictsB" "$tmp/cacheB" "$header" "$out/$folder/$name.ko.txt" "$@"
    docs=$((docs + 1))
    awk -v name="$name" -v budget=$((200 - printed)) -v counts="$tmp/counts" '
        FILENAME == ARGV[1] {a[FNR] = $0; if (FNR > n) n = FNR; next}
        {b[FNR] = $0; if (FNR > n) n = FNR}
        END {
            for (i = 1; i <= n; ++i)
                if ((i in a) != (i in b) || a[i] != b[i]) line[++changed] = i
            shown = 0
            if (changed) printf "%s: %d changed line%s\n", name, changed, (changed == 1 ? "" : "s")
            for (k = 1; k <= changed && shown < 20 && shown < budget; ++k) {
                i = line[k]
                printf "  %d A: %s\n", i, (i in a) ? a[i] : "<missing>"
                printf "  %" length(i "") "s B: %s\n", "", (i in b) ? b[i] : "<missing>"
                ++shown
            }
            if (shown < changed) printf "  ... %d more\n", changed - shown
            print changed + 0, shown > counts
        }
    ' "$expected" "$out/$folder/$name.ko.txt"
    read -r count shown < "$tmp/counts"
    if [ "$count" -ne 0 ]; then
        changedSets=$((changedSets + 1))
        if [ "$accept" -eq 1 ]; then
            cp "$out/$folder/$name.ko.txt" "${ja%.ja.txt}.ko.txt"
            accepted=$((accepted + 1))
        fi
    fi
    changedLines=$((changedLines + count)) printed=$((printed + shown))
done
[ -z "$missing" ] || echo "missing:$missing"
printf 'e2e: %d documents compared, %d changed lines in %d documents\n' \
    "$docs" "$changedLines" "$changedSets"
[ "$docs" -gt 0 ] || { echo "no document compared from $data" >&2; exit 2; }
if [ "$accept" -eq 1 ]; then
    echo "e2e: accepted $accepted documents into $data"
    exit 0
fi
[ "$changedLines" -eq 0 ]
