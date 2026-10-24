#!/bin/sh
# Checks a table of rule cases (or every table in a directory) against a built CLI.
#   tools/rule-cases.sh CLI DICTS [CASES] [--accept] [-- CLI-OPTIONS...]
# CASES (default tests/rules/cases/) is a TSV with the header `rule input expected kind layer`, or a
# directory of them. Every input is translated as its own session (-each -why). `expected` is one of
#   the whole Korean output;
#   rule:dict:<row id>=<target>  some word of the line is chosen by the local rule that the
#                                dictionary row <row id> carries, with that target (the dictionary
#                                translation as -why prints it);
#   rule:L<n>=<target>           the same with the local rule number;
#   rule:cx:<family>:<prio>=<target>  some word or ending of the line takes that target from the
#                                construction row (src/core/dict/builtin/constructions.tsv);
#   !rule:dict:<row id>, !rule:cx:<family>:<prio>  that rule or row chooses nothing in the line;
#   has:<text>, lacks:<text>     the whole output holds <text>, or does not.
# `rule` names what the row tests: dict:<row id> or L<n> for a local rule, G<prio> for a global
# rule, cx:<family>:<prio> for a construction row, else <stage>:<name> (sound:, constraint:,
# rewrite:, compose:, normalize:, sense:, ...).
# kind is fix, keep or current. layer is the execution layer that decides the line
# (docs/pipeline-layers.md: local, local-ending, global-stem, global-last, global-ending, lookup,
# defaults, senses, rewrites, compose, normalize, sound, restore, ...); failures are grouped by it.
# Failing rows are reported grouped by layer, each with its rule, input, expected and actual
# value, so that a change in one layer that breaks another layer's rows shows as such.
# --accept rewrites the expected value of every full-output row whose kind is current. --no-current
# fails every row whose kind is current (tests/rules/critical/ takes fix and keep only). The words
# after -- are passed to the CLI (--speech strict).
# Exit: 0 all rows pass, 1 a row fails, 2 invalid arguments or failed CLI.
set -eu
LC_ALL=C; export LC_ALL
[ $# -ge 2 ] || { echo "usage: tools/rule-cases.sh CLI DICTS [CASES] [--accept] [--no-current]" >&2; exit 2; }
cli=$1 dicts=$2; shift 2
here=$(cd "$(dirname "$0")/.." && pwd)
cases=$here/tests/rules/cases accept= nocurrent=
while [ $# -gt 0 ]; do
    case $1 in
        --accept) accept=1 ;;
        --no-current) nocurrent=1 ;;
        --) shift; break ;;
        *) cases=$1 ;;
    esac
    shift
done
[ -e "$cases" ] || { echo "no cases at $cases" >&2; exit 2; }
[ -d "$dicts" ] || { echo "no dictionaries at $dicts" >&2; exit 2; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-rules.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM
# Lines starting with # (the ids of the rules a table enforces, spec/) precede the header and are
# not rows. A directory holds one table per theme (tests/rules/cases/*.tsv); they are checked together.
src=$cases
if [ -d "$cases" ]; then
    [ -z "$accept" ] || { echo "--accept needs a single file" >&2; exit 2; }
    first=1
    for f in "$cases"/*.tsv; do
        grep -v '^#' "$f" > "$tmp/one.tsv" || true
        head -n 1 "$tmp/one.tsv" | grep -q '^rule	input	expected	kind	layer$' || { echo "bad header in $f" >&2; exit 2; }
        if [ -n "$first" ]; then cat "$tmp/one.tsv"; first=; else tail -n +2 "$tmp/one.tsv"; fi
    done > "$tmp/all.tsv"
    cases=$tmp/all.tsv
else
    grep '^#' "$cases" > "$tmp/notes.tsv" || true
    grep -v '^#' "$cases" > "$tmp/one.tsv" || true
    cases=$tmp/one.tsv
fi
head -n 1 "$cases" | grep -q '^rule	input	expected	kind	layer$' || { echo "bad header in $cases" >&2; exit 2; }
tail -n +2 "$cases" | cut -f 2 > "$tmp/in.txt"
"$cli" -dicts "$dicts" -each -why "$@" -in "$tmp/in.txt" > "$tmp/out.txt" 2> "$tmp/why.txt" ||
    { echo "CLI failed" >&2; exit 2; }
# The dictionary row id names the rule a row carries; a case may use either.
awk -F '\t' 'FNR == 1 { seen = 0 } !/^#/ && !seen++ { for (i = 1; i <= NF; i++) c[$i] = i; next } /^#/ { next } $c["rule"] != "" && $c["id"] != "" { print $c["id"] "\t" "L" $c["rule"] }' "$dicts"/*.tsv > "$tmp/rule-by-id.tsv"
awk -F '\t' -v OFS='\t' -v accept="$accept" -v nocurrent="$nocurrent" -v fixed="$tmp/fixed.tsv" \
    -f "$here/tools/rule-cases.awk" \
 "$tmp/why.txt" "$tmp/out.txt" "$tmp/rule-by-id.tsv" "$cases" || status=$?
[ -z "$accept" ] || cat "$tmp/notes.tsv" "$tmp/fixed.tsv" > "$src"
exit "${status:-0}"
