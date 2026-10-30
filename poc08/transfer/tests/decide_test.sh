#!/bin/sh
# Decider fixture: four invented lines, one per family, with 0.7.5's (wrong) class as base; the deciders
# must override each one, except ため, which is off (τ = 1) until it has more samples. usage: decide_test.sh HD8DECIDE DATA_DIR TEST_DIR
set -e
out=$("$1" --data "$2" --decide "$3/decide.base.tsv" < "$3/decide.ir.jsonl" |
  sed -e 's/.*"case":"\([^"]*\)".*"family":"\([^"]*\)".*"class":"\([^"]*\)".*/\1 \2 \3/')
expect=$(cat "$3/decide.expect")
[ "$out" = "$expect" ] || { echo "got:"; echo "$out"; echo "expected:"; echo "$expect"; exit 1; }
echo "deciders: 4/4"
