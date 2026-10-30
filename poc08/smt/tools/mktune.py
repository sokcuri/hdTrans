#!/usr/bin/env python3
"""The weight-tuning set: 500 gc + 500 caption + 500 fiction lines from the dev pools, disjoint from every
held-out, gate and dev-test line (block.keys) and from the caption units used by eval/ and E3 (±300 input
lines around each of their lines). Used only to tune decoder weights; no line enters a table.

  mktune.py BLOCK.keys OUTDIR      → OUTDIR/tune.ja, OUTDIR/tune.src (pool:index per line)
"""
import csv
import glob
import hashlib
import json
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smtlib

V08 = "/Users/sokcuri/hdwork/v08"
PATHS = json.load(open(os.environ.get("HD8SMT_PATHS", f"{V08}/smt/paths.json")))["tune_pools"]  # private pools
GC, CAP, FIC = PATHS["gc"], PATHS["cap"], PATHS["fic_glob"]
JA = re.compile(r"[぀-ヿ一-鿿]")


def blocked(line, keys):
    return any(hashlib.sha1(k.encode()).hexdigest() in keys for k in smtlib.sentence_keys(line))


def ok(line, lo=4, hi=80):
    s = line.strip()
    return lo <= len(s) <= hi and JA.search(s) is not None


def main():
    keys = set(open(sys.argv[1]).read().split())
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(1031)
    sel = []

    gc = open(GC, encoding="utf-8").read().split("\n")
    skip = set()
    for f in ("gate.idx", "dev.idx"):
        skip |= {int(x) for x in open(f"{V08}/nmt/data/{f}").read().split()}
    idx = [i for i, l in enumerate(gc) if i not in skip and ok(l) and not blocked(l, keys)]
    rnd.shuffle(idx)
    sel += [(f"gc:{i}", gc[i].strip()) for i in sorted(idx[:500])]

    near = set()
    for p in [f"{V08}/eval/captions/lines.tsv", f"{V08}/eval-runs/e3/fresh/lines.tsv"]:
        with open(p, encoding="utf-8", newline="") as f:
            for r in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
                m = json.loads(r["meta"] or "{}")
                if "input_line" in m:
                    n = int(m["input_line"])
                    near.update(range(n - 300, n + 301))
    cap = open(CAP, encoding="utf-8").read().split("\n")
    idx = [i for i, l in enumerate(cap) if (i + 1) not in near and i not in near and ok(l, 6, 60)
           and not blocked(l, keys)]
    rnd.shuffle(idx)
    sel += [(f"cap:{i}", cap[i].strip()) for i in sorted(idx[:500])]

    fic = []
    for p in sorted(glob.glob(FIC)):
        for i, l in enumerate(open(p, encoding="utf-8").read().split("\n")):
            if ok(l, 6, 100) and not blocked(l, keys):
                fic.append((f"fic:{os.path.basename(p)}:{i}", l.strip()))
    rnd.shuffle(fic)
    sel += sorted(fic[:500])

    seen, rows = set(), []
    for src, l in sel:
        if l in seen:
            continue
        seen.add(l)
        rows.append((src, l))
    with open(f"{out}/tune.ja", "w", encoding="utf-8") as f:
        f.write("\n".join(l for _, l in rows) + "\n")
    with open(f"{out}/tune.src", "w", encoding="utf-8") as f:
        f.write("\n".join(s for s, _ in rows) + "\n")
    print(len(rows), "lines", {k: sum(1 for s, _ in rows if s.startswith(k)) for k in ("gc", "cap", "fic")})


if __name__ == "__main__":
    main()
