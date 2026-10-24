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
# A TERM to the whole process group stops every set and leaves no translation running.
printf '#!/bin/sh\nexec sleep 30\n' > "$tmp/slow"
chmod +x "$tmp/slow"
python3 -I - "$here/tools/measure.sh" "$tmp/slow" "$dicts" <<'PY'
import os, signal, subprocess, sys, time
measure, slow, dicts = sys.argv[1:]
p = subprocess.Popen(["sh", measure, "-c", slow, "-d", dicts, "slow"], start_new_session=True,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
os.killpg(p.pid, signal.SIGTERM)
try:
    status = p.wait(timeout=20)
except subprocess.TimeoutExpired:
    os.killpg(p.pid, signal.SIGKILL)
    sys.exit("measure.sh did not stop")
time.sleep(1)
left = subprocess.run(["pgrep", "-g", str(p.pid)], capture_output=True, text=True).stdout.split()
if left:
    os.killpg(p.pid, signal.SIGKILL)
    sys.exit(f"left running: {left}")
assert status != 0, status
PY
# The audit draws the same lines again and rates a grade file.
python3 -I "$here/tools/audit.py" draw "$tmp/work/second" "$tmp/audit.tsv" --per-set 1
python3 -I "$here/tools/audit.py" draw "$tmp/work/second" "$tmp/again.tsv" --per-set 1
cmp "$tmp/audit.tsv" "$tmp/again.tsv"
[ "$(cut -f 1 "$tmp/audit.tsv" | cut -d : -f 1 | tr '\n' ' ')" = 'corpus sample docs examples ' ]
cut -f 1 "$tmp/audit.tsv" | awk '{print $0 "\twrong\ttest"}' > "$tmp/grades.tsv"
python3 -I "$here/tools/audit.py" rate "$tmp/audit.tsv" "$tmp/grades.tsv" | grep -F 'all' |
    grep -F '100.0' > /dev/null
