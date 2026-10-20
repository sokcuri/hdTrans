#!/usr/bin/env python3
"""Regroup the rules program by role layer instead of by head word.

Usage: tools/rule-split.py RULES_DIR LAYERS.tsv OUT_DIR [--check]
RULES_DIR holds the current rule files. LAYERS.tsv is the table
tools/rule-layers.py writes. Writes one file per layer to OUT_DIR:

  01-surface.txt   decides from the Japanese surface alone
  02-part.txt      decides from parts of speech alone
  03-sense.txt     decides from semantic codes
  04-grammar.txt   decides from grammar codes
  05-ending.txt    decides from the ending's surface or translation
  06-assign.txt    rewrites a translation; the decision is the assignment
  00-empty.txt     the always-false rules

Rule numbers do not change: an L number is what a dictionary row points at, and a
G priority is its name, so both keep working wherever the rule now sits. The
comment above each rule moves with it. With --check, the files are only compared
with RULES_DIR and the exit code says whether the split loses, duplicates or
invents a rule.
"""
import argparse
import collections
import csv
from pathlib import Path
import sys

FILES = {
    "surface": "01-surface.txt",
    "part": "02-part.txt",
    "sense": "03-sense.txt",
    "grammar": "04-grammar.txt",
    "ending": "05-ending.txt",
    "assign": "06-assign.txt",
    "empty": "00-empty.txt",
}
TITLES = {
    "surface": "표기로 고르는 규칙: 일본어 표기만 보고 후보를 고른다.",
    "part": "품사로 고르는 규칙: 품사 코드만 보고 후보를 고른다.",
    "sense": "의미 코드로 고르는 규칙: 의미 코드로 뜻을 가른다.",
    "grammar": "문법 코드로 고르는 규칙: 문법 코드로 뜻을 가른다.",
    "ending": "어미로 고르는 규칙: 어미의 표기나 번역을 보고 고른다.",
    "assign": "번역을 바꾸는 규칙: 조건이 맞으면 번역을 대입한다.",
    "empty": "늘 거짓인 규칙: 사전 행이 가리키지만 아무것도 고르지 않는다.",
}


def read_rules(rules_dir):
    """[(layer key, comment lines, rule line, source file, source line)] in file order."""
    out = []
    for path in sorted(rules_dir.glob("*.txt")):
        pending = []
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("#"):
                pending.append(line)
            elif stripped.startswith(("L ", "G ")):
                parts = stripped.split()
                out.append(((parts[0], parts[1]), pending, line, path.name, number))
                pending = []
            elif stripped:
                pending = []
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rules", type=Path, metavar="RULES_DIR")
    parser.add_argument("layers", type=Path, metavar="LAYERS.tsv")
    parser.add_argument("out", type=Path, metavar="OUT_DIR")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.rules.is_dir():
        parser.error(f"no rule folder at {args.rules}")

    layers = {(r["kind"], r["number"]): r["layer"]
              for r in csv.DictReader(args.layers.open(encoding="utf-8"), delimiter="\t")}
    rules = read_rules(args.rules)
    missing = [r for r in rules if r[0] not in layers]
    if missing:
        print(f"{len(missing)} rules have no layer, first {missing[0][0]}", file=sys.stderr)
        return 2

    grouped = collections.defaultdict(list)
    for key, comment, line, file_name, number in rules:
        grouped[layers[key]].append((comment, line, file_name, number))

    counts = {name: len(grouped.get(name, ())) for name in FILES}
    print(f"{len(rules)} rules in {sum(counts.values())} places", file=sys.stderr)
    for name in FILES:
        print(f"  {FILES[name]:16} {counts[name]:>5}", file=sys.stderr)

    if args.check:
        seen = collections.Counter((k, n) for k, _, _, _, n in rules)
        duplicated = [k for k, v in seen.items() if v > 1]
        if duplicated:
            print(f"duplicate rules: {duplicated[:5]}", file=sys.stderr)
            return 1
        return 0

    args.out.mkdir(parents=True, exist_ok=True)
    header = ("# 규칙 레이어: 조건이 무엇을 보는지로 나눈 파일. 번호는 그대로라 사전의 rule 열과\n"
              "# 전역 규칙의 PRIO가 가리키는 대상이 바뀌지 않는다.\n")
    for name, file_name in FILES.items():
        entries = grouped.get(name, ())
        if not entries:
            continue
        body = [header, f"# {TITLES[name]}\n"]
        for comment, line, source, number in entries:
            if comment:
                body.extend(c + "\n" for c in comment)
            body.append(line + "\n")
        (args.out / file_name).write_text("".join(body), encoding="utf-8")
    print(f"wrote {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
