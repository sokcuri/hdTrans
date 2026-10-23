#!/usr/bin/env python3
"""Resolves cherry-pick conflicts in line tables and checks the tables afterwards.

  tools/union-tsv.py FILE...           replace each conflict block by the lines of both sides
  tools/union-tsv.py --check [COMMIT]  check dicts/, the builtin tables and tests/rules/
  tools/union-tsv.py --self-test

A resolved block keeps the ours lines, then the theirs lines not already in it; the base
section of a diff3 block is dropped. Exit 1 when a block is left.

--check reports conflict markers, rows repeated verbatim, an id carried by rows with different
keys (dicts/*.tsv except words-variants.tsv), a case whose full expected output differs from
another row with the same rule and input, and, given COMMIT, table lines that COMMIT deleted
but that are still present (a union brings back rows the picked commit removed). Exit 1 on any.
"""
import collections
import glob
import re
import subprocess
import sys
import tempfile
from pathlib import Path

BLOCK = re.compile(r"^<<<<<<< [^\n]*\n(.*?)^(?:\|\|\|\|\|\|\| [^\n]*\n.*?)?=======\n(.*?)^>>>>>>> [^\n]*\n?",
                   re.S | re.M)
MARKER = re.compile(r"^(<<<<<<<|=======$|>>>>>>>|\|\|\|\|\|\|\|)")
TABLES = ("dicts/*.tsv", "src/core/dict/builtin/**/*.tsv", "tests/rules/**/*.tsv")


def union(text):
    def keep(m):
        out, seen = [], set()
        for line in m.group(1).splitlines(True) + m.group(2).splitlines(True):
            key = line.rstrip("\n")
            if key not in seen:
                seen.add(key)
                out.append(line if line.endswith("\n") else line + "\n")
        return "".join(out)
    return BLOCK.sub(keep, text)


def check_table(path, lines, report):
    header, ids, rows = None, collections.defaultdict(set), collections.Counter()
    full = {}
    for n, line in enumerate(lines, 1):
        if MARKER.match(line):
            report(f"MARKER {path}:{n}")
        if not line or line.startswith(("#", "//")):
            continue
        cols = line.split("\t")
        if header is None and cols[0] in ("key", "rule"):
            header = cols
            continue
        rows[line] += 1
        if header and header[0] == "key" and "id" in header and not path.endswith("words-variants.tsv"):
            i = header.index("id")
            if i < len(cols) and cols[i]:
                ids[cols[i]].add(cols[0])
        if header and header[0] == "rule" and len(cols) >= 3 and not cols[2].startswith(("rule:", "!rule:")):
            prev = full.setdefault((cols[0], cols[1]), cols[2])
            if prev != cols[2]:
                report(f"CASE {path}: {cols[0]} {cols[1]} expects both {prev} and {cols[2]}")
    for line, k in rows.items():
        if k > 1:
            report(f"DUPROW {path}: {line[:80]} x{k}")
    for i, keys in ids.items():
        if len(keys) > 1:
            report(f"IDCOLLIDE {path}: {i} on {' '.join(sorted(keys))[:80]}")


def deleted_lines(commit):
    diff = subprocess.run(["git", "show", "--format=", "-U0", commit, "--", *TABLES],
                          capture_output=True, text=True, check=True).stdout
    removed, added, path = collections.defaultdict(list), collections.defaultdict(set), None
    for line in diff.split("\n"):
        if line.startswith("--- "):
            path = line[6:] if line.startswith("--- a/") else None
        elif line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else path
        elif path and line.startswith("-") and line[1:].strip():
            removed[path].append(line[1:])
        elif path and line.startswith("+"):
            added[path].add(line[1:])
    return {p: [x for x in ls if x not in added[p]] for p, ls in removed.items()}


def check(commit=None):
    bad = []
    report = bad.append
    for pattern in TABLES:
        for path in sorted(glob.glob(pattern, recursive=True)):
            check_table(path, Path(path).read_text(encoding="utf-8").split("\n"), report)
    if commit:
        for path, lines in deleted_lines(commit).items():
            if Path(path).exists():
                present = set(Path(path).read_text(encoding="utf-8").split("\n"))
                for line in lines:
                    if line in present:
                        report(f"STILL {path}: {line[:80]}")
    for line in bad:
        print(line)
    print("CHECK BAD" if bad else "CHECK OK")
    return 1 if bad else 0


def self_test():
    text = "a\n<<<<<<< HEAD\nx\ny\n=======\ny\nz\n>>>>>>> pick\nb\n"
    assert union(text) == "a\nx\ny\nz\nb\n", union(text)
    diff3 = "<<<<<<< HEAD\nx\n||||||| base\nw\n=======\nz\n>>>>>>> pick\n"
    assert union(diff3) == "x\nz\n", union(diff3)
    assert union("<<<<<<< HEAD\n=======\nz\n>>>>>>> pick\n") == "z\n"
    found = []
    check_table("dicts/t.tsv", ["key\tko\tid", "a\t가\tw:1", "b\t나\tw:1", "a\t가\tw:1", "=======", ""],
                found.append)
    assert [f.split()[0] for f in found] == ["MARKER", "DUPROW", "IDCOLLIDE"], found
    found = []
    check_table("tests/rules/t.tsv", ["rule\tinput\texpected\tkind\tlayer", "L1\t猫\t고양이\tfix\tlocal",
                                      "L1\t猫\trule:L1=고양이\tfix\tlocal", "L1\t猫\t괭이\tkeep\tlocal"], found.append)
    assert len(found) == 1 and found[0].startswith("CASE"), found
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.tsv"
        p.write_text(text, encoding="utf-8")
        assert resolve([str(p)]) == 0 and p.read_text(encoding="utf-8") == "a\nx\ny\nz\nb\n"
    print("self-test ok")
    return 0


def resolve(paths):
    left = 0
    for path in paths:
        p = Path(path)
        text = union(p.read_text(encoding="utf-8"))
        p.write_text(text, encoding="utf-8")
        if any(MARKER.match(x) for x in text.split("\n")):
            print(f"conflicts left in {path}")
            left = 1
    return left


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if argv[:1] == ["--check"]:
        return check(argv[1] if len(argv) > 1 else None)
    if not argv or argv[0].startswith("-"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return resolve(argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
