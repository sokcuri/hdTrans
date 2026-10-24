#!/usr/bin/env python3
"""Blind judge input for changed lines, and the verdicts mapped back.

  tools/blind.py make PAIRS.tsv DIR
  tools/blind.py disagree DIR FIRST.tsv SECOND.tsv OUT.tsv
  tools/blind.py final DIR FIRST.tsv SECOND.tsv [THIRD.tsv] > VERDICTS.tsv
  tools/blind.py --self-test

make reads judge pairs (key, ja, before, after; tools/measure.sh, tools/e2e-pairs.py) and writes
DIR/blind.tsv with `id ja X Y` rows, where id is the row number and X, Y are before and after in a
random order per line, and DIR/key.tsv with `id key flip` (flip 1: X is after). A judge sees only
blind.tsv and answers `id answer [note]` rows with answer X, Y (that one is the better translation)
or same. disagree writes the blind rows the two answer files answer differently, for a third judge.
final writes `key verdict judges [note]` rows (verdict improved, same or worse, as tools/verdicts.py
reads them): the answer of FIRST and SECOND when they agree, else the one THIRD shares with either,
else same. A line without two answers, or with two different ones and no third, is left out, so
verdicts.py counts it unjudged. A worse verdict
thus means two judges called the line worse. All files use CSV-style TSV quoting; the last row of
an id counts. Exit 2 on invalid input.
"""
import contextlib
import csv
import io
from pathlib import Path
import secrets
import sys
import tempfile

ANSWERS = ("X", "Y", "same")


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(2)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return [r for r in csv.reader(f, delimiter="\t") if r and r[0]]


def write(path, rows):
    with open(path, "w", encoding="utf-8", newline="") as f:
        csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)


def make(pairs, folder):
    rows = read(pairs)
    if any(len(r) != 4 for r in rows):
        fail(f"{pairs}: rows need four columns: key, ja, before, after")
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    blind, key = [], []
    for i, (k, ja, before, after) in enumerate(rows, 1):
        flip = secrets.randbelow(2)
        blind.append((i, ja, *((after, before) if flip else (before, after))))
        key.append((i, k, flip))
    write(folder / "blind.tsv", blind)
    write(folder / "key.tsv", key)
    print(f"{len(rows)} lines in {folder / 'blind.tsv'}", file=sys.stderr)


def answers(path, ids):
    out = {}
    for row in read(path):
        if len(row) < 2 or row[1] not in ANSWERS or row[0] not in ids:
            fail(f"{path}: not an answer row for this blind file: {row}")
        out[row[0]] = (row[1], row[2] if len(row) > 2 else "")
    return out


def keys(folder):
    rows = read(Path(folder) / "key.tsv")
    if any(len(r) != 3 or r[2] not in ("0", "1") for r in rows):
        fail(f"{folder}/key.tsv: rows need id, key, flip 0 or 1")
    return {r[0]: (r[1], r[2] == "1") for r in rows}


def disagree(folder, first, second, out):
    ids = keys(folder)
    a, b = answers(first, ids), answers(second, ids)
    split = {i for i in ids if i in a and i in b and a[i][0] != b[i][0]}
    rows = [r for r in read(Path(folder) / "blind.tsv") if r[0] in split]
    write(out, rows)
    print(f"{len(rows)} of {len(ids)} lines answered differently", file=sys.stderr)


def verdict(answer, flip):
    if answer == "same":
        return "same"
    return "improved" if (answer == "X") == flip else "worse"


def final(folder, first, second, third=None):
    ids = keys(folder)
    judged = [answers(p, ids) for p in (first, second)]
    c = answers(third, ids) if third else {}
    writer = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
    for i, (key, flip) in ids.items():
        a, b = (j.get(i) for j in judged)
        if a and b and a[0] == b[0]:
            answer, who, note = a[0], "1+2", a[1] or b[1]
        elif a and b and i in c and c[i][0] in (a[0], b[0]):
            answer = c[i][0]
            who, note = ("1+3" if a[0] == answer else "2+3"), c[i][1]
        elif a and b and i in c:
            answer, who, note = "same", "split", c[i][1]
        else:
            continue
        writer.writerow((key, verdict(answer, flip), who, note))


def self_test():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        pairs = d / "pairs.tsv"
        write(pairs, [("s:1", "猫", "old1", "new1"), ("s:2", "犬", "old2", "new2"),
                      ("s:3", "鳥", "old3", "new3"), ("s:4", "魚", "old4", "new4")])
        with contextlib.redirect_stderr(io.StringIO()):
            make(pairs, d / "b")
        blind = read(d / "b" / "blind.tsv")
        assert [r[0] for r in blind] == ["1", "2", "3", "4"]
        assert all({r[2], r[3]} == {f"old{r[0]}", f"new{r[0]}"} for r in blind)
        assert "s:1" not in (d / "b" / "blind.tsv").read_text(encoding="utf-8")
        new = {r[0]: ("X" if r[2].startswith("new") else "Y") for r in blind}
        old = {i: ("Y" if a == "X" else "X") for i, a in new.items()}
        write(d / "a1.tsv", [("1", new["1"]), ("2", old["2"]), ("3", new["3"]), ("4", "same")])
        write(d / "a2.tsv", [("1", new["1"]), ("2", old["2"]), ("3", old["3"]), ("4", new["4"])])
        with contextlib.redirect_stderr(io.StringIO()):
            disagree(d / "b", d / "a1.tsv", d / "a2.tsv", d / "third.tsv")
        assert [r[0] for r in read(d / "third.tsv")] == ["3", "4"]
        write(d / "a3.tsv", [("3", old["3"]), ("4", "X" if new["4"] == "Y" else "Y")])
        with contextlib.redirect_stdout(io.StringIO()) as out:
            final(d / "b", d / "a1.tsv", d / "a2.tsv", d / "a3.tsv")
        got = [tuple(r[:3]) for r in csv.reader(io.StringIO(out.getvalue()), delimiter="\t")]
        assert got == [("s:1", "improved", "1+2"), ("s:2", "worse", "1+2"),
                       ("s:3", "worse", "2+3"), ("s:4", "same", "split")], got
        with contextlib.redirect_stdout(io.StringIO()) as out:
            final(d / "b", d / "a1.tsv", d / "a2.tsv")
        assert out.getvalue().count("\n") == 2
    print("self-test ok")


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv[:1] == ["make"] and len(argv) == 3:
        return make(*argv[1:])
    if argv[:1] == ["disagree"] and len(argv) == 5:
        return disagree(*argv[1:])
    if argv[:1] == ["final"] and len(argv) in (4, 5):
        return final(*argv[1:])
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
