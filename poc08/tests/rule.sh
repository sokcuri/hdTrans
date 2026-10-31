#!/bin/sh
# 0.8-rule fix and keep cases: usage tests/rule.sh HDTRANS08 HD8RETRO DICTS
# rule.cases.tsv rows: kind, input, expected, note. kind "out": hdtrans08 --poc08 rule writes expected for
# the input line (one-line document). kind "retro": the retrofit's own text for the line, before the gate that
# keeps lines whose speech level does not change (so reader and generator faults show on any line), contains
# expected.
set -u
b=$1 retro=$2 dicts=$3
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp" HDTRANS_CACHE_DIR=off
fail=0
n=0
grep -v '^#' "$here/tests/rule.cases.tsv" | tail -n +2 > "$tmp/cases"
while IFS="$(printf '\t')" read -r kind input expected note; do
    n=$((n + 1))
    printf '%s\n' "$input" > "$tmp/in.txt"
    "$b" --dicts "$dicts" --poc08 rule --poc08-rule-pre "$tmp/pre.jsonl" --in "$tmp/in.txt" > "$tmp/out.txt" || fail=1
    case "$kind" in
    out)
        got=$(cat "$tmp/out.txt")
        [ "$got" = "$expected" ] || { echo "FAIL $n ($note): got $got, want $expected"; fail=1; } ;;
    retro)
        "$retro" --data "$here/generator/data" --rules none "$tmp/pre.jsonl" > "$tmp/retro.jsonl" 2> /dev/null || fail=1
        got=$(python3 -c 'import json,sys; print(json.loads(open(sys.argv[1]).readline())["gen"])' "$tmp/retro.jsonl")
        case "$got" in
        *"$expected"*) ;;
        *) echo "FAIL $n ($note): got $got, want it to contain $expected"; fail=1 ;;
        esac ;;
    *) echo "FAIL $n: unknown kind $kind"; fail=1 ;;
    esac
done < "$tmp/cases"
[ "$n" -gt 0 ] || { echo "FAIL: no cases"; fail=1; }
exit $fail
