#!/bin/sh
# Translates the private line-aligned sets with one CLI and compares them with an earlier run.
#   tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME [-- CLI-OPTIONS...]
# Sets, each skipped when its variable is unset:
#   HDTRANS_CORPUS       a text file, one sentence per line          -> NAME/corpus.txt
#   HDTRANS_GAME_SAMPLE  a folder with ja.txt and line-aligned ref.txt -> NAME/sample.txt
# Every line is its own session (--each), with a fresh HOME, no cache and no user dictionary.
# Runs live in ${HDTRANS_WORK:-build/work}/NAME. BASE is another run name there or a run folder;
# with it, changed lines go to NAME/corpus.diff.tsv (line, ja, before, after) and
# NAME/sample.diff.tsv (line, ja, ref, before, after), and the sample prints chrF before -> after
# with the changed, better and worse line counts (tools/chrf.py). Without it the sample prints chrF.
# CLI defaults to build/<preset>/hdtrans and DICTS to dicts/ of this tree; measure a baseline
# build with -c and -d pointing into its worktree.
# Exit: 0 measured, 2 invalid arguments, no set or failed CLI.
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
name=$1; shift
[ "${1-}" != -- ] || shift
work=${HDTRANS_WORK:-$here/build/work}
out=$work/$name
if [ -n "$base" ] && [ ! -d "$base" ]; then base=$work/$base; fi
[ -z "$base" ] || [ -d "$base" ] || { echo "no run at $base" >&2; exit 2; }
[ -x "$cli" ] || { echo "no CLI at $cli" >&2; exit 2; }
[ -n "${HDTRANS_CORPUS-}${HDTRANS_GAME_SAMPLE-}" ] || { echo "set HDTRANS_CORPUS or HDTRANS_GAME_SAMPLE" >&2; exit 2; }
mkdir -p "$out"
home=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-measure.XXXXXX")
trap 'rm -rf "$home"' EXIT
trap 'exit 2' HUP INT TERM

translate() { # INPUT OUTPUT [CLI-OPTIONS...]
    in=$1 to=$2; shift 2
    HOME=$home HDTRANS_CACHE_DIR=off "$cli" --dicts "$dicts" --userdict '' --each "$@" --in "$in" --out "$to" ||
        { echo "CLI failed on $in" >&2; exit 2; }
    [ "$(wc -l < "$in")" -eq "$(wc -l < "$to")" ] || { echo "line count of $to differs from $in" >&2; exit 2; }
}

# Writes the lines where BEFORE and AFTER differ, with the line number and the other columns.
changed() { # OUT BEFORE AFTER COLUMN...
    python3 -I - "$@" <<'PY'
import sys
out, before, after, *cols = sys.argv[1:]
read = lambda p: open(p, encoding="utf-8").read().split("\n")
b, a, cs = read(before), read(after), [read(c) for c in cols]
n = 0
with open(out, "w", encoding="utf-8") as w:
    for i, (x, y) in enumerate(zip(b, a)):
        if x != y:
            n += 1
            w.write("\t".join([str(i + 1), *(c[i].replace("\t", " ") for c in cs), x, y]) + "\n")
print(n)
PY
}

if [ -n "${HDTRANS_CORPUS-}" ]; then
    translate "$HDTRANS_CORPUS" "$out/corpus.txt" "$@"
    if [ -n "$base" ] && [ -f "$base/corpus.txt" ]; then
        n=$(changed "$out/corpus.diff.tsv" "$base/corpus.txt" "$out/corpus.txt" "$HDTRANS_CORPUS")
        echo "corpus: $n changed lines ($out/corpus.diff.tsv: line, ja, before, after)"
    else
        echo "corpus: $(wc -l < "$out/corpus.txt") lines in $out/corpus.txt"
    fi
fi
if [ -n "${HDTRANS_GAME_SAMPLE-}" ]; then
    ja=$HDTRANS_GAME_SAMPLE/ja.txt ref=$HDTRANS_GAME_SAMPLE/ref.txt
    translate "$ja" "$out/sample.txt" "$@"
    if [ -n "$base" ] && [ -f "$base/sample.txt" ]; then
        printf 'sample: '; python3 -I "$here/tools/chrf.py" "$ref" "$base/sample.txt" "$out/sample.txt"
        changed "$out/sample.diff.tsv" "$base/sample.txt" "$out/sample.txt" "$ja" "$ref" > /dev/null
        echo "  ($out/sample.diff.tsv: line, ja, ref, before, after)"
    else
        printf 'sample: '; python3 -I "$here/tools/chrf.py" "$ref" "$out/sample.txt"
    fi
fi
