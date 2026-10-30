#!/usr/bin/env python3
"""Interpolated modified Kneser-Ney n-gram LM over Korean tokens, written as HD8LM1 (src/lm.h).

  lm.py TRAIN.tok OUT.bin [--order 4] [--prune 0,0,1,1] [--arpa OUT.arpa]

--prune k1,k2,...: drop n-grams of order n whose raw count is <= k_n after estimation (count pruning;
lower orders that are contexts of kept higher orders are always kept). Scores are log10 × 10^4 integers.
Own estimator (Chen & Goodman 1998, interpolated modified KN with three discounts per order).
"""
import argparse
import collections
import math
import struct
import sys


def fnv(ids):
    h = (0xcbf29ce484222325 ^ len(ids)) & 0xFFFFFFFFFFFFFFFF
    for i in ids:
        h ^= i
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
        h ^= h >> 29
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("train")
    ap.add_argument("out")
    ap.add_argument("--order", type=int, default=4)
    ap.add_argument("--prune", default="")
    ap.add_argument("--arpa")
    a = ap.parse_args()
    N = a.order
    prune = [int(x) for x in a.prune.split(",")] if a.prune else [0] * N
    prune += [prune[-1]] * (N - len(prune))

    vocab = {"<unk>": 0, "<s>": 1, "</s>": 2}
    counts = [collections.Counter() for _ in range(N)]  # raw counts per order (tuples of ids)
    nsent = 0
    with open(a.train, encoding="utf-8") as f:
        for line in f:
            toks = line.split()
            ids = [1]
            for t in toks:
                i = vocab.get(t)
                if i is None:
                    i = vocab[t] = len(vocab)
                ids.append(i)
            ids.append(2)
            nsent += 1
            top = counts[N - 1]
            L = len(ids)
            for s in range(L):
                for n in range(1, N + 1):
                    if s + n > L:
                        break
                    if n == N or s == 0:
                        counts[n - 1][tuple(ids[s:s + n])] += 1
    # adjusted counts: highest order raw; lower orders continuation counts, except n-grams starting with <s>
    adj = [None] * N
    adj[N - 1] = counts[N - 1]
    for n in range(N - 1, 0, -1):
        c = collections.Counter()
        for g in adj[n]:
            c[g[1:]] += 1
        for g, v in counts[n - 1].items():  # n-grams starting with <s> keep raw counts
            if g[0] == 1:
                c[g] = v
        if n - 1 == 0:
            c.pop((1,), None)  # <s> is never predicted
        adj[n - 1] = c
    # discounts
    D = []
    for n in range(N):
        cc = collections.Counter(v for v in adj[n].values() if v <= 4)
        n1, n2, n3, n4 = cc[1], cc[2], cc[3], cc[4]
        if n1 == 0 or n2 == 0 or n3 == 0:
            D.append((0.5, 1.0, 1.5))
            continue
        Y = n1 / (n1 + 2 * n2)
        D.append((max(0.0, 1 - 2 * Y * n2 / n1), max(0.0, 2 - 3 * Y * n3 / n2), max(0.0, 3 - 4 * Y * n4 / n3)))
    # context statistics per order
    V = len(vocab)
    probs = [dict() for _ in range(N)]
    bows = [dict() for _ in range(N)]
    for n in range(N):
        ctx = collections.defaultdict(lambda: [0, 0, 0, 0])  # total, N1, N2, N3+
        for g, v in adj[n].items():
            c = ctx[g[:-1]]
            c[0] += v
            c[min(v, 3)] += 1
        d1, d2, d3 = D[n]
        gam = {}
        for h, (tot, a1, a2, a3) in ctx.items():
            gam[h] = (d1 * a1 + d2 * a2 + d3 * a3) / tot
        for g, v in adj[n].items():
            h = g[:-1]
            d = d1 if v == 1 else d2 if v == 2 else d3
            disc = max(v - d, 0) / ctx[h][0]
            if n == 0:
                lower = 1.0 / V
            else:
                lower = p_lower(probs, bows, g[1:])
            probs[n][g] = disc + gam[h] * lower
        if n > 0:
            for h, gv in gam.items():
                bows[n - 1][h] = gv
    # unigram for <s> has no probability of its own
    probs[0][(1,)] = probs[0].get((1,), 1e-99)
    unk_p = probs[0].get((0,)) or 1.0 / V
    # pruning: drop high-order n-grams with small raw counts unless they are contexts of kept ones
    keep = [set(probs[n]) for n in range(N)]
    for n in range(N - 1, 0, -1):
        if prune[n] <= 0:
            continue
        need = needed(keep, n + 1, N)
        src = counts[n] if n == N - 1 else adj[n]
        keep[n] = {g for g in probs[n] if src.get(g, 0) > prune[n] or g in need}
    write(a.out, vocab, probs, bows, keep, N, unk_p)
    sizes = [len(k) for k in keep]
    print(f"sentences {nsent} vocab {V} ngrams {sizes} discounts {[tuple(round(x, 3) for x in d) for d in D]}",
          file=sys.stderr)


def needed(keep, n1, N):
    """contexts (n-grams) needed by kept n-grams of order n1+1 (0-based index n1)"""
    if n1 >= N:
        return set()
    return {g[:-1] for g in keep[n1]} | {g[1:] for g in keep[n1]}


def p_lower(probs, bows, g):
    """interpolated lower-order probability of g (already estimated)"""
    n = len(g) - 1
    if g in probs[n]:
        return probs[n][g]
    if n == 0:
        return probs[0].get(g, 0.0)
    b = bows[n - 1].get(g[:-1], 1.0) if n - 1 >= 0 else 1.0
    if g[:-1] not in bows[n - 1]:
        b = 1.0
    return b * p_lower(probs, bows, g[1:])


def write(path, vocab, probs, bows, keep, N, unk_p):
    inv = sorted(vocab.items(), key=lambda x: x[1])
    with open(path, "wb") as f:
        f.write(b"HD8LM1\0\0")
        f.write(struct.pack("<IIi", N, len(inv), int(round(math.log10(unk_p) * 1e4))))
        for w, _ in inv:
            b = w.encode("utf-8")
            f.write(struct.pack("<I", len(b)))
            f.write(b)
        for n in range(N):
            rows = []
            for g in keep[n]:
                p = probs[n].get(g, 0.0)
                lp = int(round(math.log10(p) * 1e4)) if p > 0 else -990000
                bw = bows[n].get(g) if n < N - 1 else None
                lb = int(round(math.log10(bw) * 1e4)) if bw else 0
                rows.append((fnv(list(g)), lp, lb))
            rows.sort()
            for i in range(1, len(rows)):
                if rows[i][0] == rows[i - 1][0]:
                    raise SystemExit("hash collision")
            f.write(struct.pack("<Q", len(rows)))
            for k, lp, lb in rows:
                f.write(struct.pack("<Qii", k, lp, lb))


if __name__ == "__main__":
    main()
