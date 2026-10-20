#!/usr/bin/env python3
"""Check that the ctx column of the dictionaries rebuilds the rules program.

Usage: tools/ctx-verify.py DICTS RULES_DIR [--hdrulec PATH]
Builds a program text from the ctx column of the dictionary rows (a header with one
local rule per number, then each rule's body, an empty rule for a number no row
names), and compares every body with the rule files through hdrulec dump.

Exit 0 when every rule a row names has the same body in both; 1 when one differs;
2 on a bad invocation.
"""
import argparse
import subprocess
from pathlib import Path
import re
import sys
import tempfile

TITLE = re.compile(r"^\s*L (\d+) (.*)$")


def bodies(text):
    out = {}
    for line in text.splitlines():
        match = TITLE.match(line)
        if match:
            out[int(match.group(1))] = match.group(2).strip()
    return out


def from_dicts(directory):
    rows = {}
    for path in sorted(Path(directory).glob("*.tsv")):
        if path.name == "kana.tsv":
            continue
        with path.open(encoding="utf-8") as source:
            header = None
            for line in source:
                if line.startswith("#") or not line.strip():
                    continue
                fields = line.rstrip("\n").split("\t")
                if fields[0] == "key":
                    header = {name: i for i, name in enumerate(fields)}
                    continue
                if header is None or "ctx" not in header or "rule" not in header:
                    continue
                body = fields[header["ctx"]] if header["ctx"] < len(fields) else ""
                rule = fields[header["rule"]] if header["rule"] < len(fields) else ""
                if not body or not rule.isdigit():
                    continue
                if int(rule) in rows and rows[int(rule)] != body:
                    raise SystemExit(f"L {rule} has two bodies")
                rows[int(rule)] = body
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("rules", type=Path, metavar="RULES_DIR")
    parser.add_argument("--hdrulec", default="build/mac/hdrulec")
    args = parser.parse_args()
    if not args.dicts.is_dir() or not args.rules.is_dir():
        parser.error("DICTS and RULES_DIR must be directories")
    build = Path(args.hdrulec)
    if not build.is_file():
        parser.error(f"no hdrulec at {build}")

    rows = from_dicts(args.dicts)
    top = max(rows)
    with tempfile.TemporaryDirectory(prefix="ctx-verify-") as temp:
        program = Path(temp) / "ctx.txt"
        program.write_text("".join(
            [f"rules 1 {top + 1} 0\n"] + [f"L {n} {rows.get(n, '||0')}\n" for n in range(top + 1)]),
            encoding="utf-8")
        dumped = subprocess.run([str(build), "dump", str(program)], capture_output=True,
                                text=True, encoding="utf-8", check=True).stdout
        files = subprocess.run([str(build), "dump", str(args.rules)], capture_output=True,
                               text=True, encoding="utf-8", check=True).stdout
    left, right = bodies(dumped), bodies(files)
    missing = sorted(set(right) - set(left))
    differs = sorted(n for n in rows if left.get(n) != right.get(n))
    print(f"{len(rows)} rules in the dictionary, {len(right)} in the rule files", file=sys.stderr)
    for n in differs:
        print(f"L{n}\n  ctx  : {left.get(n)}\n  files: {right.get(n)}", file=sys.stderr)
    for n in missing:
        print(f"L{n} is in the rule files, and no row names it", file=sys.stderr)
    print(f"{len(differs)} bodies differ, {len(missing)} rules unnumbered", file=sys.stderr)
    return 1 if differs else 0


if __name__ == "__main__":
    sys.exit(main())
