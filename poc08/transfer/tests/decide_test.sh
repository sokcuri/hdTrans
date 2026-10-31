#!/bin/sh
# Decider fixture: four invented lines, one per family, with 0.7.5's (wrong) class as base; the deciders
# must override each one, except ため, which is off (τ = 1) until it has more samples. usage: decide_test.sh HD8DECIDE DATA_DIR TEST_DIR
set -e
out=$("$1" --data "$2" --decide "$3/decide.base.tsv" < "$3/decide.ir.jsonl" |
  sed -e 's/.*"case":"\([^"]*\)".*"family":"\([^"]*\)".*"class":"\([^"]*\)".*/\1 \2 \3/')
expect=$(cat "$3/decide.expect")
[ "$out" = "$expect" ] || { echo "got:"; echo "$out"; echo "expected:"; echo "$expect"; exit 1; }
echo "deciders: 4/4"
# Two bias rows of one class (the intercept and the always-on feature "bias") add up: 2.5 − 2.0 = 0.5 > 0 → CHANGE.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp -R "$2/." "$tmp/"
printf '#! decider family=youni tau=0.0 id=youni@bias\nclasses\tCHANGE\tLIKE\nbias\tCHANGE\t2.5\nbias\tLIKE\t0\nbias\tCHANGE\t-2.0\n' \
  > "$tmp/decide/youni.tsv"
"$1" --data "$tmp" --decide "$3/decide.base.tsv" < "$3/decide.ir.jsonl" | grep -q '"case":"y1".*"class":"CHANGE"' ||
  { echo "bias rows do not add up"; exit 1; }
echo "bias rows: ok"
