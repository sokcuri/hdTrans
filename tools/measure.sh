#!/bin/sh
# Translates the private sets with one CLI and compares them with an earlier run.
#   tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME [-- CLI-OPTIONS...]
# Sets, each skipped when its variable is unset:
#   HDTRANS_CORPUS      a text file, one sentence per line             -> NAME/corpus.txt
#   HDTRANS_REF_SAMPLE  a folder with ja.txt and line-aligned ref.txt  -> NAME/sample.txt
#   HDTRANS_TESTDATA    the test-data folder of tools/e2e.sh           -> NAME/docs/<folder>/<name>.ko.txt
#   HDTRANS_EXAMPLES    the web example texts not in web/examples/     -> NAME/examples/<name>.ko.txt
# corpus and sample translate every line as its own session (--each). docs translates each
# DATA/<folder>/<name>.ja.txt without its first `#` line as one document, like tools/e2e.sh, and its
# output holds only the translation, without that line. examples translates each file that
# web/examples.tsv lists with a label, from web/examples/ or HDTRANS_EXAMPLES, as one document,
# like the web demo. Every CLI run has a fresh HOME, no cache and no user
# dictionary, and is limited by tools/with-timeout.sh ${HDTRANS_TIMEOUT:-1800}. The sets run at
# once; docs and examples translate ${HDTRANS_JOBS:-<CPU count>} documents at a time.
# The CLI is copied into the run first, so a rebuild during the run cannot mix two builds.
# Runs live in ${HDTRANS_WORK:-build/work}/NAME (a / in NAME becomes -). BASE is another run name
# there or a run folder other than NAME; with it, the changed lines of each set go to NAME/<set>.pairs.tsv in the
# format of tools/judge.py (key, ja, before, after; CSV-style TSV quoting). Keys are corpus:LINE,
# sample:LINE, docs:<folder>/<name>:LINE and examples:<file>:LINE, LINE counted in the input file.
# The sample prints chrF before -> after with the changed, better and worse line counts
# (tools/chrf.py); without BASE it prints chrF. A set the base run lacks is reported as such and
# gets no pairs. Earlier outputs of NAME are removed first. Lines are split at LF only.
# CLI defaults to build/<preset>/hdtrans and DICTS to dicts/ of this tree; measure a baseline
# build with -c and -d pointing into its worktree.
# Exit: 0 measured, 2 invalid arguments, no set, failed CLI or line counts that differ.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
case $(uname -s) in Darwin) preset=${HDTRANS_PRESET:-mac} ;; *) preset=${HDTRANS_PRESET:-linux} ;; esac
cli=$here/build/$preset/hdtrans dicts=$here/dicts base=
usage() { echo "usage: tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME [-- CLI-OPTIONS...]" >&2; exit 2; }
while getopts c:d:b: opt; do
    case $opt in
        c) cli=$OPTARG ;;
        d) dicts=$OPTARG ;;
        b) base=$OPTARG ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || usage
name=$(printf '%s' "$1" | tr / -); shift
[ "${1-}" != -- ] || shift
work=${HDTRANS_WORK:-$here/build/work}
out=$work/$name
if [ -n "$base" ] && [ ! -d "$base" ]; then base=$work/$(printf '%s' "$base" | tr / -); fi
[ -z "$base" ] || [ -d "$base" ] || { echo "no run at $base" >&2; exit 2; }
[ -z "$base" ] || [ "$(cd "$base" && pwd -P)" != "$(mkdir -p "$out" && cd "$out" && pwd -P)" ] ||
    { echo "the base run is the run itself: $base" >&2; exit 2; }
[ -x "$cli" ] || { echo "no CLI at $cli" >&2; exit 2; }
[ -n "${HDTRANS_CORPUS-}${HDTRANS_REF_SAMPLE-}${HDTRANS_TESTDATA-}${HDTRANS_EXAMPLES-}" ] ||
    { echo "set HDTRANS_CORPUS, HDTRANS_REF_SAMPLE, HDTRANS_TESTDATA or HDTRANS_EXAMPLES" >&2; exit 2; }
for dir in "${HDTRANS_TESTDATA-}" "${HDTRANS_EXAMPLES-}"; do
    [ -z "$dir" ] || [ -d "$dir" ] || { echo "no folder at $dir" >&2; exit 2; }
