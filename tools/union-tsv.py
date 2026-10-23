#!/usr/bin/env python3
"""Resolves cherry-pick conflicts in line tables and checks the tables afterwards.

  tools/union-tsv.py FILE...           replace each conflict block by the lines of both sides
  tools/union-tsv.py --check [COMMIT]  check dicts/, the builtin tables and tests/rules/
  tools/union-tsv.py --self-test

A resolved block keeps the ours lines, then the theirs lines not already in it; the base
section of a diff3 block is dropped. A block where ours and theirs hold different rows for the
same identity is left as it is: the same `id` in a table with an id column, the same rule, input
and expectation kind in a case table, the same set and member in classes.tsv, else the same key
and class (first and third column).
Exit 1 when a block is left.

--check reports conflict markers, rows repeated verbatim, an id carried by more than one row
(dicts/*.tsv except words-variants.tsv), a case whose full expected output differs from another
row with the same rule and input, and, given COMMIT, table lines that COMMIT deleted but that are
still present (a union brings back rows the picked commit removed). Exit 1 on any finding.
"""
import collections
import contextlib
import glob
import io
import re
import subprocess
import sys
import tempfile
from pathlib import Path

MARKER = re.compile(r"^(<<<<<<<|=======$|>>>>>>>|\|\|\|\|\|\|\|)")
TABLES = ("dicts/*.tsv", "src/core/dict/builtin/**/*.tsv", "tests/rules/**/*.tsv")


def header_of(cols):
    return cols if cols and cols[0] in ("key", "rule") else None


def identity(header, cols, classes=False):
    if classes:
        return ("class", cols[0], cols[1] if len(cols) > 1 else "")
    if header and header[0] == "rule":
        kind = cols[2].split("=", 1)[0] if len(cols) > 2 and cols[2].startswith(("rule:", "!rule:")) else "full"
        return ("case", *cols[:2], kind)
    if header and "id" in header:
        i = header.index("id")
        if i < len(cols) and cols[i]:
            return ("id", cols[i])
    return ("key", cols[0], cols[2] if len(cols) > 2 else "")


def union(text, path=""):
    """Returns the resolved text and the identities of the blocks left in place."""
    classes = Path(path).name == "classes.tsv"
    out, kept, header = [], [], None
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith("<<<<<<< "):
            if header is None and line and not line.startswith(("#", "//")):
                header = header_of(line.rstrip("\r").split("\t")) or []
            out.append(line)
            i += 1
            continue
        start, sides, part = i, ([], []), 0
        i += 1
        while i < len(lines) and not lines[i].startswith(">>>>>>> "):
            if lines[i].startswith("||||||| "):
                part = 2
            elif lines[i] == "=======":
                part = 1
            elif part < 2:
                sides[part].append(lines[i])
            i += 1
        if i == len(lines):
            out.extend(lines[start:])
            break
        ours = {identity(header, x.rstrip("\r").split("\t"), classes): x
                for x in sides[0] if x and not x.startswith("#")}
        clash = [k for x in sides[1] if x and not x.startswith("#")
                 for k in [identity(header, x.rstrip("\r").split("\t"), classes)] if k in ours and ours[k] != x]
        if clash:
            out.extend(lines[start:i + 1])
            kept.extend(clash)
        else:
            seen = set()
            for x in sides[0] + sides[1]:
                if x not in seen:
                    seen.add(x)
                    out.append(x)
        i += 1
    return "\n".join(out), kept


def check_table(path, lines, report):
    header, ids, rows, full = None, collections.defaultdict(set), collections.Counter(), {}
    for n, line in enumerate(lines, 1):
        if MARKER.match(line):
            report(f"MARKER {path}:{n}")
        if not line or line.startswith(("#", "//")):
            continue
        cols = line.rstrip("\r").split("\t")
        if header is None and header_of(cols):
            header = cols
            continue
        rows[line] += 1
        if header and header[0] == "key" and "id" in header and not path.endswith("words-variants.tsv"):
            i = header.index("id")
            if i < len(cols) and cols[i]:
                ids[cols[i]].add(line)
        if header and header[0] == "rule" and len(cols) >= 3 and not cols[2].startswith(("rule:", "!rule:")):
            prev = full.setdefault((cols[0], cols[1]), cols[2])
            if prev != cols[2]:
                report(f"CASE {path}: {cols[0]} {cols[1]} expects both {prev} and {cols[2]}")
    for line, k in rows.items():
        if k > 1:
            report(f"DUPROW {path}: {line[:80]} x{k}")
    for i, found in ids.items():
        if len(found) > 1:
            keys = sorted({x.split("\t")[0] for x in found})
            report(f"DUPID {path}: {i} on {len(found)} rows ({' '.join(keys)[:60]})")


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


