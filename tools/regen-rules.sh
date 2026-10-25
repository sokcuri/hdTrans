#!/bin/sh
# Regenerates src/core/rules/program.hash and compiled_gen.cpp from the rules program the built
# CLI assembles from dicts/ and globals.txt (docs/rule-compiler.md), then rebuilds.
#   tools/regen-rules.sh
# HDTRANS_PRESET picks the Ninja preset (default mac on macOS, linux elsewhere). Build output
# goes to build/regen.log; its tail is shown when a build fails.
# Exit: 0 regenerated, 1 build or lint failed.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
case $(uname -s) in Darwin) preset=${HDTRANS_PRESET:-mac} ;; *) preset=${HDTRANS_PRESET:-linux} ;; esac
bin=build/$preset
export HDTRANS_CACHE_DIR=off
build() {
    cmake --build --preset "$preset" --parallel > build/regen.log 2>&1 ||
        { tail -n 30 build/regen.log >&2; echo "build failed (build/regen.log)" >&2; exit 1; }
}
build
"$bin/hdtrans" -dicts dicts --dump-rules build/rules.txt -in tests/empty.in
lint=$("$bin/hdrulec" lint build/rules.txt -dicts dicts 2>&1 > /dev/null | tail -n 1)
case $lint in
    "0 error(s)"*) ;;
    *) echo "lint: $lint (hdrulec lint build/rules.txt -dicts dicts)" >&2; exit 1 ;;
esac
"$bin/hdrulec" dump build/rules.txt -hash > src/core/rules/program.hash
"$bin/hdrulec" cpp build/rules.txt -globals -o src/core/rules/compiled_gen.cpp
build
