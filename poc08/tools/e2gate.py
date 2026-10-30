#!/usr/bin/env python3
"""E2 (PIVOT): the end-to-end arms against 0.7.x on held-out fiction and captions, and the kill criteria.

  poc08/tools/e2gate.py --eval EVALDIR --runs RUNSDIR --tools EVALTOOLS --arm NAME='hdtrans08 ARGS' [--arm …]
                        [--sets fiction,captions] [--carryover PCT]

Runs only at gate time (the eval sets are read once). For each arm and set: run_system.py (document
mode), then judge.py against the cached hd076 baseline with the second judge on every line (--frac2 1),
then the criteria:
  - crit/major lines down >= 30% relative to hd076 (judge 1);
  - lines worse by both judges <= 15%;
  - carry-over of the rule cases >= 90% (poc08/tools/carryover.sh, passed in with --carryover).
Outputs and judgments stay in RUNSDIR; only the aggregates are printed.
"""
import argparse
import json
import os
import subprocess
import sys


def run(cmd):
    print("+", " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eval", required=True)
    ap.add_argument("--runs", required=True)
    ap.add_argument("--tools", required=True)
    ap.add_argument("--arm", action="append", required=True, help="NAME=command")
    ap.add_argument("--sets", default="fiction,captions")
    ap.add_argument("--carryover", type=float)
    a = ap.parse_args()
    rows = []
    for spec in a.arm:
        name, cmd = spec.split("=", 1)
        for s in a.sets.split(","):
            out_dir = os.path.join(a.runs, "systems", name)
            os.makedirs(out_dir, exist_ok=True)
            os.makedirs(os.path.join(a.runs, "runs", name), exist_ok=True)
            ko = os.path.join(out_dir, f"{s}.ko.txt")
            run([sys.executable, os.path.join(a.tools, "run_system.py"), "--set", os.path.join(a.eval, s),
                 "--cmd", cmd, "--out", ko])
            base = os.path.join(a.runs, "baselines", s, "hd076.ko.txt")
            rep_path = os.path.join(a.runs, "runs", name, f"e2-{s}.json")
            run([sys.executable, os.path.join(a.tools, "judge.py"), "--set", os.path.join(a.eval, s),
                 "--sys", f"{name}={ko}", "--sys", f"hd076={base}", "--baselines", "hd076", "--frac2", "1",
                 "--no-human", "--out", rep_path])
            rep = json.load(open(rep_path, encoding="utf-8"))
            m1 = rep["judges"][0]["model"]
            arm_cm = rep["mqm"][m1][name]["cm_lines_pct"]
            base_cm = rep["mqm"][m1]["hd076"]["cm_lines_pct"]
            pair = rep["agreement"]["pairs"].get(f"{name} vs hd076", {})
            worse_both = pair.get("x_worse_by_both", 0) / max(1, rep["lines"])
            j1 = rep["pairs"].get(f"{name} vs hd076", {}).get(m1, {})
            rows.append((name, s, arm_cm, base_cm, worse_both, j1.get("win"), j1.get("tie"), j1.get("loss")))
    print("arm\tset\tcrit/major arm\thd076\trelative change\tworse by both\twins:ties:losses\tkill")
    for name, s, arm_cm, base_cm, wb, w, t, l in rows:
        rel = (arm_cm - base_cm) / base_cm if base_cm else 0.0
        kill = []
        if rel > -0.30:
            kill.append("crit/major not down 30%")
        if wb > 0.15:
            kill.append("worse by both > 15%")
        if a.carryover is not None and a.carryover < 90:
            kill.append("carry-over < 90%")
        print(f"{name}\t{s}\t{arm_cm:.1f}%\t{base_cm:.1f}%\t{100 * rel:+.1f}%\t{100 * wb:.1f}%\t{w}:{t}:{l}\t"
              f"{'; '.join(kill) or 'pass'}")


if __name__ == "__main__":
    main()
