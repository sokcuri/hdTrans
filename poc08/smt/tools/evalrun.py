#!/usr/bin/env python3
"""Dev measurement of one model: outputs, chrF and (optionally) the judge harness and the flip suite.

  evalrun.py MODEL NAME [--judge] [--flip] [--speech auto] [--extra '...decoder args']

Sets (dev only, never eval/ or eval2/):
  gate   300 gc lines with human references (each line its own document)
  fic150 150 fresh fiction lines from E3 (documents, no reference)
Outputs, judge JSON and reports go to /Users/sokcuri/hdwork/v08/eval-runs/smt/NAME/ (private).
Baselines are judged from the cache: hd076 (0.7.6) and ssnmt.
"""
import argparse
import json
import re
import os
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import smtlib
from tune import chrf_stats, chrf_total

V08 = "/Users/sokcuri/hdwork/v08"
R = f"{V08}/eval-runs/smt"
T = "/Users/sokcuri/hdwork/wt/poc08-eval/poc08/eval/tools"
HD8SMT = os.environ.get("HD8SMT", os.path.join(smtlib.ROOT, "build", "mac", "poc08", "smt", "hd8smt"))
JUDGE = {  # fixed 200-line judged sample: 100 gc lines of gate, 100 fiction lines of fic150
    "j100gc": {"dir": f"{R}/devsets/j100gc", "from": "gate", "idx": f"{R}/devsets/j100gc/idx.txt"},
    "j100fic": {"dir": f"{R}/devsets/j100fic", "from": "fic150", "idx": f"{R}/devsets/j100fic/idx.txt"},
}
SETS = {
    "gate": {"dir": f"{V08}/nmt/data/gateset", "ja": f"{V08}/nmt/data/gate.ja", "ref": f"{V08}/nmt/data/gate.ko",
             "base": {"hd076": f"{V08}/nmt/runs/gate.hd076.ko", "ssnmt": f"{V08}/nmt/runs/gate.ssnmt.ko"}},
    "fic150": {"dir": f"{R}/devsets/fic150", "base": {"hd076": f"{R}/base/fic150.hd076.ko",
                                                      "ssnmt": f"{R}/base/fic150.ssnmt.ko"}},
}


def lines(p):
    t = open(p, encoding="utf-8").read().split("\n")
    if t and t[-1] == "":
        t.pop()
    return t


