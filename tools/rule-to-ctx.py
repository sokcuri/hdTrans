#!/usr/bin/env python3
"""Move the body of a local rule into the dictionary row that names it.

Usage: tools/rule-to-ctx.py RULES_DIR DICTS [--reverse] [--column ctx]
Every dictionary row with a rule number gets the body of that rule in a new column,
verbatim: the rule files hold `L 925 <body>` and the row holds the body in `ctx`.
The rule number column stays, so both ways of running the program can be compared.

The rule bodies carry no tabs, so a body is one TSV field as it stands. Identical
bodies are kept once per file for a smaller diff.

With --reverse the column is removed and the files return to their earlier shape.
"""
import argparse
import collections
from pathlib import Path
import re
import sys

LOCAL = re.compile(r"^L (\d+) (.*)$")


def read_rules(directory):
    bodies = {}
    for path in sorted(Path(directory).glob("*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            match = LOCAL.match(line)
            if match:
                number, body = match.group(1), match.group(2)
                if number in bodies and bodies[number] != body:
                    raise SystemExit(f"L {number} is defined twice with different bodies")
                bodies[number] = body
    return bodies


def add_column(path, bodies, name):
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    out, header, added = [], None, 0
    for line in lines:
        stripped = line.rstrip("\n")
        if stripped.startswith("#") or not stripped.strip():
            out.append(line)
            continue
        fields = stripped.split("\t")
        if fields[0] == "key":
            header = fields
            at = header.index("rule") + 1 if "rule" in header else len(header)
            header.insert(at, name)
            out.append("\t".join(header) + ("\n" if line.endswith("\n") else ""))
            continue
        if header is None:
            out.append(line)
            continue
        at = header.index(name)
        rule = fields[header.index("rule")] if "rule" in header and header.index("rule") < len(fields) else ""
        body = bodies.get(rule, "") if rule else ""
        if body:
            added += 1
        while len(fields) < at:
            fields.append("")
        fields.insert(at, body)
        out.append("\t".join(fields) + ("\n" if line.endswith("\n") else ""))
    path.write_text("".join(out), encoding="utf-8")
    return added


def drop_column(path, name):
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    out, header, removed = [], None, 0
    for line in lines:
        stripped = line.rstrip("\n")
        if stripped.startswith("#") or not stripped.strip():
            out.append(line)
            continue
        fields = stripped.split("\t")
        if fields[0] == "key":
            header = fields
            if name in header:
                header.remove(name)
            out.append("\t".join(header) + ("\n" if line.endswith("\n") else ""))
            continue
        if header is None:
            out.append(line)
            continue
        if name in fields:
            fields.remove(name)
            removed += 1
        out.append("\t".join(fields) + ("\n" if line.endswith("\n") else ""))
    path.write_text("".join(out), encoding="utf-8")
    return removed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rules", type=Path, metavar="RULES_DIR")
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("--reverse", action="store_true")
    parser.add_argument("--column", default="ctx", metavar="NAME")
    args = parser.parse_args()
    bodies = read_rules(args.rules)
    total = 0
    for path in sorted(args.dicts.glob("*.tsv")):
        if path.name == "kana.tsv":
            continue
        if args.reverse:
            total += drop_column(path, args.column)
        else:
            total += add_column(path, bodies, args.column)
    print(f"{total} rows {'lost' if args.reverse else 'carry'} the rule body", file=sys.stderr)


if __name__ == "__main__":
    main()
