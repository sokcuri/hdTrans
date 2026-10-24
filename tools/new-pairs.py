#!/usr/bin/env python3
"""Judge pairs that earlier judged pair files do not already hold, and their verdicts.

  tools/new-pairs.py OUT.tsv NEW.tsv [OLD.tsv...]
  tools/new-pairs.py --differ OUT.tsv PAIRS.tsv BRANCH.tsv...
  tools/new-pairs.py --verdicts OUT.tsv PAIRS.tsv JUDGED.tsv VERDICTS.tsv [JUDGED.tsv VERDICTS.tsv...]
  tools/new-pairs.py --self-test

Pair files hold key, ja, before, after (CSV-style TSV quoting). The first form writes the rows of
NEW.tsv whose key, before and after together appear in none of the OLD files and prints
`new N of M`; a line whose before or after moved since it was judged is new again. --differ writes
the rows of PAIRS.tsv whose key two BRANCH files hold with different after. --verdicts writes, for
each row of PAIRS.tsv, the verdict row (key verdict judge [note], as tools/verdicts.py reads it)
of the last JUDGED/VERDICTS couple whose JUDGED file holds the same key, before and after; rows
without one are left out.
Exit 2 on invalid input.
"""
import collections
import contextlib
import csv
import io
from pathlib import Path
import sys
import tempfile


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(2)


def rows_of(path):
    try:
        with open(path, encoding="utf-8", newline="") as f:
            return [r for r in csv.reader(f, delimiter="\t") if r and r[0]]
    except OSError as error:
        fail(str(error))


def read(path):
    rows = rows_of(path)
    if any(len(r) != 4 for r in rows):
        fail(f"{path}: rows need four columns: key, ja, before, after")
    return rows


def write(path, rows):
    try:
        with open(path, "w", encoding="utf-8", newline="") as f:
            csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)
    except OSError as error:
        fail(str(error))


def new(out, pairs, old):
    seen = {(r[0], r[2], r[3]) for path in old for r in read(path)}
    rows = read(pairs)
    fresh = [r for r in rows if (r[0], r[2], r[3]) not in seen]
    write(out, fresh)
    print(f"new {len(fresh)} of {len(rows)}")


def differ(out, pairs, branches):
    after = collections.defaultdict(set)
    for path in branches:
        for r in read(path):
            after[r[0]].add(r[3])
    rows = [r for r in read(pairs) if len(after[r[0]]) > 1]
    write(out, rows)
    print(f"differ {len(rows)}")


def verdicts(out, pairs, couples):
    if not couples or len(couples) % 2:
        fail("--verdicts needs JUDGED.tsv VERDICTS.tsv couples")
    found = {}
    for judged, given in zip(couples[::2], couples[1::2]):
        by_key = {r[0]: r for r in rows_of(given) if len(r) >= 2}
        for r in read(judged):
            if r[0] in by_key:
                found[(r[0], r[2], r[3])] = by_key[r[0]]
    rows = [found[(r[0], r[2], r[3])] for r in read(pairs) if (r[0], r[2], r[3]) in found]
    write(out, rows)
    print(f"verdicts {len(rows)} of {len(read(pairs))}")


def self_test():
    with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()) as report:
        d = Path(d)
        (d / "old.tsv").write_text("s:1\t猫\ta\tb\ns:2\t犬\tc\td\n", encoding="utf-8")
        (d / "new.tsv").write_text('s:1\t猫\ta\tb\ns:2\t犬\tc\te\ns:3\t鳥\t"x\ty"\tz\n', encoding="utf-8")
        new(d / "out.tsv", d / "new.tsv", [d / "old.tsv"])
        assert report.getvalue() == "new 2 of 3\n"
        assert [r[0] for r in read(d / "out.tsv")] == ["s:2", "s:3"]
        assert read(d / "out.tsv")[1][2] == "x\ty"
        differ(d / "differ.tsv", d / "new.tsv", [d / "old.tsv", d / "new.tsv"])
        assert [r[0] for r in read(d / "differ.tsv")] == ["s:2"]
        # A line judged improved as b, worse as c, and back at b keeps the verdict for b.
        (d / "c1.tsv").write_text("s:1\t猫\ta\tb\n", encoding="utf-8")
        (d / "v1.tsv").write_text("s:1\timproved\tj\n", encoding="utf-8")
        (d / "c2.tsv").write_text("s:1\t猫\ta\tc\n", encoding="utf-8")
        (d / "v2.tsv").write_text("s:1\tworse\tj\n", encoding="utf-8")
        (d / "now.tsv").write_text("s:1\t猫\ta\tb\ns:9\t魚\tp\tq\n", encoding="utf-8")
        verdicts(d / "total.tsv", d / "now.tsv", [d / "c1.tsv", d / "v1.tsv", d / "c2.tsv", d / "v2.tsv"])
        assert rows_of(d / "total.tsv") == [["s:1", "improved", "j"]]
    print("self-test ok")


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv[:1] == ["--differ"] and len(argv) >= 4:
        return differ(argv[1], argv[2], argv[3:])
    if argv[:1] == ["--verdicts"] and len(argv) >= 5:
        return verdicts(argv[1], argv[2], argv[3:])
    if len(argv) >= 2 and not argv[0].startswith("--"):
        return new(argv[0], argv[1], argv[2:])
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
