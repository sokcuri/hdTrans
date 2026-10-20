#!/usr/bin/env python3
"""Count how often each rule fires, and list the rules that never do.

Usage: tools/rule-usage.py CLI DICTS CORPUS OUT.tsv
CORPUS is a text file of Japanese lines, translated as separate lines with -why.
A rule fires when a WHY line names it in its rule column (L<number> for a local
rule, G<number> for a global one) for the word or for its ending. The rules that
exist are the local rule numbers of the dictionary rows that carry a ctx body and
the G lines of src/core/rules/globals.txt.

OUT.tsv columns: rule, firings, rows, ctx. rows is how many dictionary rows name a
local rule; ctx is 1 when the rule has a body. Rules with no firing are written
first, then by firings.
"""
import argparse
import collections
import csv
from pathlib import Path
import re
import subprocess
import sys

RULE = re.compile(r"(?:^|:)([LG])(\d+)$")


def dictionary_rules(dicts):
    """The local rule numbers and their row counts, from the ctx column."""
    rules = collections.Counter()
    rows = collections.Counter()
    for path in sorted(Path(dicts).glob("*.tsv")):
        with path.open(encoding="utf-8", newline="") as source:
            reader = csv.DictReader((line for line in source if not line.startswith(("#", "//"))),
                                    delimiter="\t", quoting=csv.QUOTE_NONE)
            if not reader.fieldnames or "rule" not in reader.fieldnames or "ctx" not in reader.fieldnames:
                continue
            for row in reader:
                number = row.get("rule", "")
                if not number.isdigit():
                    continue
                rows[f"L{number}"] += 1
                if row.get("ctx"):
                    rules[f"L{number}"] = 1
    return rules, rows


def global_rules(root):
    rules = {}
    for path in sorted((Path(root) / "src/core/rules").glob("*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            match = re.match(r"^G\s+(\d+)", line)
            if match:
                rules[f"G{match.group(1)}"] = 1
    return rules


def firings(cli, dicts, corpus):
    result = subprocess.run([str(cli), "-dicts", str(dicts), "--each", "-why", "-in", str(corpus)],
                            capture_output=True, text=True, encoding="utf-8")
    counts = collections.Counter()
    for line in result.stderr.splitlines():
        fields = line.split("\t")
        if not fields or fields[0] != "WHY":
            continue
        for column in (14, 16):
            if column >= len(fields):
                continue
            match = RULE.search(fields[column])
            if match:
                counts[match.group(1) + match.group(2)] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cli", type=Path, metavar="CLI")
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("corpus", type=Path, metavar="CORPUS")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    args = parser.parse_args()
    for path, kind in ((args.cli, "CLI"), (args.dicts, "dictionary folder"), (args.corpus, "corpus")):
        if not path.exists():
            parser.error(f"no {kind} at {path}")

    local, rows = dictionary_rules(args.dicts)
    global_ = global_rules(args.dicts.resolve().parent)
    known = {**local, **global_}
    counts = firings(args.cli, args.dicts, args.corpus)
    unknown = sorted(rule for rule in counts if rule not in known)
    ordered = sorted(known, key=lambda r: (counts.get(r, 0) > 0, counts.get(r, 0), r))
    with args.out.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        writer.writerow(["rule", "firings", "rows", "ctx"])
        for rule in ordered:
            writer.writerow([rule, counts.get(rule, 0), rows.get(rule, 0), known.get(rule, 0)])
    silent = sum(1 for rule in known if rule not in counts)
    print(f"{len(known)} rules, {len(counts)} fired, {silent} never fired; "
          f"{len(unknown)} fired but not found" + (f" ({', '.join(unknown[:10])})" if unknown else ""),
          file=sys.stderr)
    if unknown:
        return 1
    return 0


if __name__ == "__main__":
    main()
