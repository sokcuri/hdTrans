#!/usr/bin/env python3
"""Writes CURVE.md from curve.tsv (one JSON row per learning-curve point).

  curvemd.py CURVE.tsv OUT.md [--prefix ds]

Fit: chrF = a + b·log2(pairs), least squares over the points of one teacher (rows whose name starts with
--prefix). The band at 1M/3M/10M is the 95% prediction interval of that line (t distribution); a second fit
with a saturating curve (chrF = c − d·pairs^−e, grid over e) shows how much the log-linear model may over-promise.
"""
import argparse
import json
import math


def tq(df):
    # two-sided 95% t quantiles for small df
    t = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23}
    return t.get(df, 2.0)


def linfit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    res = [y - (a + b * x) for x, y in zip(xs, ys)]
    s2 = sum(r * r for r in res) / (n - 2) if n > 2 else float("nan")
    return a, b, s2, mx, sxx


def satfit(xs_pairs, ys):
    best = None
    for e100 in range(5, 100, 1):
        e = e100 / 100
        z = [p ** -e for p in xs_pairs]
        n = len(z)
        mz, my = sum(z) / n, sum(ys) / n
        szz = sum((v - mz) ** 2 for v in z)
        if szz == 0:
            continue
        d = -sum((v - mz) * (y - my) for v, y in zip(z, ys)) / szz
        c = my + d * mz
        sse = sum((y - (c - d * v)) ** 2 for v, y in zip(z, ys))
        if best is None or sse < best[0]:
            best = (sse, c, d, e)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tsv")
    ap.add_argument("out")
    ap.add_argument("--prefix", default="ds")
    a = ap.parse_args()
    rows = [json.loads(l) for l in open(a.tsv) if l.strip()]
    rows = [r for r in rows if r["name"].startswith(a.prefix)]
    rows.sort(key=lambda r: r["pairs"])
    L = []
    L.append("# SMT learning curve (teacher-translated pairs, dev only)\n")
    L.append("Teacher: the pinned open-weight teacher of nmt/par (see its manifest). Pilot data from another teacher is")
    L.append("excluded. Dev: chrF on 300 gc lines with human references (punctuation stripped); crit/major and win rates")
    L.append("on a fixed 200-line judged sample (100 of those gc lines + 100 fresh fiction lines), judge 1 claude-sonnet-5-5,")
    L.append("95% bootstrap CIs. Coverage = share of source tokens of the 300 gc lines translated by phrase or memory")
    L.append("options in the 1-best (the rest: rule-engine words for unknown nouns, or untranslated passthrough).\n")
    L.append("| pairs (train) | sources | chrF gc | crit/major (200) | win vs 0.7.6 (w:t:l) | win vs NMT (w:t:l) | coverage | passthrough | flip | phrases / LM / TM (MB) |")
    L.append("|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        cm = r.get("cm.smt")
        cms = f"{cm}% {r.get('cm.smt.ci')}" if cm is not None else "–"
        def win(b):
            if f"win.{b}" not in r:
                return "–"
            w, t, l = r[f"win.{b}.wtl"]
            return f"{r[f'win.{b}']}% {r[f'win.{b}.ci']} ({w}:{t}:{l})"
        sz = r.get("sizes", {})
        mb = " / ".join(f"{sz.get(k, 0) / 1e6:.1f}" for k in ("phrases.tsv", "lm.bin", "tm.tsv"))
        flip = (r.get("flip") or ["–"])[0].replace("flip suite: ", "")
        L.append(f"| {r['pairs']:,} ({r.get('train', 0):,}) | {', '.join(f'{k} {v:,}' for k, v in r['sources'].items())} | "
                 f"{r.get('gate.chrf_stripped')} | {cms} | {win('hd076')} | {win('ssnmt')} | "
                 f"{100 * r.get('gate.coverage_phrase', 0):.1f}% | {100 * r.get('gate.oov_rate', 0):.1f}% | {flip} | {mb} |")
    if rows:
        r0 = rows[-1]
        L.append(f"\nBaselines on the same lines: chrF gc 0.7.6 {r0.get('gate.chrf_stripped.hd076')}, NMT "
                 f"{r0.get('gate.chrf_stripped.ssnmt')}; crit/major (200) 0.7.6 {r0.get('cm.hd076', '–')}% "
                 f"{r0.get('cm.hd076.ci', '')}, NMT {r0.get('cm.ssnmt', '–')}% {r0.get('cm.ssnmt.ci', '')}.")
    if len(rows) >= 3:
        xs = [math.log2(r["pairs"]) for r in rows]
        ys = [r["gate.chrf_stripped"] for r in rows]
        a_, b, s2, mx, sxx = linfit(xs, ys)
        n = len(xs)
        L.append(f"\n## Fit\n\nchrF = {a_:.2f} + {b:.2f}·log2(pairs) over {n} points ({rows[0]['pairs']:,}–{rows[-1]['pairs']:,}),"
                 f" residual s = {math.sqrt(s2):.2f}: **{b:.2f} chrF per doubling**.\n")
        sat = satfit([r["pairs"] for r in rows], ys)
        L.append("| pairs | log-linear | 95% prediction band | saturating fit |")
        L.append("|---|---|---|---|")
        for P in (1_000_000, 3_000_000, 10_000_000):
            x = math.log2(P)
            y = a_ + b * x
            se = math.sqrt(s2 * (1 + 1 / n + (x - mx) ** 2 / sxx))
            h = tq(n - 2) * se
            ys_ = sat[1] - sat[2] * P ** -sat[3] if sat else float("nan")
            L.append(f"| {P:,} | {y:.1f} | [{y - h:.1f}, {y + h:.1f}] | {ys_:.1f} (ceiling {sat[1]:.1f}, e={sat[3]:.2f}) |")
        L.append("\nThe band only covers noise around a straight line in log2(pairs); it does not cover a change of slope "
                 "(saturation, or the data mix changing as new sources arrive), so read the saturating column as the "
                 "lower scenario. chrF on 300 lines has a bootstrap CI of about ±2.5 itself.")
    open(a.out, "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
