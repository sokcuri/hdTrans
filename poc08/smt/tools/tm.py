#!/usr/bin/env python3
"""Whole-sentence memory (the TM channel inside the decoder), from the aligned training pairs.

  tm.py JA.surf KO.tok OUT.tsv [--min-len 5] [--max-len 12] [--prov tm:NAME] [--top 2]

Rows in the phrase-table format (src tgt pef pfe lef lfe count prov) for source sentences of min..max
analyzer tokens; the decoder uses them only on spans longer than any phrase, with the `tm` feature.
Only teacher output is stored (the Korean is level-neutral tokens, so the memory follows the caller's level).
"""
import argparse
import collections
import math

ap = argparse.ArgumentParser()
ap.add_argument("ja")
ap.add_argument("ko")
ap.add_argument("out")
ap.add_argument("--min-len", type=int, default=5)
ap.add_argument("--max-len", type=int, default=12)
ap.add_argument("--prov", default="tm")
ap.add_argument("--top", type=int, default=2)
a = ap.parse_args()
c = collections.defaultdict(collections.Counter)
for s, t in zip(open(a.ja, encoding="utf-8"), open(a.ko, encoding="utf-8")):
    s, t = s.strip(), t.strip()
    n = len(s.split())
    if not s or not t or not a.min_len <= n <= a.max_len:
        continue
    c[s][t] += 1
rows = 0
with open(a.out, "w", encoding="utf-8") as f:
    for s in sorted(c):
        tot = sum(c[s].values())
        for t, k in sorted(c[s].items(), key=lambda x: (-x[1], x[0]))[:a.top]:
            lp = round(math.log10(k / tot) * 1e4)
            f.write(f"{s}\t{t}\t{lp}\t0\t0\t0\t{k}\t{a.prov}\n")
            rows += 1
print(f"tm rows {rows} sources {len(c)}")
