#!/bin/sh
# Builds the preset and runs ctest with dicts/, a fresh HOME and no dictionary cache, so
# worktrees built in parallel do not share state. ctest covers the unit tests, the rule checks,
# the rule cases of every speech level and examples/; the gate fails when one of those is
# skipped instead of passing.
#   tools/gate.sh [NAME]
# HDTRANS_PRESET picks the Ninja preset (default mac on macOS, linux elsewhere). Logs go to
# ${HDTRANS_WORK:-build/work}/NAME (default: the branch name). Prints GATE OK on success.
# Exit: 0 passed, 1 failed.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
case $(uname -s) in Darwin) preset=${HDTRANS_PRESET:-mac} ;; *) preset=${HDTRANS_PRESET:-linux} ;; esac
name=${1:-$(git rev-parse --abbrev-ref HEAD | tr / -)}
out=${HDTRANS_WORK:-$here/build/work}/$name
mkdir -p "$out"
home=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-gate.XXXXXX")
trap 'rm -rf "$home"' EXIT
trap 'exit 1' HUP INT TERM
export HDTRANS_CACHE_DIR=off
cmake --build --preset "$preset" --parallel > "$out/build.log" 2>&1 ||
    { tail -n 20 "$out/build.log"; echo "GATE FAIL: build ($out/build.log)"; exit 1; }
HOME=$home HDTRANS_DICTS=$here/dicts ctest --preset "$preset" > "$out/ctest.log" 2>&1 ||
    { grep -E '\*\*\*|tests passed' "$out/ctest.log"; echo "GATE FAIL: ctest ($out/ctest.log)"; exit 1; }
for t in unit examples rules.cases rules.cases.themes rules.cases.speech-soft rules.cases.speech-strict; do
    grep -Eq "Test +#[0-9]+: $t \.+ +Passed" "$out/ctest.log" ||
        { echo "GATE FAIL: $t did not pass ($out/ctest.log)"; exit 1; }
done
tail -n 1 "$out/build.log"
grep 'tests passed' "$out/ctest.log"
echo "GATE OK"
