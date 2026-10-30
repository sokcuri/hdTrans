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
        if a.judge:
            j = [sys.executable, "-I", f"{T}/judge.py", "--set", S["dir"], "--sys", f"smt={ko}"]
            for b, bp in S["base"].items():
                j += ["--sys", f"{b}={bp}"]
            j += ["--baselines", ",".join(S["base"]), "--out", f"{out}/{s}.judge.json"]
            with open(f"{out}/{s}.judge.md", "w") as fo:
                subprocess.run(j, stdout=fo, stderr=open(f"{out}/{s}.judge.log", "w"), check=True)
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
