#!/usr/bin/env python3
"""Weight tuning by k-best batch MIRA (Cherry & Foster 2012) on sentence chrF (punctuation stripped).

  tune.py MODEL TUNE.ja TUNE.ko [--iters 8] [--nbest 100] [--jobs 16] [--out MODEL/weights.tsv]

The speech level of each tuning line is read from its reference (the last final ending), so tuning sees
only the lexical choice; the level comes from the caller at run time. Each iteration decodes the tuning set
with the current weights, adds the n-best lists to a pool, and runs MIRA over the pool; the weights with the
best corpus chrF of the 1-best are written.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
import unicodedata
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import smtlib

FEATS = ["lm", "pef", "pfe", "lef", "lfe", "phrase", "word", "oov", "lat", "gloss", "tm", "rule"]
HD8SMT = os.environ.get("HD8SMT", os.path.join(smtlib.ROOT, "build", "mac", "poc08", "smt", "hd8smt"))


def strip_punct(s):
    s = "".join(" " if unicodedata.category(c)[0] in "PS" else c for c in s)
    return re.sub(r"\s+", " ", s).strip()


def chrf_stats(hyp, ref, nmax=6):
    hyp, ref = strip_punct(hyp).replace(" ", ""), strip_punct(ref).replace(" ", "")
    out = []
    for n in range(1, nmax + 1):
        hc = collections.Counter(hyp[i:i + n] for i in range(len(hyp) - n + 1))
        rc = collections.Counter(ref[i:i + n] for i in range(len(ref) - n + 1))
        out.append((sum((hc & rc).values()), sum(hc.values()), sum(rc.values())))
    return out


def chrf_total(stats_list, beta=2.0):
    nmax = 6
    tot = [[0, 0, 0] for _ in range(nmax)]
    for st in stats_list:
        for k, s in enumerate(st):
            for j in range(3):
                tot[k][j] += s[j]
    p = r = 0.0
    k = 0
    for m, hy, rf in tot:
        if hy and rf:
            p += m / hy
            r += m / rf
            k += 1
    if not k:
        return 0.0
    p, r = p / k, r / k
    return 0.0 if p + r == 0 else 100 * (1 + beta ** 2) * p * r / (beta ** 2 * p + r)


def ref_levels(ko_lines, jobs):
    import kseg
    _, rt, _, _ = kseg.segment(ko_lines, jobs)
    out = []
    for line in rt:
        lv = re.findall(r"\+E\|F\|[^ ]*@(\w+)", line)
        out.append(lv[-1] if lv else "haera")
    return out


def decode(model, lines, levels, weights, nbest, jobs, extra=()):
    """→ list of (1-best text, [(feats, text)])"""
    n = len(lines)
    step = (n + jobs - 1) // jobs
    procs = []
    for s in range(0, n, step):
        inp = "".join(f"{lv}\t{l}\n" for lv, l in zip(levels[s:s + step], lines[s:s + step]))
        cmd = [HD8SMT, "decode", "--model", model, "--analyzer", smtlib.CORE, "--level-tab", "--weights", weights]
        if nbest:
            cmd += ["--nbest", str(nbest)]
        cmd += list(extra)
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        procs.append((p, inp))
    import threading

    def feed(p, inp):
        p.stdin.write(inp)
        p.stdin.close()

    ths = [threading.Thread(target=feed, args=x) for x in procs]
    for t in ths:
        t.start()
    res = []
    for p, _ in procs:
        out = p.stdout.read().split("\n")
        p.wait()
        i = 0
        while i < len(out):
            if out[i] == "" and i == len(out) - 1:
                break
            best = out[i]
            i += 1
            nb = []
            if nbest:
                while out[i] != "<<<":
                    parts = out[i].split(" ||| ")
                    nb.append(([int(x) / 1e4 for x in parts[2].split()], parts[3] if len(parts) > 3 else ""))
                    i += 1
                i += 1
            res.append((best, nb))
    for t in ths:
        t.join()
    assert len(res) == n, (len(res), n)
    return res


def write_weights(path, w):
    with open(path, "w") as f:
        f.write("# tuned by tune.py (k-best batch MIRA on chrF)\n")
        for k, v in zip(FEATS, w):
            f.write(f"{k}\t{v:.6f}\n")


def read_weights(path):
    w = dict.fromkeys(FEATS, 0.0)
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        k, v = line.split()[:2]
        w[k] = float(v)
    return [w[k] for k in FEATS]


def mira(pool, refs_stats_fn, w0, C=0.01, epochs=30, seed=1):
    """pool: per sentence list of (feats, score); returns averaged weights"""
    rnd = random.Random(seed)
    w = list(w0)
    avg = [0.0] * len(w)
    cnt = 0
    idx = list(range(len(pool)))
    for _ in range(epochs):
        rnd.shuffle(idx)
        for i in idx:
            cands = pool[i]
            if len(cands) < 2:
                continue
            def model(f):
                return sum(a * b for a, b in zip(w, f))
            hope = max(cands, key=lambda c: model(c[0]) + c[1])
            fear = max(cands, key=lambda c: model(c[0]) - c[1])
            loss = hope[1] - fear[1]
            diff = [a - b for a, b in zip(hope[0], fear[0])]
            margin = model(hope[0]) - model(fear[0])
            norm = sum(d * d for d in diff)
            viol = loss - margin
            if viol > 0 and norm > 0:
                eta = min(C, viol / norm)
                w = [a + eta * d for a, d in zip(w, diff)]
            for k in range(len(w)):
                avg[k] += w[k]
            cnt += 1
    return [a / cnt for a in avg] if cnt else w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("ja")
    ap.add_argument("ko")
    ap.add_argument("--iters", type=int, default=8)
    ap.add_argument("--nbest", type=int, default=100)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--out")
    ap.add_argument("--init")
    ap.add_argument("--extra", default="", help="more decoder arguments, e.g. --rule-dicts DIR")
    a = ap.parse_args()
    out = a.out or os.path.join(a.model, "weights.tsv")
    ja = [l for l in open(a.ja, encoding="utf-8").read().split("\n")]
    ko = [l for l in open(a.ko, encoding="utf-8").read().split("\n")]
    if ja and ja[-1] == "":
        ja.pop()
    ko = ko[:len(ja)]
    keep = [i for i in range(len(ja)) if ja[i].strip() and ko[i].strip() and "\t" not in ja[i]]
    ja, ko = [ja[i] for i in keep], [ko[i] for i in keep]
    levels = ref_levels(ko, a.jobs)
    w = read_weights(a.init or os.path.join(HERE, "..", "data", "weights.default.tsv"))
    pool = [dict() for _ in ja]
    best = (-1, None)
    tmp = tempfile.mkdtemp()
    for it in range(a.iters):
        wp = os.path.join(tmp, f"w{it}.tsv")
        write_weights(wp, w)
        res = decode(a.model, ja, levels, wp, a.nbest, a.jobs, a.extra.split())
        score = chrf_total([chrf_stats(b, r) for (b, _), r in zip(res, ko)])
        print(f"iter {it} chrF {score:.2f} weights " + " ".join(f"{k}={v:.3f}" for k, v in zip(FEATS, w)),
              file=sys.stderr, flush=True)
        if score > best[0]:
            best = (score, list(w))
        for i, (_, nb) in enumerate(res):
            for f, text in nb:
                key = text
                if key not in pool[i]:
                    st = chrf_stats(text, ko[i])
                    pool[i][key] = (f, chrf_total([st]) / 100.0)
        P = [list(d.values()) for d in pool]
        w = mira(P, None, w, seed=it)
    write_weights(out, best[1])
    print(f"best chrF {best[0]:.2f} → {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
