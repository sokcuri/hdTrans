#!/bin/sh
# Cherry-picks commits one by one onto the current branch for an integration.
#   tools/pick.sh COMMIT...
# Conflicts in the line tables (dicts/, src/core/dict/builtin/, tests/rules/) are resolved by
# keeping the lines of both sides (tools/union-tsv.py); program.hash and compiled_gen.cpp take
# ours and are regenerated (tools/regen-rules.sh) whenever the pick touches rule sources;
# examples/*.ko.txt take ours and are named so that examples/ is checked again. Any other
# conflict stops the run with the pick in progress.
# Before each commit it stops when tools/union-tsv.py --check finds something new (repeated rows,
# colliding ids, contradicting cases, rows the picked commit deleted), when a conflict marker is
# staged, when a staged file is not part of the picked commit, or when the commit adds a path
# starting with - or holding a space (a stray file named after a CLI option). Each commit keeps
# the message and author of the original. Resolve a stop by hand, run `git commit -C COMMIT`,
# then pick the rest.
# Exit: 0 all picked, 1 stopped, 2 invalid arguments or a dirty tree.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
[ $# -ge 1 ] || { echo "usage: tools/pick.sh COMMIT..." >&2; exit 2; }
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "commit or stash the changes first" >&2; exit 2
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-pick.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
gen='src/core/rules/program.hash src/core/rules/compiled_gen.cpp'
stop() { echo "STOP $c: $*"; exit 1; }
for c in "$@"; do
    c=$(git rev-parse --short "$c^{commit}") || exit 2
    python3 -I tools/union-tsv.py --check | grep -v '^CHECK ' | sort > "$tmp/before" || true
    regen=
    if ! git cherry-pick --no-commit "$c" > "$tmp/pick.log" 2>&1; then
        for f in $(git diff --name-only --diff-filter=U); do
            case $f in
                src/core/rules/program.hash|src/core/rules/compiled_gen.cpp)
                    git checkout --ours -- "$f"; git add "$f"; regen=1 ;;
                dicts/*.tsv|src/core/dict/builtin/*.tsv|tests/rules/*.tsv)
                    python3 -I tools/union-tsv.py "$f" || stop "conflict left in $f"
                    git add "$f" ;;
                examples/*.ko.txt)
                    git checkout --ours -- "$f"; git add "$f"
                    echo "EXAMPLE $c $f: took ours; check examples/ after the pick" ;;
                *) stop "conflict in $f" ;;
            esac
        done
    fi
    if git diff --cached --quiet; then
        git cherry-pick --quit 2> /dev/null || true
        echo "empty $c: already applied"; continue
    fi
    if git diff --cached --name-only | grep -Eq '^(dicts/|src/core/dict/builtin/|src/core/rules/globals\.txt)'; then
        regen=1
    fi
    if [ -n "$regen" ]; then
        tools/regen-rules.sh > "$tmp/regen.log" 2>&1 || { cat "$tmp/regen.log"; stop "regen failed"; }
        # shellcheck disable=SC2086
        git add $gen
    fi
    git diff --cached | grep -Eq '^\+(<<<<<<<|>>>>>>>|=======$)' && stop "conflict marker staged"
    odd=$(git diff --cached --name-only --diff-filter=A | grep -E '(^|/)-|[[:space:]]' || true)
    [ -z "$odd" ] || stop "added path looks stray: $odd"
    git show --format= --name-only "$c" > "$tmp/files"
    for g in $gen; do echo "$g"; done >> "$tmp/files"
    extra=$(git diff --cached --name-only | grep -vxF -f "$tmp/files" || true)
    [ -z "$extra" ] || stop "staged files not in $c: $extra"
    python3 -I tools/union-tsv.py --check "$c" | grep -v '^CHECK ' | sort > "$tmp/after" || true
    new=$(comm -13 "$tmp/before" "$tmp/after")
    [ -z "$new" ] || { echo "$new"; stop "table check"; }
    git commit -q -C "$c" || stop "commit failed"
    echo "picked $c -> $(git rev-parse --short HEAD)"
done
