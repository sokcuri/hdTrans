#!/usr/bin/env python3
"""Hashes of every held-out, gate and dev-test line, so training and tuning data can be filtered without
copying those lines anywhere. Writes one hex digest per line (sha1 of smtlib.norm_key / sentence keys).

  block.py OUT.keys [--extra FILE.txt ...]

Sources (read only for hashing; nothing else leaves this script): eval/ and eval2/ (lines.tsv ja + every
docs/*.ja.txt line), the NMT gate/dev lines, E3 fresh lines, the critical dev set and the flip suite.
"""
import argparse
import csv
import glob
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smtlib

V08 = "/Users/sokcuri/hdwork/v08"
SUITE = "/Users/sokcuri/hdwork/wt/poc08-eval/poc08/eval/flip/suite.tsv"


def tsv_col(path, col):
    with open(path, encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
            yield r.get(col) or ""


def text_lines(path):
    with open(path, encoding="utf-8") as f:
        for l in f:
            yield l.rstrip("\n")


def sources(extra):
    groups = {}
    for root in ("eval", "eval2"):
        for d in sorted(glob.glob(f"{V08}/{root}/*/")):
            name = f"{root}/{os.path.basename(d.rstrip('/'))}"
            it = []
            if os.path.exists(d + "lines.tsv"):
                it.append(tsv_col(d + "lines.tsv", "ja"))
            for doc in sorted(glob.glob(d + "docs/*.ja.txt")):
                it.append(text_lines(doc))
            groups[name] = it
    groups["nmt-gate"] = [text_lines(f"{V08}/nmt/data/gate.ja"), text_lines(f"{V08}/nmt/data/dev.ja")]
    groups["e3-fresh"] = [tsv_col(f"{V08}/eval-runs/e3/fresh/lines.tsv", "ja")]
    groups["critical-dev"] = [text_lines(f"{V08}/critical/devset/ja.txt")]
    groups["flip"] = [tsv_col(SUITE, "ja")]
    for e in extra:
        groups[os.path.basename(e)] = [text_lines(e)]
    return groups


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--extra", nargs="*", default=[])
    a = ap.parse_args()
    keys = set()
    for name, its in sources(a.extra).items():
        n = 0
        for it in its:
            for line in it:
                for k in smtlib.sentence_keys(line):
                    keys.add(hashlib.sha1(k.encode()).hexdigest())
                n += 1
        print(f"{name}\t{n} lines", file=sys.stderr)
    with open(a.out, "w") as f:
        for k in sorted(keys):
            f.write(k + "\n")
    print(f"{len(keys)} keys", file=sys.stderr)


if __name__ == "__main__":
    main()
