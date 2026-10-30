#!/usr/bin/env python3
"""Render scoreboard tables from judge.py run files (one JSON per set).

  scoreboard.py RUN.json... [--systems a,b,c] [--judge MODEL]
Prints markdown: per domain and pooled (all lines, equal weight per line) MQM-lite rates, pairwise
win rates (wins/(wins+losses), ties apart) with bootstrap CIs, chrF where references exist, judge
agreement and anchor calibration.
"""
import argparse
import json
import random


def boot(fn, n, reps=1000, seed=11):
    rnd = random.Random(seed)
    vals = []
    for _ in range(reps):
        v = fn([rnd.randrange(n) for _ in range(n)])
        if v is not None:
            vals.append(v)
    if not vals:
        return None
    vals.sort()
    return vals[int(0.025 * len(vals))], vals[min(len(vals) - 1, int(0.975 * len(vals)))]


def ci(c, d=1):
    return "" if not c else f" [{c[0]:.{d}f}, {c[1]:.{d}f}]"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--systems", default="")
    ap.add_argument("--judge", default="")
    a = ap.parse_args()
    reps = [json.load(open(p, encoding="utf-8")) for p in a.runs]
    judge = a.judge or reps[0]["judges"][0]["model"]
    systems = [s for s in a.systems.split(",") if s] or list(dict.fromkeys(s for r in reps for s in r["systems"]))
    sets = [r["set"] for r in reps]
    out = [f"MQM-lite by judge {judge}: lines with a critical or major error, % [95% CI] · critical errors "
           f"per 100 lines · penalty per line (minor 1, major 5, critical 25)\n"]
    out.append("| system | " + " | ".join(sets) + " | all |")
    out.append("|---|" + "---|" * (len(sets) + 1))
    pooled = {}
    for s in systems:
        cells = []
        rows = []
        for r in reps:
            d = r["mqm"].get(judge, {}).get(s)
            if not d:
                cells.append("–")
                continue
            rows += list(d["lines"].values())
            cells.append(f"{d['cm_lines_pct']:.1f}%{ci(d['cm_lines_ci'])} · {d['crit_per100']:.1f} · {d['pen']:.2f}")
        if rows:
            n = len(rows)
            cm = 100 * sum(1 for x in rows if x[0] or x[1]) / n
            cmc = boot(lambda ix: 100 * sum(1 for i in ix if rows[i][0] or rows[i][1]) / n, n)
            cr = 100 * sum(x[0] for x in rows) / n
            pen = sum(x[3] for x in rows) / n
            pooled[s] = (cm, cr, pen)
            cells.append(f"**{cm:.1f}%{ci(cmc)} · {cr:.1f} · {pen:.2f}** (n={n})")
        else:
            cells.append("–")
        out.append(f"| {s} | " + " | ".join(cells) + " |")
    # pairwise
    pair_names = list(dict.fromkeys(p for r in reps for p in r["pairs"]))
    pair_names = [p for p in pair_names if all(x in systems for x in p.split(" vs "))]
    out.append(f"\nPairwise by judge {judge}: win rate of the first system = wins/(wins+losses) % [95% CI] "
               f"(wins:ties:losses)\n")
    out.append("| pair | " + " | ".join(sets) + " | all |")
    out.append("|---|" + "---|" * (len(sets) + 1))
    for p in pair_names:
        cells, allv = [], []
        for r in reps:
            d = r["pairs"].get(p, {}).get(judge)
            if not d:
                cells.append("–")
                continue
            allv += list(d["lines"].values())
            wr = "–" if d["win_rate"] is None else f"{d['win_rate']:.1f}%{ci(d['win_rate_ci'])}"
            cells.append(f"{wr} ({d['win']}:{d['tie']}:{d['loss']})")
        w = sum(v == 1 for v in allv)
        l_ = sum(v == -1 for v in allv)

        def wr(ix):
            ww = sum(allv[i] == 1 for i in ix)
            ll = sum(allv[i] == -1 for i in ix)
            return None if ww + ll == 0 else 100 * ww / (ww + ll)
        cells.append(f"**{100 * w / max(1, w + l_):.1f}%{ci(boot(wr, len(allv)))}** "
                     f"({w}:{len(allv) - w - l_}:{l_})" if allv else "–")
        out.append(f"| {p} | " + " | ".join(cells) + " |")
    # chrF
    chr_rows = [(r["set"], s, d) for r in reps for s, d in r.get("chrf", {}).items() if s in systems]
    if chr_rows:
        out.append("\nchrF against the human reference [95% CI]\n")
        out.append("| set | system | chrF | chrF, punctuation stripped | morpheme chrF |")
        out.append("|---|---|---|---|---|")
        for st, s, d in chr_rows:
            mc = f"{d['morph_chrf']:.1f}{ci(d.get('morph_chrf_ci'))}" if "morph_chrf" in d else "–"
            out.append(f"| {st} | {s} | {d['chrf']:.1f}{ci(d['chrf_ci'])} | "
                       f"{d['chrf_nopunct']:.1f}{ci(d['chrf_nopunct_ci'])} | {mc} |")
    # agreement and anchors
    out.append("\nJudge agreement (judge 1 vs judge 2 on the shared lines) and anchors\n")
    out.append("| set | outputs | has crit/major: agree, κ | has critical: agree, κ | pair sign agreement "
               "(both decisive) | anchors judge 1: flips caught, human flagged | anchors judge 2 | first-shown "
               "wins, judge 1 |")
    out.append("|---|---|---|---|---|---|---|---|")
    for r in reps:
        ag = r["agreement"]
        k = lambda x: "–" if x is None else f"{x:.2f}"  # noqa: E731
        sg = []
        for p, d in ag.get("pairs", {}).items():
            if d["sign_where_both_decisive"] is not None:
                sg.append(f"{p}: {d['sign_where_both_decisive']:.2f} (n={d['n_decisive']})")
        js = [j["model"] for j in r["judges"]]
        an = [r["anchors"].get(j, {}) for j in js]
        an_s = [f"{x.get('flips_caught', 0)}/{x.get('flips', 0)}, {x.get('human_flagged', 0)}/{x.get('human', 0)}"
                for x in an]
        pos = r["position"].get(js[0], {})
        fs = "–" if pos.get("first_shown_wins_pct") is None else f"{pos['first_shown_wins_pct']:.0f}%"
        out.append(f"| {r['set']} | {ag.get('n_outputs', 0)} | {k(ag.get('cm_agree'))}, {k(ag.get('cm_kappa'))} | "
                   f"{k(ag.get('crit_agree'))}, {k(ag.get('crit_kappa'))} | {'; '.join(sg)} | {an_s[0]} | "
                   f"{an_s[1] if len(an_s) > 1 else '–'} | {fs} |")
    print("\n".join(out))


if __name__ == "__main__":
    main()
