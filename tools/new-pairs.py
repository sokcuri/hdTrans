#!/usr/bin/env python3
"""Judge pairs that earlier judged pair files do not already hold.

  tools/new-pairs.py OUT.tsv NEW.tsv [OLD.tsv...]
  tools/new-pairs.py --self-test

Writes the rows of NEW.tsv (key, ja, before, after; CSV-style TSV quoting) whose key, before and
after together appear in none of the OLD files, and prints `new N of M`. A line whose before or
after moved since it was judged is new again.
Exit 2 on invalid input.
"""
import contextlib
import csv
import io
from pathlib import Path
import sys
import tempfile


def read(path):
    try:
        with open(path, encoding="utf-8", newline="") as f:
            rows = [r for r in csv.reader(f, delimiter="\t") if r]
    except OSError as error:
        print(error, file=sys.stderr)
        sys.exit(2)
    if any(len(r) != 4 for r in rows):
        print(f"{path}: rows need four columns: key, ja, before, after", file=sys.stderr)
        sys.exit(2)
    return rows


def run(out, new, old):
    seen = {(r[0], r[2], r[3]) for path in old for r in read(path)}
    rows = read(new)
    fresh = [r for r in rows if (r[0], r[2], r[3]) not in seen]
    with open(out, "w", encoding="utf-8", newline="") as f:
        csv.writer(f, delimiter="\t", lineterminator="\n").writerows(fresh)
    print(f"new {len(fresh)} of {len(rows)}")


def self_test():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "old.tsv").write_text("s:1\t猫\ta\tb\ns:2\t犬\tc\td\n", encoding="utf-8")
        (d / "new.tsv").write_text('s:1\t猫\ta\tb\ns:2\t犬\tc\te\ns:3\t鳥\t"x\ty"\tz\n', encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()) as report:
            run(d / "out.tsv", d / "new.tsv", [d / "old.tsv"])
        assert report.getvalue() == "new 2 of 3\n"
        assert [r[0] for r in read(d / "out.tsv")] == ["s:2", "s:3"]
        assert read(d / "out.tsv")[1][2] == "x\ty"
    print("self-test ok")


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return run(argv[0], argv[1], argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
