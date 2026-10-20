#!/usr/bin/env python3
"""Write one table of every rule for review.

Usage: tools/rule-review.py DICTS USAGE.tsv OUT.tsv [--firings FILE.tsv]
USAGE.tsv is the table tools/rule-usage.py writes (rule, firings, rows, ctx). The
output has one row per rule number, its body, a note and what it costs to read it.

Columns: rule, kind, firings, rows, rule_rows, has_ctx, default, surface, closed,
open, assigns, operands, note, body
  kind       L local, G global
  rows       dictionary rows that name the rule
  rule_rows  of those, rows that carry the body (1 after interning)
  default    how many of the rows carry the sem code XXX, the candidate used when
             no rule chooses (docs/rule-language.md)
  surface    the number of Japanese-surface conditions (S)
  closed     surface conditions whose key is only a closed class (particle, ending,
             symbol, determiner)
  open       surface conditions whose key is an open class (noun, verb, adjective)
  assigns    whether the body assigns a target (D...=) or an ending (Y...=)
  operands   the number of conditions in the body
  note       the row's note column, which says what the rule decides
"""
import argparse
import collections
import csv
from pathlib import Path
import re
import sys

SURFACE = re.compile(r'(?<!\w)S([+-]\d+)(==|!=)\s*"([^"]*)"')
OPERAND = re.compile(r'(?<!\w)([SDAPGUTWMY])([+-]\d+)(==|!=|-=|\+=|=)\s*"([^"]*)"')
ASSIGN = re.compile(r'(?<!\w)[DY][+-]\d+=')
CLOSED_CLASSES = {"0", "2", "5", "6"}
REQUIRED = {"key", "ko", "pos", "rule", "ctx"}


def dictionary_rows(dicts):
    """The first body and note of each rule number, and the rows that name it."""
    bodies, notes, rows, defaults = {}, {}, collections.Counter(), collections.Counter()
    closed_keys = set()
    for path in sorted(Path(dicts).glob("*.tsv")):
        lines = path.read_text(encoding="utf-8").split("\n")
        names = None
        for line in lines:
            if not line or line.startswith(("#", "//")):
                continue
            if names is None:
                names = line.split("\t")
                if not REQUIRED <= set(names):
                    names = ()
                continue
            fields = line.split("\t")
            if len(fields) < len(names):
                continue
            row = dict(zip(names, fields))
            rule = row.get("rule", "")
            if not rule.isdigit():
                continue
            rows[rule] += 1
            if row.get("ctx") and rule not in bodies:
                bodies[rule] = row["ctx"]
                notes[rule] = row.get("note", "")
            if "XXX" in row.get("sem", ""):
                defaults[rule] += 1
            pos = row.get("pos", "")
            if pos and pos[1:2] in CLOSED_CLASSES:
                closed_keys.add(row["key"])
    return bodies, notes, rows, defaults, closed_keys


def usage_table(path):
    if path is None:
        return {}
    with path.open(encoding="utf-8", newline="") as source:
        return {row["rule"]: row for row in csv.DictReader(source, delimiter="\t")}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("usage", type=Path, metavar="USAGE.tsv")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--firings", type=Path, metavar="FILE.tsv",
                        help="the -why dump the usage came from, for the bodies of the rules")
    args = parser.parse_args()

    bodies, notes, rows, defaults, closed_keys = dictionary_rows(args.dicts)
    usage = usage_table(args.usage)
    globals_ = {}
    for path in sorted((Path(__file__).resolve().parents[1] / "src/core/rules").glob("*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            match = re.match(r"^G\s+(\d+)", line)
            if match:
                globals_[match.group(1)] = line
    header = ["rule", "kind", "firings", "rows", "rule_rows", "has_ctx", "default", "surface",
              "closed", "open", "assigns", "operands", "note", "body"]
    written = 0
    with args.out.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        for number, body in bodies.items():
            surfaces = SURFACE.findall(body)
            closed = sum(1 for _, _, key in surfaces if key in closed_keys)
            operands = len(OPERAND.findall(body))
            entry = usage.get(f"L{number}", {})
            writer.writerow([f"L{number}", "L", entry.get("firings", 0), rows[number],
                             1 if body else 0, 1 if body else 0, defaults[number], len(surfaces),
                             closed, len(surfaces) - closed, 1 if ASSIGN.search(body) else 0,
                             operands, notes.get(number, ""), body])
            written += 1
        for number, line in sorted(globals_.items(), key=lambda kv: int(kv[0])):
            rest = re.sub(r"^G\s+\d+\s+\d+\s+\d+\s+[01]\s*", "", line)
            surfaces = SURFACE.findall(rest)
            entry = usage.get(f"G{number}", {})
            writer.writerow([f"G{number}", "G", entry.get("firings", 0), 0, 0, 1, 0, len(surfaces),
                             sum(1 for _, _, key in surfaces if key in closed_keys),
                             sum(1 for _, _, key in surfaces if key not in closed_keys),
                             1 if ASSIGN.search(rest) else 0, len(OPERAND.findall(rest)),
                             "", rest])
            written += 1
    print(f"{written} rules written to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
