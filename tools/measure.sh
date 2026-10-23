#!/bin/sh
# Translates the private line-aligned sets with one CLI and compares them with an earlier run.
#   tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME [-- CLI-OPTIONS...]
# Sets, each skipped when its variable is unset:
#   HDTRANS_CORPUS      a text file, one sentence per line              -> NAME/corpus.txt
#   HDTRANS_REF_SAMPLE  a folder with ja.txt and line-aligned ref.txt   -> NAME/sample.txt
# Every line is its own session (--each), with a fresh HOME, no cache and no user dictionary.
# The CLI is copied into the run first, so a rebuild during the run cannot mix two builds.
# Runs live in ${HDTRANS_WORK:-build/work}/NAME (a / in NAME becomes -). BASE is another run name
# there or a run folder; with it, the changed lines go to NAME/corpus.pairs.tsv and
# NAME/sample.pairs.tsv in the format of tools/judge.py (key set:line, ja, before, after; CSV-style
# TSV quoting), and the sample prints chrF before -> after with the changed, better and worse line
# counts (tools/chrf.py). Without it the sample prints chrF. Lines are split at LF only.
# CLI defaults to build/<preset>/hdtrans and DICTS to dicts/ of this tree; measure a baseline
# build with -c and -d pointing into its worktree. Each set runs under
# tools/with-timeout.sh ${HDTRANS_TIMEOUT:-1800}.
# Exit: 0 measured, 2 invalid arguments, no set, failed CLI or line counts that differ.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
case $(uname -s) in Darwin) preset=${HDTRANS_PRESET:-mac} ;; *) preset=${HDTRANS_PRESET:-linux} ;; esac
cli=$here/build/$preset/hdtrans dicts=$here/dicts base=
usage() { echo "usage: tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME [-- CLI-OPTIONS...]" >&2; exit 2; }
while getopts c:d:b: opt; do
    case $opt in
        c) cli=$OPTARG ;;
        d) dicts=$OPTARG ;;
        b) base=$OPTARG ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || usage
name=$(printf '%s' "$1" | tr / -); shift
[ "${1-}" != -- ] || shift
work=${HDTRANS_WORK:-$here/build/work}
out=$work/$name
if [ -n "$base" ] && [ ! -d "$base" ]; then base=$work/$(printf '%s' "$base" | tr / -); fi
[ -z "$base" ] || [ -d "$base" ] || { echo "no run at $base" >&2; exit 2; }
[ -x "$cli" ] || { echo "no CLI at $cli" >&2; exit 2; }
[ -n "${HDTRANS_CORPUS-}${HDTRANS_REF_SAMPLE-}" ] || { echo "set HDTRANS_CORPUS or HDTRANS_REF_SAMPLE" >&2; exit 2; }
mkdir -p "$out"
cp "$cli" "$out/hdtrans"
home=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-measure.XXXXXX")
trap 'rm -rf "$home"' EXIT
trap 'exit 2' HUP INT TERM

# Line count with LF as the only break; a missing final LF still ends a line.
count() { python3 -I -c 'import sys; d = open(sys.argv[1], "rb").read(); print(d.count(b"\n") + (not d.endswith(b"\n") and d != b""))' "$1"; }

translate() { # INPUT OUTPUT [CLI-OPTIONS...]
    in=$1 to=$2; shift 2
    HOME=$home HDTRANS_CACHE_DIR=off "$here/tools/with-timeout.sh" "${HDTRANS_TIMEOUT:-1800}" \
        "$out/hdtrans" --dicts "$dicts" --userdict '' --each "$@" --in "$in" --out "$to" ||
        { echo "CLI failed on $in" >&2; exit 2; }
    [ "$(count "$in")" -eq "$(count "$to")" ] ||
        { echo "line counts differ: $in $(count "$in"), $to $(count "$to")" >&2; exit 2; }
}

# Writes the changed lines as judge pairs and prints their number.
pairs() { # SET JA BEFORE AFTER OUT
    python3 -I - "$@" <<'PY'
import csv, sys
name, ja, before, after, out = sys.argv[1:]
def lines(path):
    data = open(path, "rb").read().decode("utf-8")
    return (data[:-1] if data.endswith("\n") else data).split("\n")
j, b, a = lines(ja), lines(before), lines(after)
if not len(j) == len(b) == len(a):
    sys.exit(f"line counts differ: {ja} {len(j)}, {before} {len(b)}, {after} {len(a)}")
n = 0
with open(out, "w", encoding="utf-8", newline="") as w:
    writer = csv.writer(w, delimiter="\t", lineterminator="\n")
    for i, (x, y, z) in enumerate(zip(j, b, a), 1):
        if y != z:
            n += 1
            writer.writerow([f"{name}:{i}", x, y, z])
print(n)
PY
}

if [ -n "${HDTRANS_CORPUS-}" ]; then
    translate "$HDTRANS_CORPUS" "$out/corpus.txt" "$@"
    if [ -n "$base" ] && [ -f "$base/corpus.txt" ]; then
        n=$(pairs corpus "$HDTRANS_CORPUS" "$base/corpus.txt" "$out/corpus.txt" "$out/corpus.pairs.tsv")
        echo "corpus: $n changed lines ($out/corpus.pairs.tsv)"
    else
        echo "corpus: $(count "$out/corpus.txt") lines in $out/corpus.txt"
    fi
fi
if [ -n "${HDTRANS_REF_SAMPLE-}" ]; then
    ja=$HDTRANS_REF_SAMPLE/ja.txt ref=$HDTRANS_REF_SAMPLE/ref.txt
    [ "$(count "$ja")" -eq "$(count "$ref")" ] || { echo "line counts differ: $ja $ref" >&2; exit 2; }
    translate "$ja" "$out/sample.txt" "$@"
    if [ -n "$base" ] && [ -f "$base/sample.txt" ]; then
        printf 'sample: '; python3 -I "$here/tools/chrf.py" "$ref" "$base/sample.txt" "$out/sample.txt"
        n=$(pairs sample "$ja" "$base/sample.txt" "$out/sample.txt" "$out/sample.pairs.tsv")
        echo "  $n changed lines ($out/sample.pairs.tsv; references: line N of $ref)"
    else
        printf 'sample: '; python3 -I "$here/tools/chrf.py" "$ref" "$out/sample.txt"
    fi
fi
