#!/bin/sh
# tools/pick.sh in a scratch repository: union of independent rows, and stops on a merge commit,
# a cherry-pick blocked by an untracked file, two sides changing the same row id and a stray path.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-pick-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
cd "$tmp"
git init -q -b main .
git config user.email test@example.invalid
git config user.name test
git config commit.gpgsign false
mkdir dicts
printf 'key\tko\tid\nおもむろに\t천천히\tw:198\n' > dicts/t.tsv
git add . && git commit -qm base
branch() { git switch -q -c "$1" main; }
edit() { printf '%s\n' "$2" >> dicts/t.tsv; git commit -qam "$1"; }
branch a; edit a "猫	고양이	w:1"; a=$(git rev-parse HEAD)
branch b; edit b "犬	개	w:2"; b=$(git rev-parse HEAD)
branch c; sed 's/천천히/느긋이/' dicts/t.tsv > x && mv x dicts/t.tsv && git commit -qam c; c=$(git rev-parse HEAD)
branch d; sed 's/천천히/서서히/' dicts/t.tsv > x && mv x dicts/t.tsv && git commit -qam d; d=$(git rev-parse HEAD)
branch e; echo x > new.txt && git add new.txt && git commit -qm e; e=$(git rev-parse HEAD)
branch f; echo x > ./--in && git add -- ./--in && git commit -qm f; f=$(git rev-parse HEAD)
branch m; git merge -q --no-ff -m m a; m=$(git rev-parse HEAD)
pick() { # EXPECTED-STATUS PATTERN COMMIT...
    want=$1 pattern=$2; shift 2
    status=0
    "$here/tools/pick.sh" "$@" > out.log 2>&1 || status=$?
    if [ "$status" -ne "$want" ] || ! grep -q -- "$pattern" out.log; then
        echo "pick $*: exit $status, wanted $want and '$pattern'"; cat out.log; exit 1
    fi
    git cherry-pick --abort > /dev/null 2>&1 || true
    git reset -q --hard
}
branch t1; pick 0 'picked' "$a" "$b"
[ "$(grep -c 'w:[12]$' dicts/t.tsv)" -eq 2 ] || { echo "union lost a row"; cat dicts/t.tsv; exit 1; }
branch t2; pick 1 'merge' "$m"
branch t3; echo y > new.txt; pick 1 'untracked' "$e"; rm new.txt
branch t4; pick 1 'conflict left' "$c" "$d"
branch t5; pick 1 'stray' "$f"
echo "pick: ok"
