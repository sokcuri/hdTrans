#!/usr/bin/env python3
"""Split the ported rules program into role layers.

Usage: tools/rule-layers.py RULES_PORT.tsv OUT.tsv [--report]
RULES_PORT.tsv is the table tools/rule-port.py writes. Every rule is given one
layer from what its conditions look at, which says what the rule is for:

  surface     only the Japanese surface of the word (S)
  part        the part-of-speech code alone (P, M)
  sense       the semantic codes alone (A, T)
  grammar     the grammar codes alone (G, W)
  ending      the ending's surface or translation (U, Y)
  assign      it changes a translation (D= or Y=)
  mixed       more than one of the above families
  empty       the always-false rule (||0)

OUT.tsv columns: kind, number, file, line, layer, families, depth, conditions,
assigns, comment. families lists the condition families the rule uses, condition
the condition count, assigns the number of assignment conditions.

--report prints the layer counts and, per layer, the rules with the most
conditions, so a layer can be read as a unit.
"""
import argparse
import collections
import csv
from pathlib import Path
import sys

FAMILIES = {
    "S": "surface", "D": "assign", "P": "part", "M": "part",
    "A": "sense", "T": "sense", "G": "grammar", "W": "grammar",
    "U": "ending", "Y": "ending",
}
ORDER = ["empty", "surface", "part", "sense", "grammar", "ending", "assign"]


# Which family decides what a rule is for when it looks at several. A rule that
# assigns a translation is doing assembly, whatever it reads to decide; a rule that
# reads a semantic code is choosing a sense, even when it also checks the part of
# speech to find the word. Surface and part checks alone are the plainest kind.
PRIORITY = ["assign", "sense", "grammar", "ending", "part", "surface"]


def classify(nodes):
    conditions = [n for n in nodes if n["field"]]
    families = {FAMILIES.get(n["field"], "other") for n in conditions}
    assigns = sum(1 for n in conditions if n["operator"] == "=")
    depth = max((int(n["depth"]) for n in nodes), default=0)
    if not conditions:
        layer = "empty"
    else:
        layer = next(name for name in PRIORITY if name in families)
    return layer, sorted(families), depth, len(conditions), assigns


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", type=Path, metavar="RULES_PORT.tsv")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()
    if not args.port.is_file():
        parser.error(f"no port at {args.port}")

    rows = list(csv.DictReader(args.port.open(encoding="utf-8"), delimiter="\t"))
    by_rule = collections.OrderedDict()
    for row in rows:
        by_rule.setdefault((row["kind"], row["number"]), []).append(row)

    out = []
    for (kind, number), nodes in by_rule.items():
        first = nodes[0]
        layer, families, depth, conditions, assigns = classify(nodes)
        out.append(dict(kind=kind, number=number, file=first["file"],
                        line=first["line"], layer=layer, families=" ".join(families),
                        depth=depth, conditions=conditions, assigns=assigns,
                        comment=first["comment"]))

    with args.out.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        writer.writerow(["kind", "number", "file", "line", "layer", "families",
                         "depth", "conditions", "assigns", "comment"])
        for row in out:
            writer.writerow([row["kind"], row["number"], row["file"], row["line"],
                             row["layer"], row["families"], row["depth"],
                             row["conditions"], row["assigns"], row["comment"]])

    counts = collections.Counter(row["layer"] for row in out)
    print(f"{len(out)} rules -> {args.out}", file=sys.stderr)
    for name in ORDER:
        if counts[name]:
            print(f"  {name:8} {counts[name]:>5}", file=sys.stderr)
    if args.report:
        for name in ORDER:
            layer = [r for r in out if r["layer"] == name]
            if not layer:
                continue
            print(f"\n## {name} ({len(layer)} rules)")
            for row in sorted(layer, key=lambda r: -r["conditions"])[:5]:
                print(f"  {row['file']}:{row['number']} conditions={row['conditions']} "
                      f"depth={row['depth']} {row['comment'][:60]}")


if __name__ == "__main__":
    main()
