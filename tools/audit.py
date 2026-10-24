#!/usr/bin/env python3
"""Fixed random sample of current outputs for grading, and its error rate.

  tools/audit.py draw RUN OUT.tsv [--seed S] [--per-set N]
  tools/audit.py rate LINES.tsv GRADES.tsv
  tools/audit.py --self-test

draw takes N lines (default 75) from each set of a tools/measure.sh run folder RUN whose variable
is set (HDTRANS_CORPUS, HDTRANS_REF_SAMPLE, HDTRANS_TESTDATA, HDTRANS_EXAMPLES) and writes them in
the pairs format of tools/judge.py with the keys of measure.sh: key, ja, output, and an empty
fourth column. Only lines with kana or kanji count, each distinct Japanese text once per set. A line
is chosen by the hash of seed, set and its Japanese text, so the same seed draws the same lines
from every run, and adding lines to a set changes few of the chosen ones.
rate reads grades (key, grade, judge, optional note; grade correct, minor or wrong; the last row of
a key counts) and prints per set the lines, the grade counts, wrong % and not-correct %.
Exit 1 when a line has no grade, 2 on invalid input.
"""
import argparse
import contextlib
import csv
import hashlib
import io
import os
from pathlib import Path
import re
import sys
import tempfile

SETS = ("corpus", "sample", "docs", "examples")
GRADES = ("correct", "minor", "wrong")
JAPANESE = re.compile("[ぁ-ヿ㐀-鿿]")


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(2)


def lines(path):
    data = Path(path).read_bytes().decode("utf-8")
    return (data[:-1] if data.endswith("\n") else data).split("\n")


def aligned(name, ja, ko):
    """Yields (line number, ja, ko) of one input file and its translation."""
    source = lines(ja)
    offset = 0
    if name == "docs":
        source, offset = source[1:], 1
    target = lines(ko)
    if len(source) != len(target):
        fail(f"line counts differ: {ja} {len(source)}, {ko} {len(target)}")
    for i, (x, y) in enumerate(zip(source, target), 1 + offset):
        yield i, x, y


def candidates(run, name):
    """Yields (key, ja, output) of a set, or nothing when the run or the variable lacks it."""
    run = Path(run)
    here = Path(__file__).resolve().parent.parent
    if name == "corpus" and os.environ.get("HDTRANS_CORPUS") and (run / "corpus.txt").is_file():
        for i, x, y in aligned(name, os.environ["HDTRANS_CORPUS"], run / "corpus.txt"):
            yield f"corpus:{i}", x, y
    elif name == "sample" and os.environ.get("HDTRANS_REF_SAMPLE") and (run / "sample.txt").is_file():
        ja = Path(os.environ["HDTRANS_REF_SAMPLE"]) / "ja.txt"
        for i, x, y in aligned(name, ja, run / "sample.txt"):
            yield f"sample:{i}", x, y
    elif name == "docs" and os.environ.get("HDTRANS_TESTDATA"):
        root = Path(os.environ["HDTRANS_TESTDATA"])
        for ko in sorted((run / "docs").glob("*/*.ko.txt")):
            doc = f"{ko.parent.name}/{ko.name[:-len('.ko.txt')]}"
            ja = root / f"{doc}.ja.txt"
            if not ja.is_file():
                continue
            for i, x, y in aligned(name, ja, ko):
                yield f"docs:{doc}:{i}", x, y
    elif name == "examples" and os.environ.get("HDTRANS_EXAMPLES"):
        roots = (here / "web/examples", Path(os.environ["HDTRANS_EXAMPLES"]))
        for ko in sorted((run / "examples").glob("*.ko.txt")):
            file = ko.name[:-len(".ko.txt")] + ".txt"
            found = [r / file for r in roots if (r / file).is_file()]
            if not found:
                continue
            for i, x, y in aligned(name, found[0], ko):
                yield f"examples:{file}:{i}", x, y


