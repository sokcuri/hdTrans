#!/usr/bin/env python3
"""One learning-curve point: snapshot N teacher pairs, build, tune, measure on dev.

  curve.py N [--par DIR] [--name NAME] [--judge] [--no-tune] [--init WEIGHTS]

Reads every <source>.ja/<source>.ko of the parallel folder up to the shorter of the two line counts, takes the
first N pairs in source order (sources in the manifest's priority order), builds models/NAME, tunes it on the
real-register tuning set and runs evalrun.py. Appends one row to /Users/sokcuri/hdwork/v08/smt/curve.tsv.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
SMT = "/Users/sokcuri/hdwork/v08/smt"
ORDER = ["game", "aozora", "ln", "captions", "generic"]


def count(p):
    with open(p, "rb") as f:
        return sum(1 for _ in f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("n", type=int)
    ap.add_argument("--par", default="/Users/sokcuri/hdwork/v08/nmt/par")
    ap.add_argument("--name")
    ap.add_argument("--judge", action="store_true")
    ap.add_argument("--no-tune", action="store_true")
    ap.add_argument("--init")
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    name = a.name or f"ds{a.n // 1000}k"
    snap = f"{SMT}/snap/{name}"
    os.makedirs(snap, exist_ok=True)
    srcs = sorted(glob.glob(f"{a.par}/*.ja"), key=lambda p: (ORDER.index(os.path.basename(p)[:-3])
                                                              if os.path.basename(p)[:-3] in ORDER else 99, p))
    left = a.n
    pairs = []
    used = {}
    for jf in srcs:
        kf = jf[:-3] + ".ko"
        if not os.path.exists(kf) or left <= 0:
            continue
        k = min(count(jf), count(kf), left)
        J = open(jf, encoding="utf-8").read().split("\n")[:k]
        K = open(kf, encoding="utf-8").read().split("\n")[:k]
        oj, ok = f"{snap}/{os.path.basename(jf)}", f"{snap}/{os.path.basename(kf)}"
        open(oj, "w", encoding="utf-8").write("\n".join(J) + "\n")
        open(ok, "w", encoding="utf-8").write("\n".join(K) + "\n")
        pairs += ["--pairs", oj, ok]
        used[os.path.basename(jf)[:-3]] = k
        left -= k
    total = sum(used.values())
    print(f"snapshot {total} pairs {used}", file=sys.stderr)
    t0 = time.time()
    model = f"{SMT}/models/{name}"
    subprocess.run([PY, "-I", f"{HERE}/build.py", model, *pairs, "--prov", f"smt:{name}"], check=True)
    tb = time.time() - t0
    if not a.no_tune:
        cmd = [PY, "-I", f"{HERE}/tune.py", model, f"{SMT}/tune/tune.ja", f"{SMT}/tune/tune.ko", "--iters", "6", "--extra", a.extra]
        if a.init:
            cmd += ["--init", a.init]
        subprocess.run(cmd, check=True)
    ev = [PY, "-I", f"{HERE}/evalrun.py", model, name, "--flip", "--extra", a.extra]
    if a.judge:
        ev.append("--judge")
    subprocess.run(ev, check=True, stdout=subprocess.DEVNULL)
    res = json.load(open(f"/Users/sokcuri/hdwork/v08/eval-runs/smt/{name}/summary.json"))
    info = json.load(open(f"{model}/build.json"))
    size = {f: os.path.getsize(f"{model}/{f}") for f in ("phrases.tsv", "lm.bin", "tm.tsv") if os.path.exists(f"{model}/{f}")}
    row = {"name": name, "pairs": total, "sources": used, "train": info.get("train"),
           "dropped": info.get("dropped"), "roundtrip": info.get("roundtrip"), "build_s": round(tb),
           "sizes": size, **{k: v for k, v in res.items() if k not in ("model", "cmd")}}
    with open(f"{SMT}/curve.tsv", "a") as f:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")
    print(json.dumps(row, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