def read_lines(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read().split("\n")


def check(commit=None):
    bad = []
    for pattern in TABLES:
        for path in sorted(glob.glob(pattern, recursive=True)):
            check_table(path, read_lines(path), bad.append)
    if commit:
        for path, lines in deleted_lines(commit).items():
            if Path(path).exists():
                present = set(read_lines(path))
                bad.extend(f"STILL {path}: {x[:80]}" for x in lines if x in present)
    for line in bad:
        print(line)
    print("CHECK BAD" if bad else "CHECK OK")
    return 1 if bad else 0


def resolve(paths):
    left = 0
    for path in paths:
        with open(path, encoding="utf-8", newline="") as f:
            text, kept = union(f.read(), path)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        for k in kept:
            print(f"conflict kept in {path}: both sides change {' '.join(k[1:])}")
        if any(MARKER.match(x) for x in text.split("\n")):
            if not kept:
                print(f"conflict left in {path}")
            left = 1
    return left


def self_test():
    hdr = "key\tko\tid\n"
    text = hdr + "a\t가\tw:1\n<<<<<<< HEAD\nx\t나\tw:2\ny\t다\tw:3\n=======\ny\t다\tw:3\nz\t라\tw:4\n>>>>>>> pick\nb\t마\tw:5\n"
    assert union(text) == (hdr + "a\t가\tw:1\nx\t나\tw:2\ny\t다\tw:3\nz\t라\tw:4\nb\t마\tw:5\n", []), union(text)
    diff3 = hdr + "<<<<<<< HEAD\nx\t나\tw:2\n||||||| base\nw\t바\tw:9\n=======\nz\t라\tw:4\n>>>>>>> pick\n"
    assert union(diff3)[0] == hdr + "x\t나\tw:2\nz\t라\tw:4\n", union(diff3)
    clash = hdr + "<<<<<<< HEAD\nおもむろに\t천천히\tw:198\n=======\nおもむろに\t느긋이\tw:198\n>>>>>>> pick\n"
    assert union(clash) == (clash, [("id", "w:198")]), union(clash)
    cases = ("rule\tinput\texpected\tkind\tlayer\n<<<<<<< HEAD\nL1\t猫\t고양이\tfix\tlocal\n=======\n"
             "L1\t猫\t괭이\tfix\tlocal\nL1\t猫\trule:L1=고양이\tfix\tlocal\n>>>>>>> pick\n")
    assert union(cases)[1] == [("case", "L1", "猫", "full")], union(cases)
    sets = ("//= particle.case\tcase particles\nparticle.case\tを\n<<<<<<< HEAD\nparticle.case\tに\n=======\n"
            "particle.case\tへ\n>>>>>>> pick\n")
    assert union(sets, "src/core/dict/builtin/classes.tsv") == (
        "//= particle.case\tcase particles\nparticle.case\tを\nparticle.case\tに\nparticle.case\tへ\n", []), union(sets)
    assert union(sets)[1] == [("key", "particle.case", "")], union(sets)
    noted = "<<<<<<< HEAD\nparticle.case\tに\tgoal\n=======\nparticle.case\tに\tplace\n>>>>>>> pick\n"
    assert union(noted, "classes.tsv")[1] == [("class", "particle.case", "に")], union(noted, "classes.tsv")
    found = []
    check_table("dicts/t.tsv", ["key\tko\tid", "a\t가\tw:1", "b\t나\tw:1", "a\t가\tw:1", "=======",
                                "c\t다\tw:2", "c\t라\tw:2", ""], found.append)
    assert [f.split()[0] for f in found] == ["MARKER", "DUPROW", "DUPID", "DUPID"], found
    found = []
    check_table("tests/rules/t.tsv", ["rule\tinput\texpected\tkind\tlayer", "L1\t猫\t고양이\tfix\tlocal",
                                      "L1\t猫\trule:L1=고양이\tfix\tlocal", "L1\t猫\t괭이\tkeep\tlocal"], found.append)
    assert len(found) == 1 and found[0].startswith("CASE"), found
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "t.tsv"
        p.write_text(clash, encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            assert resolve([str(p)]) == 1 and p.read_text(encoding="utf-8") == clash
        p.write_bytes(text.replace("\n", "\r\n", 1).encode())
        assert resolve([str(p)]) == 0 and p.read_bytes().startswith(b"key\tko\tid\r\n")
    print("self-test ok")
    return 0


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
