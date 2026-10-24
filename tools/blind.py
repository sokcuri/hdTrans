#!/usr/bin/env python3
"""Blind judge input for changed lines, and the verdicts mapped back.

  tools/blind.py make PAIRS.tsv KEY.tsv BLIND.tsv
  tools/blind.py disagree BLIND.tsv FIRST.tsv SECOND.tsv OUT.tsv
  tools/blind.py final KEY.tsv FIRST.tsv SECOND.tsv [THIRD.tsv] > VERDICTS.tsv
  tools/blind.py --self-test

make reads judge pairs (key, ja, before, after; tools/measure.sh, tools/e2e-pairs.py) and writes
BLIND.tsv with `id ja X Y` rows, where id is the row number and X, Y are before and after in a
random order per line, and KEY.tsv with `id key flip` (flip 1: X is after). It refuses a KEY.tsv
or BLIND.tsv that exists, so answers are never read through a new key. A judge sees only BLIND.tsv and answers
`id answer [note]` rows with answer X, Y (that one is the better translation) or same. disagree
writes the blind rows the two answer files answer differently, for a third judge. final writes
`key verdict judges [note]` rows (verdict improved, same or worse, as tools/verdicts.py reads
them): the answer of FIRST and SECOND when they agree, else the one THIRD shares with either, else
same. A line without two answers, or with two different ones and no third, is left out, so
verdicts.py counts it unjudged. A worse verdict thus means two judges called the line worse. All
files use CSV-style TSV quoting; the last row of an id counts. Exit 2 on invalid input.
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
    try:
        with open(path, encoding="utf-8", newline="") as f:
            return [r for r in csv.reader(f, delimiter="\t") if r and r[0]]
    except OSError as error:
        fail(str(error))


def write(path, rows):
    try:
        with open(path, "w", encoding="utf-8", newline="") as f:
            csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)
    except OSError as error:
        fail(str(error))


def make(pairs, key_path, blind_path):
    rows = read(pairs)
    if any(len(r) != 4 for r in rows):
        fail(f"{pairs}: rows need four columns: key, ja, before, after")
    if len({r[0] for r in rows}) != len(rows):
        fail(f"{pairs}: a key appears twice")
    blind, key = [], []
    for i, (k, ja, before, after) in enumerate(rows, 1):
        flip = secrets.randbelow(2)
        blind.append((i, ja, *((after, before) if flip else (before, after))))
        key.append((i, k, flip))
    try:
        f = open(key_path, "x", encoding="utf-8", newline="")
    except FileExistsError:
        fail(f"{key_path} exists; answers belong to the key they were made with")
    except OSError as error:
        fail(str(error))
    try:
        with f:
            csv.writer(f, delimiter="\t", lineterminator="\n").writerows(key)
        with open(blind_path, "x", encoding="utf-8", newline="") as b:
            csv.writer(b, delimiter="\t", lineterminator="\n").writerows(blind)
    except OSError as error:
        Path(key_path).unlink()
        fail(str(error))
    print(f"{len(rows)} lines in {blind_path}", file=sys.stderr)


def answers(path, ids):
    out = {}
    for row in read(path):
        if len(row) < 2 or row[1] not in ANSWERS or row[0] not in ids:
            fail(f"{path}: not an answer row for this blind file: {row}")
        out[row[0]] = (row[1], row[2] if len(row) > 2 else "")
    return out


def keys(path):
    rows = read(path)
    if any(len(r) != 3 or r[2] not in ("0", "1") for r in rows):
        fail(f"{path}: rows need id, key, flip 0 or 1")
    return {r[0]: (r[1], r[2] == "1") for r in rows}


def disagree(blind_path, first, second, out):
    rows = read(blind_path)
    ids = {r[0] for r in rows}
    a, b = answers(first, ids), answers(second, ids)
    split = [r for r in rows if r[0] in a and r[0] in b and a[r[0]][0] != b[r[0]][0]]
    write(out, split)
    print(f"{len(split)} of {len(rows)} lines answered differently", file=sys.stderr)


def verdict(answer, flip):
    if answer == "same":
        return "same"
    return "improved" if (answer == "X") == flip else "worse"


def final(key_path, first, second, third=None):
    ids = keys(key_path)
    a, b = answers(first, ids), answers(second, ids)
    c = answers(third, ids) if third else {}
    writer = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
    for i, (key, flip) in ids.items():
        if i not in a or i not in b:
            continue
        if a[i][0] == b[i][0]:
            answer, who, note = a[i][0], "1+2", "; ".join(n for n in (a[i][1], b[i][1]) if n)
        elif i not in c:
            continue
        elif c[i][0] in (a[i][0], b[i][0]):
            answer = c[i][0]
            who, note = ("1+3" if a[i][0] == answer else "2+3"), c[i][1]
        else:
            answer, who, note = "same", "split", c[i][1]
        writer.writerow((key, verdict(answer, flip), who, note))


def self_test():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        pairs, key, blind = d / "pairs.tsv", d / "key.tsv", d / "blind.tsv"
        write(pairs, [("s:1", "猫", "old1", "new1"), ("s:2", "犬", "old2", "new2"),
                      ("s:3", "鳥", "old3", "new3"), ("s:4", "魚", "old4", "new4")])
        with contextlib.redirect_stderr(io.StringIO()):
            make(pairs, key, blind)
        rows = read(blind)
        assert [r[0] for r in rows] == ["1", "2", "3", "4"]
        assert all({r[2], r[3]} == {f"old{r[0]}", f"new{r[0]}"} for r in rows)
        assert "s:1" not in blind.read_text(encoding="utf-8")
        before = blind.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()):
            for k, target in ((key, blind), (key, d / "again.tsv"), (d / "k3.tsv", blind)):
                try:
                    make(pairs, k, target)
                    raise AssertionError("make reused a key or blind file")
                except SystemExit as error:
                    assert error.code == 2
            assert not (d / "k3.tsv").exists()
            try:
                make(pairs, d / "k2.tsv", d / "missing" / "blind.tsv")
                raise AssertionError("make wrote into a missing folder")
            except SystemExit as error:
                assert error.code == 2
        assert blind.read_bytes() == before and not (d / "again.tsv").exists()
        assert not (d / "k2.tsv").exists()
        new = {r[0]: ("X" if r[2].startswith("new") else "Y") for r in rows}
        old = {i: ("Y" if a == "X" else "X") for i, a in new.items()}
        write(d / "a1.tsv", [("1", new["1"], "n1"), ("2", old["2"]), ("3", new["3"]), ("4", "same")])
        write(d / "a2.tsv", [("1", new["1"], "n2"), ("2", old["2"]), ("3", old["3"]), ("4", new["4"])])
        with contextlib.redirect_stderr(io.StringIO()):
            disagree(blind, d / "a1.tsv", d / "a2.tsv", d / "third.tsv")
        assert [r[0] for r in read(d / "third.tsv")] == ["3", "4"]
        write(d / "a3.tsv", [("3", old["3"]), ("4", "X" if new["4"] == "Y" else "Y")])
        with contextlib.redirect_stdout(io.StringIO()) as out:
            final(key, d / "a1.tsv", d / "a2.tsv", d / "a3.tsv")
        got = [tuple(r) for r in csv.reader(io.StringIO(out.getvalue()), delimiter="\t")]
        assert [g[:3] for g in got] == [("s:1", "improved", "1+2"), ("s:2", "worse", "1+2"),
                                        ("s:3", "worse", "2+3"), ("s:4", "same", "split")], got
        assert got[0][3] == "n1; n2"
        with contextlib.redirect_stdout(io.StringIO()) as out:
            final(key, d / "a1.tsv", d / "a2.tsv")
        assert out.getvalue().count("\n") == 2
    print("self-test ok")


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv[:1] == ["make"] and len(argv) == 4:
        return make(*argv[1:])
    if argv[:1] == ["disagree"] and len(argv) == 5:
        return disagree(*argv[1:])
    if argv[:1] == ["final"] and len(argv) in (4, 5):
        return final(*argv[1:])
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