def pick(rows, name, seed, size):
    """The size rows of a set with the lowest hash of seed, set and ja, in input order."""
    seen, pool = set(), []
    for order, (key, ja, out) in enumerate(rows):
        if JAPANESE.search(ja) and ja not in seen:
            seen.add(ja)
            rank = hashlib.sha256(f"{seed}\0{name}\0{ja}".encode()).hexdigest()
            pool.append((rank, order, (key, ja, out)))
    chosen = sorted(pool)[:size]
    return [row for _, _, row in sorted(chosen, key=lambda c: c[1])], len(pool)


def draw(run, out, seed, size):
    if not Path(run).is_dir():
        fail(f"no run at {run}")
    total = 0
    with open(out, "w", encoding="utf-8", newline="") as w:
        writer = csv.writer(w, delimiter="\t", lineterminator="\n")
        for name in SETS:
            chosen, pool = pick(candidates(run, name), name, seed, size)
            if pool:
                print(f"{name}: {len(chosen)} of {pool} lines", file=sys.stderr)
            writer.writerows((key, ja, ko, "") for key, ja, ko in chosen)
            total += len(chosen)
    if not total:
        fail(f"no set with outputs in {run}")


def rate(sample, grades):
    with open(sample, encoding="utf-8", newline="") as f:
        keys = [r[0] for r in csv.reader(f, delimiter="\t") if r]
    graded = {}
    with open(grades, encoding="utf-8", newline="") as f:
        for row in csv.reader(f, delimiter="\t"):
            if not row:
                continue
            if len(row) < 3 or row[1] not in GRADES:
                fail(f"{grades}: not a grade row: {row}")
            graded[row[0]] = row[1]
    counts = {}
    missing = 0
    for key in keys:
        if key not in graded:
            missing += 1
            continue
        for name in (key.split(":")[0], "all"):
            counts.setdefault(name, dict.fromkeys(GRADES, 0))[graded[key]] += 1
    print("set\tlines\tcorrect\tminor\twrong\twrong %\tnot correct %")
    for name in [*SETS, "all"]:
        c = counts.get(name)
        if c:
            n = sum(c.values())
            print(f"{name}\t{n}\t{c['correct']}\t{c['minor']}\t{c['wrong']}\t"
                  f"{100 * c['wrong'] / n:.1f}\t{100 * (n - c['correct']) / n:.1f}")
    if missing:
        print(f"{missing} lines without a grade", file=sys.stderr)
        return 1
    return 0


def self_test():
    rows = [(f"corpus:{i}", f"猫{i}", "x") for i in range(1, 51)]
    rows += [("corpus:51", "abc", "abc"), ("corpus:52", "猫1", "y")]
    chosen, pool = pick(rows, "corpus", 1, 5)
    assert pool == 50 and len(chosen) == 5
    assert [r[0] for r in chosen] == sorted((r[0] for r in chosen), key=lambda k: int(k[7:]))
    more, _ = pick(rows + [(f"corpus:{i}", f"犬{i}", "x") for i in range(60, 70)], "corpus", 1, 5)
    assert len(set(chosen) & set(more)) >= 2
    assert pick(rows, "corpus", 2, 5)[0] != chosen
    with tempfile.TemporaryDirectory() as d:
        sample, grades = Path(d) / "s.tsv", Path(d) / "g.tsv"
        sample.write_text("corpus:1\ta\tb\t\ndocs:x/y:2\ta\tb\t\n", encoding="utf-8")
        grades.write_text("corpus:1\twrong\tj\ndocs:x/y:2\tminor\tj\tnote\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()) as report:
            assert rate(sample, grades) == 0
        assert "all\t2\t0\t1\t1\t50.0\t100.0" in report.getvalue()
        grades.write_text("corpus:1\tcorrect\tj\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            assert rate(sample, grades) == 1
    print("self-test ok")


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    d = sub.add_parser("draw")
    d.add_argument("run", metavar="RUN")
    d.add_argument("out", metavar="OUT.tsv")
    d.add_argument("--seed", default="1")
    d.add_argument("--per-set", type=int, default=75)
    r = sub.add_parser("rate")
    r.add_argument("sample", metavar="LINES.tsv")
    r.add_argument("grades", metavar="GRADES.tsv")
    args = parser.parse_args()
    if args.command == "draw":
        return draw(args.run, args.out, args.seed, args.per_set)
    return rate(args.sample, args.grades)


if __name__ == "__main__":
    sys.exit(main())
