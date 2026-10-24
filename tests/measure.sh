#!/bin/sh
# tools/measure.sh on all four sets, then tools/audit.py on the run.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cli=$1
dicts=$here/tests/unit/dict/testdata/folder
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-measure-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$tmp/sample" "$tmp/data/a" "$tmp/examples"
printf '学生\n猫\n' > "$tmp/corpus.txt"
printf '学生\n猫\n' > "$tmp/sample/ja.txt"
printf '학생\n고양이\n' > "$tmp/sample/ref.txt"
printf '# one\n学生\n猫\n' > "$tmp/data/a/one.ja.txt"
printf '# two\n猫\n' > "$tmp/data/a/two.ja.txt"
first=$(head -n 1 "$here/web/examples.tsv" | cut -f 1)
[ -f "$here/web/examples/$first" ] || printf '学生\n' > "$tmp/examples/$first"
export HDTRANS_WORK=$tmp/work HDTRANS_CORPUS=$tmp/corpus.txt HDTRANS_REF_SAMPLE=$tmp/sample \
    HDTRANS_TESTDATA=$tmp/data HDTRANS_EXAMPLES=$tmp/examples HDTRANS_JOBS=2
"$here/tools/measure.sh" -c "$cli" -d "$dicts" first > "$tmp/first.log" 2>&1
grep -F 'docs: 2 documents, 3 lines' "$tmp/first.log" > /dev/null
[ "$(awk 'END {print NR}' "$tmp/work/first/docs/a/one.ko.txt")" -eq 2 ]
[ -f "$tmp/work/first/examples/${first%.txt}.ko.txt" ]
# A changed line in the base run shows up in the pairs with the input line number.
printf 'x\n' > "$tmp/work/first/docs/a/two.ko.txt"
"$here/tools/measure.sh" -c "$cli" -d "$dicts" -b first second > "$tmp/second.log" 2>&1
grep -F 'docs: 1 changed lines in 1 of 2 documents' "$tmp/second.log" > /dev/null
grep -F 'examples: 0 changed lines' "$tmp/second.log" > /dev/null
grep -F 'corpus: 0 changed lines' "$tmp/second.log" > /dev/null
[ "$(cut -f 1,2 "$tmp/work/second/docs.pairs.tsv")" = "$(printf 'docs:a/two:2\t猫')" ]
# A base without the sets is reported as such, and a run cannot be its own base.
mkdir "$tmp/empty"
"$here/tools/measure.sh" -c "$cli" -d "$dicts" -b "$tmp/empty" fourth > "$tmp/fourth.log" 2>&1
grep '^docs: 2 documents in .*/fourth/docs; not in .*/empty$' "$tmp/fourth.log" > /dev/null
grep '^corpus: 2 lines in .*/fourth/corpus.txt; not in .*/empty$' "$tmp/fourth.log" > /dev/null
[ ! -e "$tmp/work/fourth/docs.pairs.tsv" ]
status=0
"$here/tools/measure.sh" -c "$cli" -d "$dicts" -b fourth fourth > /dev/null 2>&1 || status=$?
[ "$status" -eq 2 ]
# A failing set fails the run after the others have reported.
status=0
HDTRANS_TESTDATA=$tmp/sample "$here/tools/measure.sh" -c "$cli" -d "$dicts" third > "$tmp/third.log" 2>&1 ||
    status=$?
[ "$status" -eq 2 ]
grep -F 'corpus: 2 lines' "$tmp/third.log" > /dev/null
# A TERM to the whole process group stops every set and leaves no translation running. The fake
# CLI records its pid and blocks until killed; TERM goes out once all six translations (corpus,
# sample, two documents, two examples at HDTRANS_JOBS=2) have started. Only where GNU timeout
# moves the CLI into its own group does this show that measure.sh stops it itself.
printf '#!/bin/sh\necho $$ >> "%s/pids"\nexec tail -f /dev/null\n' "$tmp" > "$tmp/slow"
chmod +x "$tmp/slow"
python3 -I - "$here/tools/measure.sh" "$tmp/slow" "$dicts" "$tmp/pids" "$tmp" <<'PY'
import os, signal, subprocess, sys, time
measure, slow, dicts, pids, tmp = sys.argv[1:]
p = subprocess.Popen(["sh", measure, "-c", slow, "-d", dicts, "slow"], start_new_session=True,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def started():
    try:
        return [int(x) for x in open(pids).read().split()]
    except FileNotFoundError:
        return []


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


def give_up(message):
    listing = subprocess.run(["ps", "-ax", "-o", "pid,ppid,pgid,stat,command"],
                             capture_output=True, text=True).stdout.splitlines()
    print("\n".join(l for l in listing if tmp in l or "documents.py" in l or "tail -f /dev/null" in l), file=sys.stderr)
    for x in started():
        if alive(x):
            os.kill(x, signal.SIGKILL)
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    sys.exit(message)


def until(test, seconds):
    deadline = time.monotonic() + seconds
    while not test() and time.monotonic() < deadline:
        time.sleep(0.1)
    return test()


if not until(lambda: len(started()) >= 6 or p.poll() is not None, 60) or p.poll() is not None:
    give_up(f"{len(started())} translations started, measure.sh status {p.poll()}")
# Several TERMs in a row, as an impatient caller or a timeout would send them.
for _ in range(5):
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except OSError:
        break
    time.sleep(0.02)
if not until(lambda: p.poll() is not None, 60):
    give_up("measure.sh did not stop")
if not until(lambda: not any(alive(x) for x in started()), 10):
    give_up(f"left running: {[x for x in started() if alive(x)]}")
assert p.returncode != 0, p.returncode
PY
# The audit draws the same lines again and rates a grade file.
python3 -I "$here/tools/audit.py" draw "$tmp/work/second" "$tmp/audit.tsv" --per-set 1
python3 -I "$here/tools/audit.py" draw "$tmp/work/second" "$tmp/again.tsv" --per-set 1
cmp "$tmp/audit.tsv" "$tmp/again.tsv"
[ "$(cut -f 1 "$tmp/audit.tsv" | cut -d : -f 1 | tr '\n' ' ')" = 'corpus sample docs examples ' ]
cut -f 1 "$tmp/audit.tsv" | awk '{print $0 "\twrong\ttest"}' > "$tmp/grades.tsv"
python3 -I "$here/tools/audit.py" rate "$tmp/audit.tsv" "$tmp/grades.tsv" | grep -F 'all' |
    grep -F '100.0' > /dev/null