def pooled_judge(paths, judge="claude-sonnet-5-5", seed=7):
    """Pooled 200-line numbers from judge.py JSONs: crit/major line share per system and win rates of smt."""
    import random
    mqm, pairs = {}, {}
    for p in paths:
        d = json.load(open(p))
        for sysname, v in d["mqm"].get(judge, {}).items():
            for lid, x in v["lines"].items():
                mqm.setdefault(sysname, {})[lid] = 1 if (x[0] + x[1]) > 0 else 0
        for pn, v in d["pairs"].items():
            if judge in v:
                for lid, x in v[judge]["lines"].items():
                    pairs.setdefault(pn, {})[lid] = x
    rnd = random.Random(seed)

    def ci(vals, f):
        bs = sorted(f([vals[rnd.randrange(len(vals))] for _ in vals]) for _ in range(1000))
        return [round(bs[25], 1), round(bs[974], 1)]

    out = {}
    for sysname, d in mqm.items():
        v = list(d.values())
        out[f"cm.{sysname}"] = round(100 * sum(v) / len(v), 1)
        out[f"cm.{sysname}.ci"] = ci(v, lambda x: 100 * sum(x) / len(x))
        out[f"cm.{sysname}.n"] = len(v)
    for pn, d in pairs.items():
        if not pn.startswith("smt vs"):
            continue
        v = list(d.values())
        w, l, t = v.count(1), v.count(-1), v.count(0)
        wr = lambda x: 100 * x.count(1) / max(1, x.count(1) + x.count(-1))
        out[f"win.{pn.split(' vs ')[1]}"] = round(wr(v), 1)
        out[f"win.{pn.split(' vs ')[1]}.ci"] = ci(v, wr)
        out[f"win.{pn.split(' vs ')[1]}.wtl"] = [w, t, l]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("name")
    ap.add_argument("--sets", default="gate,fic150")
    ap.add_argument("--judge", action="store_true")
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--speech", default="auto")
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    out = f"{R}/{a.name}"
    os.makedirs(out, exist_ok=True)
    cmd = [HD8SMT, "decode", "--model", os.path.abspath(a.model), "--analyzer", smtlib.CORE, "--speech", a.speech]
    cmd += shlex.split(a.extra)
    res = {"model": os.path.abspath(a.model), "cmd": " ".join(cmd)}
    for s in a.sets.split(","):
        S = SETS[s]
        ko = f"{out}/{s}.ko"
        t0 = time.time()
        if "ja" in S:  # line set: every line its own scene
            p = subprocess.run(cmd + ["--each"], input="\n".join(lines(S["ja"])) + "\n", capture_output=True,
                               text=True, check=True)
            open(ko, "w", encoding="utf-8").write(p.stdout)
            n = len(lines(S["ja"]))
        else:
            subprocess.run([sys.executable, "-I", f"{T}/run_system.py", "--set", S["dir"], "--cmd",
                            " ".join(shlex.quote(c) for c in cmd), "--out", ko], check=True, capture_output=True)
            n = len(lines(ko))
        res[f"{s}.seconds"] = round(time.time() - t0, 2)
        res[f"{s}.lines_per_s"] = round(n / max(1e-9, time.time() - t0), 1)
        if "ref" in S:
            hyp, ref = lines(ko), lines(S["ref"])
            res[f"{s}.chrf_stripped"] = round(chrf_total([chrf_stats(h, r) for h, r in zip(hyp, ref)]), 2)
            for b, bp in S["base"].items():
                res[f"{s}.chrf_stripped.{b}"] = round(chrf_total([chrf_stats(h, r) for h, r in zip(lines(bp), ref)]), 2)
    # coverage: share of source tokens translated by phrase-table or memory options (1-best, --why)
    p = subprocess.run(cmd + ["--each", "--why"], input="\n".join(lines(SETS["gate"]["ja"])) + "\n",
                       capture_output=True, text=True, check=True)
    cov = {"phrase": 0, "tm": 0, "rule": 0, "passthrough": 0, "oov": 0, "gloss": 0}
    for l in p.stdout.split("\n"):
        if not l.startswith("#   "):
            continue
        m = re.search(r" k (\d+)\] (\S+)$", l)
        if not m:
            continue
        k, prov = int(m.group(1)), m.group(2)
        key = ("oov" if prov == "passthrough:oov" else "passthrough" if prov == "passthrough" else
               "rule" if prov == "rule:oov" else "gloss" if prov == "user:glossary" else
               "tm" if prov.startswith("tm:") else "phrase")
        cov[key] += k
    tot = sum(cov.values()) or 1
    res["gate.tokens"] = cov
    res["gate.coverage_phrase"] = round((cov["phrase"] + cov["tm"]) / tot, 4)
    res["gate.oov_rate"] = round(cov["oov"] / tot, 4)
    res["gate.rule_rate"] = round(cov["rule"] / tot, 4)
    if a.judge:
        for js, J in JUDGE.items():
            idx = [int(x) for x in lines(J["idx"])]
            src = lines(f"{out}/{J['from']}.ko")
            ko = f"{out}/{js}.ko"
            open(ko, "w", encoding="utf-8").write("".join(src[i] + "\n" for i in idx))
            j = [sys.executable, "-I", f"{T}/judge.py", "--set", J["dir"], "--sys", f"smt={ko}"]
            for b in ("hd076", "ssnmt"):
                j += ["--sys", f"{b}={R}/base/{js}.{b}.ko"]
            j += ["--baselines", "hd076,ssnmt", "--out", f"{out}/{js}.judge.json"]
            with open(f"{out}/{js}.judge.md", "w") as fo:
                subprocess.run(j, stdout=fo, stderr=open(f"{out}/{js}.judge.log", "w"), check=True)
        res.update(pooled_judge([f"{out}/{js}.judge.json" for js in JUDGE]))
    if a.flip:
        p = subprocess.run([sys.executable, "-I", f"{T}/flip.py", "run", "--cmd",
                            " ".join(shlex.quote(c) for c in cmd + ["--each"]), "--save", f"{out}/flip.ko"],
                           capture_output=True, text=True)
        open(f"{out}/flip.txt", "w").write(p.stdout)
        for l in p.stdout.split("\n"):
            if l.startswith("all") or "pass" in l[:40]:
                res.setdefault("flip", []).append(l.strip())
    json.dump(res, open(f"{out}/summary.json", "w"), ensure_ascii=False, indent=1)
    print(json.dumps(res, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
