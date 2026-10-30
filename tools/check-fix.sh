#!/bin/sh
# One command from an edited tree to a judging packet: gate, base and head translations of the
# measurement sets, the changed lines, and the few of them to judge.
#   tools/check-fix.sh [-b BASE_REF] [-B BASE_CLI] [-c CLI] [-n] NAME
# BASE_REF (default: the merge base of HEAD with develop) is built once in
# ${HDTRANS_WORK:-build/work}/bases/<commit> (a detached worktree; -B gives a built CLI instead) and
# its sets are translated once into the cache of tools/sets.py, so every later fix on the same base
# only translates its own side. The head side is the work tree: when only dicts/ (or files that do
# not reach the engine) changed, it runs the base CLI on this tree's dicts/, so no build is needed;
# otherwise it builds the preset (or uses -c CLI). tools/affected.py lists the text the change can
# reach, and only lines that hold it are translated again, with a canary of
# ${HDTRANS_CANARY:-200} other lines per each-line set (tools/sets.py). tools/gate.sh runs at the
# same time; -n leaves it out.
# Sets: HDTRANS_SETS, specs of tools/sets.py separated by spaces (NAME=FILE[@doc|@blocks=N]); by
# default corpus=$HDTRANS_CORPUS and sample=$HDTRANS_REF_SAMPLE/ja.txt, each line its own session.
# Output in ${HDTRANS_WORK:-build/work}/NAME: needles.txt, base/ (the base run), <set>.txt,
# <set>.pairs.tsv, changed.tsv (all of them) and packet/
# (tools/judge-packet.py: judge packet/reps.pairs.tsv, then `expand`).
# Exit: 0 done, 1 gate failed or a canary line changed, 2 invalid arguments or failed step.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
case $(uname -s) in Darwin) preset=${HDTRANS_PRESET:-mac} ;; *) preset=${HDTRANS_PRESET:-linux} ;; esac
base_ref= base_cli= cli= gate=1
usage() { echo "usage: tools/check-fix.sh [-b BASE_REF] [-B BASE_CLI] [-c CLI] [-n] NAME" >&2; exit 2; }
while getopts b:B:c:n opt; do
    case $opt in
        b) base_ref=$OPTARG ;;
        B) base_cli=$OPTARG ;;
        c) cli=$OPTARG ;;
        n) gate= ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || usage
name=$(printf '%s' "$1" | tr / -)
work=${HDTRANS_WORK:-$here/build/work}
out=$work/$name
if [ -z "$base_ref" ]; then
    for ref in github/develop origin/develop develop; do
        base_ref=$(git merge-base HEAD "$ref" 2> /dev/null) && break || base_ref=
    done
    [ -n "$base_ref" ] || { echo "no develop branch to start from; give -b" >&2; exit 2; }
fi
sha=$(git rev-parse --verify "$base_ref^{commit}") || { echo "no commit $base_ref" >&2; exit 2; }
sets=${HDTRANS_SETS-}
if [ -z "$sets" ]; then
    [ -z "${HDTRANS_CORPUS-}" ] || sets="corpus=$HDTRANS_CORPUS"
    [ -z "${HDTRANS_REF_SAMPLE-}" ] || sets="$sets sample=$HDTRANS_REF_SAMPLE/ja.txt"
fi
[ -n "$sets" ] || { echo "set HDTRANS_SETS, HDTRANS_CORPUS or HDTRANS_REF_SAMPLE" >&2; exit 2; }
export HDTRANS_CACHE_DIR=off
mkdir -p "$out" "$work/bases"
rm -rf "$out/base" "$out/packet" "$out"/*.txt "$out"/*.json "$out"/*.tsv
elapsed() { python3 -c 'import sys, time; print(round(time.time() - float(sys.argv[1]), 1))' "$1"; }
now() { python3 -c 'import time; print(time.time())'; }
start=$(now)

# The base: a worktree of BASE_REF built once, shared by the fixes made on it.
base=$work/bases/$sha
if [ -z "$base_cli" ]; then
    base_cli=$base/tree/build/$preset/hdtrans
    if [ ! -x "$base_cli" ]; then
        until mkdir "$base.lock" 2> /dev/null; do echo "waiting for another build of $sha"; sleep 10; done
        trap 'rmdir "$base.lock" 2> /dev/null || :' EXIT
        trap 'exit 2' HUP INT TERM
        if [ ! -x "$base_cli" ]; then
            [ -d "$base/tree" ] || git worktree add --detach "$base/tree" "$sha" > "$base.log" 2>&1
            (cd "$base/tree" && cmake --preset "$preset" && cmake --build --preset "$preset" --parallel) \
                >> "$base.log" 2>&1 || { tail -n 20 "$base.log"; echo "base build failed ($base.log)" >&2; exit 2; }
        fi
        rmdir "$base.lock"
        trap - EXIT
    fi
    base_dicts=$base/tree/dicts
else
    rm -rf "$out/base-tree"
    mkdir -p "$out/base-tree"
    git archive "$sha" dicts | tar -x -C "$out/base-tree"
    base_dicts=$out/base-tree/dicts
fi
echo "base $sha: $base_cli ($(elapsed "$start") s)"

python3 -I tools/affected.py "$sha" > "$out/needles.txt"
if [ -z "$cli" ]; then
    if grep -qx '\*' "$out/needles.txt"; then
        cmake --build --preset "$preset" --parallel > "$out/build.log" 2>&1 ||
            { tail -n 20 "$out/build.log"; echo "build failed ($out/build.log)" >&2; exit 2; }
        cp "build/$preset/hdtrans" "$out/hdtrans"
        cli=$out/hdtrans
    else
        cli=$base_cli
    fi
fi
echo "head: $cli, $(grep -cv '^\*$' "$out/needles.txt" || :) needles$(grep -qx '\*' "$out/needles.txt" && echo ', every line' || :)"

# The gate runs while the sets translate.
status=0 gate_pid=
if [ -n "$gate" ]; then
    tools/gate.sh "$name-gate" > "$out/gate.log" 2>&1 &
    gate_pid=$!
    trap 'kill "$gate_pid" 2> /dev/null || :' EXIT
fi
echo "base sets:"
# shellcheck disable=SC2086
python3 -I tools/sets.py run --cache "$work/sets-cache" -c "$base_cli" -d "$base_dicts" "$out/base" $sets
echo "head sets:"
# shellcheck disable=SC2086
python3 -I tools/sets.py run -c "$cli" -d "$here/dicts" --base "$out/base" --needles "$out/needles.txt" \
    --canary "${HDTRANS_CANARY:-200}" "$out" $sets || { [ $? -eq 1 ] && status=1 || exit 2; }
python3 -I tools/sets.py pairs "$out/base" "$out"
cat /dev/null "$out"/*.pairs.tsv > "$out/changed.tsv"
python3 -I tools/judge-packet.py make "$out/packet" "$out/changed.tsv"
if [ -n "$gate_pid" ]; then
    wait "$gate_pid" || status=1
    trap - EXIT
    tail -n 1 "$out/gate.log"
fi
echo "done in $(elapsed "$start") s"
exit $status
