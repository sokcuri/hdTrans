#!/usr/bin/env python3
"""Accepts the actual outputs of failing rule-case rows as their expected value.

  tools/rule-cases.sh CLI DICTS CASES > report.txt
  tools/accept-cases.py report.txt FILE...
  tools/accept-cases.py --self-test

Reads the failure report of tools/rule-cases.sh (`rule: input`, `  expected X`, `  actual   Y`)
and, in each FILE, replaces X by Y in the rows with that rule, input and expected value. Only
rows whose expected value is a whole translation change; `rule:`, `!rule:`, `has:` and `lacks:` rows assert what a
rule chooses and are fixed by hand. Rows of any kind are rewritten, so run it only on rows whose
new output was judged better.
Prints the number of rows changed per file.
"""
import re
import sys
import tempfile
from pathlib import Path

FAIL = re.compile(r"^(\S+): (.*)\n  expected (.*)\n  actual   (.*)$", re.M)


def accept(report, text):
    fixes = {(r, i, e): a for r, i, e, a in FAIL.findall(report)}
    lines, n = text.split("\n"), 0
    for k, line in enumerate(lines):
        cols = line.split("\t")
        if line.startswith("#") or len(cols) < 3 or cols[2].startswith(("rule:", "!rule:", "has:", "lacks:")):
            continue
        new = fixes.get((cols[0], cols[1], cols[2]))
        if new is not None:
            cols[2] = new
            lines[k] = "\t".join(cols)
            n += 1
    return "\n".join(lines), n


def self_test():
    report = "== layer local: 1 failed\nL1: 猫だ。\n  expected 고양이다.\n  actual   고양이이다.\n"
    table = "# 규칙: LEX-1\nrule\tinput\texpected\tkind\tlayer\nL1\t猫だ。\t고양이다.\tfix\tlocal\n"
    out, n = accept(report, table)
    assert n == 1 and "L1\t猫だ。\t고양이이다.\tfix\tlocal" in out, out
    rule = "L1: 猫だ。\n  expected rule:L1=고양이\n  actual   rule:L2=괭이\n"
    assert accept(rule, "L1\t猫だ。\trule:L1=고양이\tfix\tlocal\n")[1] == 0
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.tsv"
        p.write_text(table, encoding="utf-8")
        assert accept(report, p.read_text(encoding="utf-8"))[1] == 1
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    report = Path(argv[0]).read_text(encoding="utf-8")
    for path in argv[1:]:
        p = Path(path)
        text, n = accept(report, p.read_text(encoding="utf-8"))
        if n:
            p.write_text(text, encoding="utf-8")
        print(f"{path}\t{n}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
