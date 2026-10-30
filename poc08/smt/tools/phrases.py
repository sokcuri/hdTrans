#!/usr/bin/env python3
"""Phrase extraction, scoring and pruning (build time).

  phrases.py JA.surf KO.tok ALIGN OUT.tsv [--max-src 4] [--max-tgt 6] [--top 20] [--sig a+e|a-e|none]
             [--prov NAME] [--jobs N]

Rows `src tgt pef pfe lef lfe count prov`, scores log10 × 10^4 integers:
- pef = log p(tgt|src), pfe = log p(src|tgt) (relative frequencies over extracted phrase pairs);
- lef, lfe = lexical weights (Koehn et al. 2003) from the word alignment, max over the pair's instances.
Significance pruning (Johnson et al. 2007): Fisher's exact test on sentence-level co-occurrence counts; a+e
drops pairs no more significant than a 1-1-1 pair, a-e keeps those. A source phrase of one token always keeps
its best option, so pruning creates no unknown words. At most --top options per source phrase (by pef, then lef).
"""
import argparse
import collections
import math
import multiprocessing as mp
import sys

A = {}


def init(ja, ko, al):
    A["ja"], A["ko"], A["al"] = ja, ko, al


def parse_al(s):
    out = []
    for p in s.split():
        i, j = p.split("-")
        out.append((int(i), int(j)))
    return out


def lex_counts(rng):
    ja, ko, al = A["ja"], A["ko"], A["al"]
    cfe = collections.Counter()  # (f, e)
    cf = collections.Counter()
    ce = collections.Counter()
    for n in range(*rng):
        f, e, a = ja[n].split(), ko[n].split(), parse_al(al[n])
        fa, ea = set(), set()
        for i, j in a:
            if i < len(f) and j < len(e):
                cfe[(f[i], e[j])] += 1
                cf[f[i]] += 1
                ce[e[j]] += 1
                fa.add(i)
                ea.add(j)
        for i, w in enumerate(f):
            if i not in fa:
                cfe[(w, None)] += 1
                cf[w] += 1
                ce[None] += 1
        for j, w in enumerate(e):
            if j not in ea:
                cfe[(None, w)] += 1
                ce[w] += 1
                cf[None] += 1
    return cfe, cf, ce


def extract(rng):
    ja, ko, al = A["ja"], A["ko"], A["al"]
    wef, wfe = A["wef"], A["wfe"]
    MF, ME = A["mf"], A["me"]
    pairs = {}
    for n in range(*rng):
        f, e, a = ja[n].split(), ko[n].split(), parse_al(al[n])
        F, E = len(f), len(e)
        f2e = [[] for _ in range(F)]
        e2f = [[] for _ in range(E)]
        for i, j in a:
            if i < F and j < E:
                f2e[i].append(j)
                e2f[j].append(i)
        seen = set()
        for fs in range(F):
            for fe in range(fs, min(F, fs + MF)):
                es = [j for i in range(fs, fe + 1) for j in f2e[i]]
                if not es:
                    continue
                lo, hi = min(es), max(es)
                if hi - lo + 1 > ME:
                    continue
                if any(i < fs or i > fe for j in range(lo, hi + 1) for i in e2f[j]):
                    continue
                # extend over unaligned target tokens at both ends
                s0 = lo
                while True:
                    e0 = hi
                    while True:
                        if e0 - s0 + 1 <= ME:
                            key = (fs, fe, s0, e0)
                            if key not in seen:
                                seen.add(key)
                                emit(pairs, f, e, fs, fe, s0, e0, f2e, e2f, wef, wfe)
                        e0 += 1
                        if e0 >= E or e2f[e0] or e0 - s0 + 1 > ME:
                            break
                    s0 -= 1
                    if s0 < 0 or e2f[s0] or hi - s0 + 1 > ME:
                        break
    return pairs


def emit(pairs, f, e, fs, fe, s0, e0, f2e, e2f, wef, wfe):
    src = " ".join(f[fs:fe + 1])
    tgt = " ".join(e[s0:e0 + 1])
    # lexical weights within the pair
    lef = 1.0
    for j in range(s0, e0 + 1):
        al = [i for i in e2f[j] if fs <= i <= fe]
        if al:
            lef *= sum(wef.get((f[i], e[j]), 1e-7) for i in al) / len(al)
        else:
            lef *= wef.get((None, e[j]), 1e-7)
    lfe = 1.0
    for i in range(fs, fe + 1):
        al = [j for j in f2e[i] if s0 <= j <= e0]
        if al:
            lfe *= sum(wfe.get((f[i], e[j]), 1e-7) for j in al) / len(al)
        else:
            lfe *= wfe.get((f[i], None), 1e-7)
    k = (src, tgt)
    v = pairs.get(k)
    if v is None:
        pairs[k] = [1, lef, lfe]
    else:
        v[0] += 1
        if lef > v[1]:
            v[1] = lef
        if lfe > v[2]:
            v[2] = lfe