done
mkdir -p "$out"
rm -rf "$out/docs" "$out/examples" "$out/corpus.txt" "$out/sample.txt" "$out"/*.pairs.tsv
cp "$cli" "$out/hdtrans"
home=$(mktemp -d "${TMPDIR:-/tmp}/hdtrans-measure.XXXXXX")
pids=
trap 'rm -rf "$home"' EXIT
trap '[ -z "$pids" ] || { kill $pids 2> /dev/null; wait; }; exit 2' HUP INT TERM

# Each set runs in a background subshell, where `stoppable` starts its long command; a TERM then
# stops that command too.
child=
stoppable() { # COMMAND [ARG...]
    "$@" &
    child=$!
    wait "$child"
}
# ${!-} needs the sets to start as compound commands (the case below): under set -u, bash 3.2
# leaves $! unset in a subshell where no job has started.
stops() {
    last=${!-}
    trap '[ "${!-}" = "$last" ] || child=${child:-$!}
        [ -z "$child" ] || { kill "$child" 2> /dev/null; wait "$child"; }; exit 2' TERM
}

# Line count with LF as the only break; a missing final LF still ends a line.
count() { python3 -I -c 'import sys; d = open(sys.argv[1], "rb").read(); print(d.count(b"\n") + (not d.endswith(b"\n") and d != b""))' "$1"; }

translate() { # INPUT OUTPUT [CLI-OPTIONS...]
    in=$1 to=$2; shift 2
    HOME=$home HDTRANS_CACHE_DIR=off stoppable "$here/tools/with-timeout.sh" "${HDTRANS_TIMEOUT:-1800}" \
        "$out/hdtrans" --dicts "$dicts" --userdict '' --each "$@" --in "$in" --out "$to" ||
        { echo "CLI failed on $in" >&2; exit 2; }
    [ "$(count "$in")" -eq "$(count "$to")" ] ||
        { echo "line counts differ: $in $(count "$in"), $to $(count "$to")" >&2; exit 2; }
}

# Writes the changed lines as judge pairs and prints their number.
pairs() { # SET JA BEFORE AFTER OUT
    python3 -I - "$@" <<'PY'
import csv, sys
name, ja, before, after, out = sys.argv[1:]
def lines(path):
    data = open(path, "rb").read().decode("utf-8")
    return (data[:-1] if data.endswith("\n") else data).split("\n")
j, b, a = lines(ja), lines(before), lines(after)
if not len(j) == len(b) == len(a):
    sys.exit(f"line counts differ: {ja} {len(j)}, {before} {len(b)}, {after} {len(a)}")
n = 0
with open(out, "w", encoding="utf-8", newline="") as w:
    writer = csv.writer(w, delimiter="\t", lineterminator="\n")
    for i, (x, y, z) in enumerate(zip(j, b, a), 1):
        if y != z:
            n += 1
            writer.writerow([f"{name}:{i}", x, y, z])
print(n)
PY
}

corpus() { # [CLI-OPTIONS...]
    stops
    translate "$HDTRANS_CORPUS" "$out/corpus.txt" "$@"
    if [ -n "$base" ] && [ -f "$base/corpus.txt" ]; then
        n=$(pairs corpus "$HDTRANS_CORPUS" "$base/corpus.txt" "$out/corpus.txt" "$out/corpus.pairs.tsv")
        echo "corpus: $n changed lines ($out/corpus.pairs.tsv)"
    else
        echo "corpus: $(count "$out/corpus.txt") lines in $out/corpus.txt${base:+; not in $base}"
    fi
}

sample() { # [CLI-OPTIONS...]
    stops
    ja=$HDTRANS_REF_SAMPLE/ja.txt ref=$HDTRANS_REF_SAMPLE/ref.txt
    [ "$(count "$ja")" -eq "$(count "$ref")" ] || { echo "line counts differ: $ja $ref" >&2; exit 2; }
    translate "$ja" "$out/sample.txt" "$@"
    if [ -n "$base" ] && [ -f "$base/sample.txt" ]; then
        printf 'sample: '; python3 -I "$here/tools/chrf.py" "$ref" "$base/sample.txt" "$out/sample.txt"
        n=$(pairs sample "$ja" "$base/sample.txt" "$out/sample.txt" "$out/sample.pairs.tsv")
        echo "  $n changed lines ($out/sample.pairs.tsv; references: line N of $ref)"
    else
        printf 'sample: '; python3 -I "$here/tools/chrf.py" "$ref" "$out/sample.txt"
        [ -z "$base" ] || echo "  not in $base"
    fi
}

# Translates the documents of SET (docs or examples) in parallel; with BASE, writes their changed
# lines as judge pairs.
cat > "$home/documents.py" <<'PY'
import csv, os, signal, subprocess, sys, threading
from concurrent.futures import FIRST_EXCEPTION, ThreadPoolExecutor, wait
from pathlib import Path
here, out, base, dicts, home, timeout, jobs, name, *options = sys.argv[1:]
here, out, home = Path(here), Path(out), Path(home)
running, lock = set(), threading.Lock()


def stop(*_):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    with lock:
        for process in running:
            process.terminate()
        for process in running:
            process.wait()
    os._exit(2)


signal.signal(signal.SIGTERM, stop)


def fail(message):
    print(message, file=sys.stderr)
    stop()


docs = []  # (id, input, 1 when the first line is a header that is not translated)
if name == "docs":
    root = Path(os.environ["HDTRANS_TESTDATA"])
    for path in sorted(root.glob("*/*.ja.txt")):
        if not path.read_bytes().startswith(b"#"):
            fail(f"{path}: first line is not a # line")
        docs.append((f"{path.parent.name}/{path.name[:-len('.ja.txt')]}", path, 1))
