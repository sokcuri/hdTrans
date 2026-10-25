#!/usr/bin/env python3
"""Checks that every row of the construction table has its rule cases, and reports the rows that
never fire on a corpus.

  tools/construction-cases.py [--check]          every row has a fix and a keep case
  tools/construction-cases.py --fires CLI [-d DICTS] [INPUT...]
  tools/construction-cases.py --self-test

The rows are the ids (cx:FAMILY:PRIO) of src/core/dict/builtin/constructions.tsv. A case names its
row in the `rule` column of tests/rules/critical/, tests/rules/cases/, tests/rules/speech-soft/ or
tests/rules/speech-strict/; --check fails for a row without a `fix` and a `keep` case and for a case
that names a row the table lacks (in `rule` or in a rule:cx: or !rule:cx: expectation).
--fires translates each INPUT (default $HDTRANS_CORPUS and $HDTRANS_REF_SAMPLE/ja.txt) one line at a
time with --rule-counts and prints the fires of each row, then the rows that never fired; it is a
report and fails only when the CLI does.
Exit: 0 ok, 1 a row without its cases or an unknown row, 2 invalid arguments or failed CLI.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = "src/core/dict/builtin/constructions.tsv"
CASES = ["tests/rules/critical", "tests/rules/cases", "tests/rules/speech-soft", "tests/rules/speech-strict"]
EXPECTED = re.compile(r"^!?rule:(cx:[a-z0-9-]+:[0-9]+)(=|$)")


def row_ids(table):
    """The row ids of a construction table, in table order."""
    return [line.split("\t", 1)[0] for line in table.split("\n") if line.startswith("cx:")]


def case_rows(text):
    """(rule, expected, kind) of the rows of a case table."""
    out = []
    header = None
    for line in text.split("\n"):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if header is None:
            header = fields
            continue
        row = dict(zip(header, fields))
        out.append((row.get("rule", ""), row.get("expected", ""), row.get("kind", "")))
    return out


def check(ids, tables):
    """Problems of the cases in tables ({name: text}) against the row ids."""
    known = set(ids)
    kinds = {i: set() for i in ids}
    problems = []
    for name, text in tables.items():
        for rule, expected, kind in case_rows(text):
            m = EXPECTED.match(expected)
            if m and m.group(1) not in known:
                problems.append(f"{name}: {expected.split('=')[0]} names no row of the table")
            if not rule.startswith("cx:"):
                continue
            if rule not in known:
                problems.append(f"{name}: {rule} names no row of the table")
                continue
            kinds[rule].add(kind)
    for i in ids:
        missing = [k for k in ("fix", "keep") if k not in kinds[i]]
        if missing:
            problems.append(f"{i}: no {' and no '.join(missing)} case")
    return problems


def case_tables():
    out = {}
    for c in CASES:
        p = ROOT / c
        for f in sorted(p.glob("*.tsv")) if p.is_dir() else [p] if p.exists() else []:
            out[str(f.relative_to(ROOT))] = f.read_text(encoding="utf-8")
    return out


def fires(cli, dicts, inputs):
    totals = {}
    for path in inputs:
        with tempfile.TemporaryDirectory(prefix="hdtrans-cx.") as home:
            counts, out = os.path.join(home, "counts.tsv"), os.path.join(home, "out.txt")
            env = dict(os.environ, HOME=home, HDTRANS_CACHE_DIR="off")
            p = subprocess.run([cli, "--dicts", dicts, "--userdict", "", "--each", "--rule-counts", counts,
                                "--in", path, "--out", out], env=env, capture_output=True, text=True)
            if p.returncode != 0:
                print(f"CLI failed on {path}: {p.stderr.strip()[-300:]}", file=sys.stderr)
                return None
            for line in Path(counts).read_text(encoding="utf-8").split("\n")[1:]:
                f = line.split("\t")
                if len(f) == 7 and f[0] == "cx":
                    totals[f[1]] = totals.get(f[1], 0) + int(f[6])
    return totals


def self_test():
    table = "// a\n//= de\tafter X 10\tdoc\ncx:de:10\t\tS+0== \"で\"\tselect-lemma \"에\"\t\ncx:de:20\t\tx\ty\t\n"
    ids = row_ids(table)
    assert ids == ["cx:de:10", "cx:de:20"], ids
    cases = ("# 규칙: x\nrule\tinput\texpected\tkind\tlayer\n"
             "cx:de:10\ta\tb\tfix\trewrites\ncx:de:10\ta\t!rule:cx:de:10\tkeep\trewrites\n"
             "cx:de:20\ta\trule:cx:de:30=에\tfix\trewrites\ncx:de:40\ta\tb\tkeep\trewrites\n")
    got = check(ids, {"t.tsv": cases})
    want = ["t.tsv: rule:cx:de:30 names no row of the table", "t.tsv: cx:de:40 names no row of the table",
            "cx:de:20: no keep case"]
    assert got == want, got
    assert check(["cx:de:10"], {"t.tsv": "rule\tinput\texpected\tkind\tlayer\n"}) == ["cx:de:10: no fix and no keep case"]
    print("self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    ids = row_ids((ROOT / TABLE).read_text(encoding="utf-8"))
    if argv in ([], ["--check"]):
        problems = check(ids, case_tables())
        for p in problems:
            print(p)
        print(f"construction cases: {len(ids)} rows, {len(problems)} problems")
        return 1 if problems else 0
    if not argv or argv[0] != "--fires" or len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cli, dicts, inputs = argv[1], str(ROOT / "dicts"), []
    rest = iter(argv[2:])
    for a in rest:
        if a == "-d":
            dicts = next(rest, None)
            if dicts is None:
                print(__doc__.strip(), file=sys.stderr)
                return 2
        else:
            inputs.append(a)
    if not inputs:
        if os.environ.get("HDTRANS_CORPUS"):
            inputs.append(os.environ["HDTRANS_CORPUS"])
        if os.environ.get("HDTRANS_REF_SAMPLE"):
            inputs.append(os.path.join(os.environ["HDTRANS_REF_SAMPLE"], "ja.txt"))
    if not inputs:
        print("no input: give INPUT or set HDTRANS_CORPUS or HDTRANS_REF_SAMPLE", file=sys.stderr)
        return 2
    totals = fires(cli, dicts, inputs)
    if totals is None:
        return 2
    print("row\tfires")
    for i in ids:
        print(f"{i}\t{totals.get(i, 0)}")
    idle = [i for i in ids if not totals.get(i)]
    print(f"never fired: {len(idle)} of {len(ids)}" + (": " + " ".join(idle) if idle else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