def sent_counts(rng):
    """sentence-level counts of the source and target phrases that occur in the extracted table"""
    ja, ko = A["ja"], A["ko"]
    fset, eset, pairset = A["fset"], A["eset"], A["pairset"]
    MF, ME = A["mf"], A["me"]
    cf, ce, cfe = collections.Counter(), collections.Counter(), collections.Counter()
    for n in range(*rng):
        f, e = ja[n].split(), ko[n].split()
        fs = {" ".join(f[i:i + k]) for i in range(len(f)) for k in range(1, MF + 1) if i + k <= len(f)}
        fs &= fset
        es = {" ".join(e[i:i + k]) for i in range(len(e)) for k in range(1, ME + 1) if i + k <= len(e)}
        es &= eset
        cf.update(fs)
        ce.update(es)
        for x in fs:
            for y in pairset.get(x, ()):
                if y in es:
                    cfe[(x, y)] += 1
    return cf, ce, cfe


def neglog_fisher(a, cf, ce, N):
    """-ln P(X >= a) for X ~ Hypergeometric(N, cf, ce)"""
    def lc(n, k):
        return math.lgamma(n + 1) - math.lgamma(k + 1) - math.lgamma(n - k + 1)
    hi = min(cf, ce)
    denom = lc(N, ce)
    terms = [lc(cf, k) + lc(N - cf, ce - k) - denom for k in range(a, hi + 1)]
    m = max(terms)
    return -(m + math.log(sum(math.exp(t - m) for t in terms)))


def chunks(n, k):
    step = (n + k - 1) // k
    return [(i, min(n, i + step)) for i in range(0, n, step)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ja")
    ap.add_argument("ko")
    ap.add_argument("align")
    ap.add_argument("out")
    ap.add_argument("--max-src", type=int, default=4)
    ap.add_argument("--max-tgt", type=int, default=6)
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--sig", default="a+e")
    ap.add_argument("--prov", default="smt")
    ap.add_argument("--jobs", type=int, default=24)
    a = ap.parse_args()
    ja = open(a.ja, encoding="utf-8").read().split("\n")
    ko = open(a.ko, encoding="utf-8").read().split("\n")
    al = open(a.align, encoding="utf-8").read().split("\n")
    n = min(len(ja), len(ko), len(al))
    while n and not ja[n - 1].strip():
        n -= 1
    init(ja, ko, al)
    ch = chunks(n, a.jobs * 4)
    ctx = mp.get_context("fork")  # workers read the module globals in A
    with ctx.Pool(a.jobs) as pool:
        cfe, cf, ce = collections.Counter(), collections.Counter(), collections.Counter()
        for x, y, z in pool.imap(lex_counts, ch):
            cfe.update(x), cf.update(y), ce.update(z)
    wef = {k: v / cf[k[0]] for k, v in cfe.items()}
    wfe = {k: v / ce[k[1]] for k, v in cfe.items()}
    A.update(wef=wef, wfe=wfe, mf=a.max_src, me=a.max_tgt)
    print(f"lexical table {len(cfe)} pairs", file=sys.stderr)

    pairs = {}
    with ctx.Pool(a.jobs) as pool:
        for part in pool.imap(extract, ch):
            for k, v in part.items():
                w = pairs.get(k)
                if w is None:
                    pairs[k] = v
                else:
                    w[0] += v[0]
                    w[1] = max(w[1], v[1])
                    w[2] = max(w[2], v[2])
    print(f"extracted {len(pairs)} phrase pairs", file=sys.stderr)
    cs, ct = collections.Counter(), collections.Counter()
    for (s, t), v in pairs.items():
        cs[s] += v[0]
        ct[t] += v[0]
    drop = set()
    if a.sig != "none":
        fset = set(cs)
        eset = set(ct)
        pairset = collections.defaultdict(set)
        for s, t in pairs:
            pairset[s].add(t)

        A.update(fset=fset, eset=eset, pairset=dict(pairset))
        scf, sce, scfe = collections.Counter(), collections.Counter(), collections.Counter()
        with ctx.Pool(a.jobs) as pool:
            for x, y, z in pool.imap(sent_counts, ch):
                scf.update(x), sce.update(y), scfe.update(z)
        thr = math.log(n)
        eps = 1e-3
        cache = {}
        for k in pairs:
            key = (scfe.get(k, 1), scf.get(k[0], 1), sce.get(k[1], 1))
            sig = cache.get(key)
            if sig is None:
                sig = cache[key] = neglog_fisher(key[0], key[1], key[2], n)
            if (a.sig == "a+e" and sig <= thr + eps) or (a.sig == "a-e" and sig < thr - eps):
                drop.add(k)
        print(f"significance {a.sig}: drop {len(drop)} of {len(pairs)}", file=sys.stderr)
    bysrc = collections.defaultdict(list)
    for (s, t), (c, lef, lfe) in pairs.items():
        bysrc[s].append((t, c, lef, lfe))
    rows = 0
    with open(a.out, "w", encoding="utf-8") as fo:
        for s in sorted(bysrc):
            opts = bysrc[s]
            opts.sort(key=lambda x: (-x[1], -x[2], x[0]))
            kept = [o for o in opts if (s, o[0]) not in drop]
            if not kept and " " not in s:
                kept = opts[:1]
            for t, c, lef, lfe in kept[:a.top]:
                pef = c / cs[s]
                pfe = c / ct[t]
                fo.write(f"{s}\t{t}\t{round(math.log10(pef) * 1e4)}\t{round(math.log10(pfe) * 1e4)}\t"
                         f"{round(math.log10(lef) * 1e4)}\t{round(math.log10(lfe) * 1e4)}\t{c}\t{a.prov}\n")
                rows += 1
    print(f"rows {rows} sources {len(bysrc)}", file=sys.stderr)


if __name__ == "__main__":
    main()