else:
    root = Path(os.environ["HDTRANS_EXAMPLES"])
    for row in (here / "web/examples.tsv").read_text(encoding="utf-8").splitlines():
        file, _, label = row.partition("\t")
        if not file or not label:
            continue
        found = [p for p in (here / "web/examples" / file, root / file) if p.is_file()]
        if found:
            docs.append((file, found[0], 0))
        else:
            print(f"{name}: {file} not found", file=sys.stderr)
if not docs:
    fail(f"{name}: no document in {root}")


def lines(data):
    return (data[:-1] if data.endswith("\n") else data).split("\n")


def output(doc):
    return out / name / (doc.removesuffix(".txt") + ".ko.txt")


env = dict(os.environ, HOME=str(home), HDTRANS_CACHE_DIR="off")
for var in ("HDTRANS_SNAPSHOT", "HDTRANS_VERIFY_DICTS", "HDTRANS_DRIVER_DUMP"):
    env.pop(var, None)


def translate(number, doc, path, header):
    text = path.read_bytes().decode("utf-8")
    if header:
        text = text.partition("\n")[2]
    body = home / f"{name}.{number}.txt"
    body.write_bytes(text.encode("utf-8"))
    to = output(doc)
    to.parent.mkdir(parents=True, exist_ok=True)
    with lock:
        process = subprocess.Popen(
            [str(here / "tools/with-timeout.sh"), timeout, str(out / "hdtrans"), "--dicts", dicts,
             "--userdict", "", *options, "--in", str(body), "--out", str(to)], env=env)
        running.add(process)
    status = process.wait()
    with lock:
        running.discard(process)
    body.unlink()
    if status:
        raise RuntimeError(f"CLI failed on {path}")
    if to.read_bytes().count(b"\n") != text.count("\n"):
        raise RuntimeError(f"line counts differ: {path}, {to}")
    return text


pool = ThreadPoolExecutor(int(jobs) or os.cpu_count())
futures = [pool.submit(translate, i, *doc) for i, doc in enumerate(docs)]
wait(futures, return_when=FIRST_EXCEPTION)
for future in futures:
    if future.done() and future.exception():
        pool.shutdown(wait=False, cancel_futures=True)
        fail(str(future.exception()))
texts = [future.result() for future in futures]
pool.shutdown()
if not base:
    total = sum(len(lines(t)) for t in texts)
    print(f"{name}: {len(docs)} documents, {total} lines in {out / name}")
    sys.exit()
if not (Path(base) / name).is_dir():
    print(f"{name}: {len(docs)} documents in {out / name}; not in {base}")
    sys.exit()
changed = touched = missing = 0
pairs = out / f"{name}.pairs.tsv"
with open(pairs, "w", encoding="utf-8", newline="") as w:
    writer = csv.writer(w, delimiter="\t", lineterminator="\n")
    for (doc, _, header), text in zip(docs, texts):
        before = Path(base) / output(doc).relative_to(out)
        if not before.is_file():
            missing += 1
            continue
        b = lines(before.read_bytes().decode("utf-8"))
        a = lines(output(doc).read_bytes().decode("utf-8"))
        if len(b) != len(a):
            fail(f"line counts differ: {before}, {output(doc)}")
        n = 0
        for i, (x, y, z) in enumerate(zip(lines(text), b, a), 1 + header):
            if y != z:
                n += 1
                writer.writerow([f"{name}:{doc}:{i}", x, y, z])
        changed += n
        touched += n > 0
note = f", {missing} not in {base}" if missing else ""
print(f"{name}: {changed} changed lines in {touched} of {len(docs)} documents{note} ({pairs})")
PY

documents() { # SET [CLI-OPTIONS...]
    stops
    stoppable python3 -I "$home/documents.py" "$here" "$out" "$base" "$dicts" "$home" \
        "${HDTRANS_TIMEOUT:-1800}" "${HDTRANS_JOBS:-0}" "$@"
}

# Each set runs in the background; the reports and errors follow in set order once all are done.
runs=
for s in corpus sample docs examples; do
    case $s in
        corpus) on=${HDTRANS_CORPUS-} ;;
        sample) on=${HDTRANS_REF_SAMPLE-} ;;
        docs) on=${HDTRANS_TESTDATA-} ;;
        examples) on=${HDTRANS_EXAMPLES-} ;;
    esac
    [ -n "$on" ] || continue
    case $s in
        corpus | sample) "$s" "$@" ;;
        *) documents "$s" "$@" ;;
    esac > "$home/$s.log" 2>&1 &
    pids="$pids $!" runs="$runs $s:$!"
done
status=0
for run in $runs; do
    wait "${run#*:}" || status=2
    cat "$home/${run%%:*}.log"
done
exit $status
