#!/usr/bin/env python3
"""Move the test references from a local rule number to the dictionary row id.

Usage: tools/rule-ref.py DICTS CASES_DIR [--reverse] [--map OUT.tsv]
A case row names the rule it checks as L2245, a number that only means where the
rule sits today. The dictionary row that carries the rule keeps an id that the row
takes with it, so the tests can name that instead: rule:dict:jisjk:40681.

The table is built from the rule and id columns of DICTS, which pair one to one:
5,080 ids and 5,080 rule numbers, no id with two numbers and no number with two
ids. The dictionary's own files are not changed.

With --reverse the mapping is applied backwards, so a converted tree turns back
into L numbers and can be compared with the original. --map writes the table.
"""
import argparse
import collections
import csv
from pathlib import Path
import re
import sys

REF = re.compile(r"^L(\d+)$")


def build(directory):
    """id -> rule number and rule number -> id, from the dictionary's rule and id columns."""
    by_id, by_rule = {}, {}
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
                if header is None or "rule" not in header or "id" not in header:
                    continue
                def column(name):
                    at = header[name]
                    return fields[at] if at < len(fields) else ""
                rule, row_id = column("rule"), column("id")
                if not rule or not row_id:
                    continue
                if row_id in by_id and by_id[row_id] != rule:
                    raise SystemExit(f"{row_id} names two rules: {by_id[row_id]} and {rule}")
                if rule in by_rule and by_rule[rule] != row_id:
                    raise SystemExit(f"L{rule} names two rows: {by_rule[rule]} and {row_id}")
                by_id[row_id] = rule
                by_rule[rule] = row_id
    return by_id, by_rule


def convert(directory, by_id, by_rule, reverse):
    changed = 0
    for path in sorted(Path(directory).glob("*.tsv")):
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        out = []
        for line in lines:
            fields = line.rstrip("\n").split("\t")
            if fields[0] == "rule" or len(fields) != 5:
                out.append(line)
                continue
            rule = fields[0]
            # The expected column may also name a rule, as rule:L3635=대로.
            if len(fields) > 2:
                if reverse:
                    back = re.fullmatch(r"rule:dict:(.+?)=(.*)", fields[2])
                    if back and back.group(1) in by_id:
                        fields[2] = "rule:L" + by_id[back.group(1)] + "=" + back.group(2)
                        changed += 1
                else:
                    inner = re.fullmatch(r"rule:L(\d+)=(.*)", fields[2])
                    if inner and inner.group(1) in by_rule:
                        fields[2] = "rule:dict:" + by_rule[inner.group(1)] + "=" + inner.group(2)
                        changed += 1
            if reverse:
                match = re.fullmatch(r"dict:(.+)", rule)
                if match and match.group(1) in by_id:
                    fields[0] = "L" + by_id[match.group(1)]
                    changed += 1
            else:
                match = REF.fullmatch(rule)
                if match and match.group(1) in by_rule:
                    fields[0] = "dict:" + by_rule[match.group(1)]
                    changed += 1
            out.append("\t".join(fields) + ("\n" if line.endswith("\n") else ""))
        path.write_text("".join(out), encoding="utf-8")
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("cases", type=Path, metavar="CASES_DIR")
    parser.add_argument("--reverse", action="store_true")
    parser.add_argument("--map", type=Path, metavar="OUT.tsv")
    args = parser.parse_args()
    if not args.dicts.is_dir() or not args.cases.is_dir():
        parser.error("DICTS and CASES_DIR must be directories")
    by_id, by_rule = build(args.dicts)
    if args.map:
        with args.map.open("w", encoding="utf-8", newline="") as sink:
            writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
            writer.writerow(["id", "rule"])
            for row_id, rule in sorted(by_id.items()):
                writer.writerow([row_id, "L" + rule])
    changed = convert(args.cases, by_id, by_rule, args.reverse)
    print(f"{changed} references -> {args.cases}", file=sys.stderr)


if __name__ == "__main__":
    main()
