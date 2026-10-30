#!/usr/bin/env python3
"""Builds one model folder from teacher-translated pairs (build time).

  build.py OUT --pairs JA KO [--pairs JA KO ...] [--limit N] [--heldout 1000] [--order 4] [--prune 0,0,1,1]
           [--sig a+e] [--prov NAME] [--block BLOCK.keys] [--jobs 24] [--from STEP]

Steps: filter (missing, kana left, length ratio, duplicates, hash filter against held-out/dev lines) →
Japanese tokens (W1 analyzer) → Korean tokens (kseg.py) → fast_align both ways + grow-diag-final-and →
phrases.py → lm.py → the Korean round trip on the held-out pairs. OUT gets phrases.tsv, lm.bin,
weights.tsv, build.json; work files stay in OUT/work.
"""
import argparse
import hashlib
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import smtlib

FA = os.environ.get("FAST_ALIGN", "/Users/sokcuri/hdwork/v08/smt/ext/fast_align/build")
PY = sys.executable
KANA = re.compile(r"[぀-ヿ]")


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=sys.stderr, flush=True)


def read_lines(p):
    t = open(p, encoding="utf-8").read().split("\n")
    if t and t[-1] == "":
        t.pop()
    return t


def write_lines(p, ls):
    with open(p, "w", encoding="utf-8") as f:
        f.write("\n".join(ls) + "\n")


def filter_pairs(pairs_args, block, limit, seed):
    keys = set(open(block).read().split()) if block else set()
    pairs, drop, seen = [], {"missing": 0, "kana-left": 0, "ratio": 0, "dup": 0, "blocked": 0}, set()
    for jf, kf in pairs_args:
        J, K = read_lines(jf), read_lines(kf)
        for j, k in zip(J, K):
            j, k = j.strip(), k.strip()
            if not j or not k:
                drop["missing"] += 1
                continue
            if KANA.search(k):
                drop["kana-left"] += 1
                continue
            r = len(k) / max(1, len(j))
            if not 0.4 <= r <= 2.5:
                drop["ratio"] += 1
                continue
            if (j, k) in seen:
                drop["dup"] += 1
                continue
            if any(hashlib.sha1(x.encode()).hexdigest() in keys for x in smtlib.sentence_keys(j)):
                drop["blocked"] += 1
                continue
            seen.add((j, k))
            pairs.append((j, k))
    random.Random(seed).shuffle(pairs)
    if limit:
        pairs = pairs[:limit]
    return pairs, drop


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--pairs", nargs=2, action="append", default=[])
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--heldout", type=int, default=1000)
    ap.add_argument("--order", type=int, default=4)
    ap.add_argument("--prune", default="0,0,1,1")
    ap.add_argument("--sig", default="a+e")
    ap.add_argument("--prov", default="smt")
    ap.add_argument("--block", default="/Users/sokcuri/hdwork/v08/smt/work/block.keys")
    ap.add_argument("--jobs", type=int, default=24)
    ap.add_argument("--from", dest="start", default="filter")
    a = ap.parse_args()
    steps = ["filter", "ja", "ko", "align", "phrases", "lm", "rt"]
    start = steps.index(a.start)
    W = os.path.join(a.out, "work")
    os.makedirs(W, exist_ok=True)
    info = {}
    ip = os.path.join(a.out, "build.json")
    if os.path.exists(ip):
        info = json.load(open(ip))
    t0 = time.time()

    if start <= 0:
        pairs, drop = filter_pairs(a.pairs, a.block, a.limit + a.heldout if a.limit else 0, 808)
        ho, tr = pairs[:a.heldout], pairs[a.heldout:]
        write_lines(f"{W}/train.ja", [j for j, _ in tr])
        write_lines(f"{W}/train.ko", [k for _, k in tr])
        write_lines(f"{W}/heldout.ja", [j for j, _ in ho])
        write_lines(f"{W}/heldout.ko", [k for _, k in ho])
        info.update(sources=a.pairs, train=len(tr), heldout=len(ho), dropped=drop)
        log("filter", len(tr), "train", len(ho), "heldout", drop)
    if start <= 1:
        for part in ("train", "heldout"):
            J = read_lines(f"{W}/{part}.ja")
            out = []
            B = 20000
            for i in range(0, len(J), B):
                out += smtlib.ja_tokens(J[i:i + B])
            write_lines(f"{W}/{part}.ja.surf", [" ".join(s) for s, _ in out])
            write_lines(f"{W}/{part}.ja.lem", [" ".join(l) for _, l in out])
        log("ja tokens")
    if start <= 2:
        for part in ("train", "heldout"):
            subprocess.run([PY, "-I", f"{HERE}/kseg.py", f"{W}/{part}.ko", f"{W}/{part}.ko.tok", "--rt",
                            f"{W}/{part}.ko.rt", "--norm", f"{W}/{part}.ko.norm", "--jobs", str(a.jobs)], check=True)
        log("ko tokens")
    if start <= 3:
        S, L, K = read_lines(f"{W}/train.ja.surf"), read_lines(f"{W}/train.ja.lem"), read_lines(f"{W}/train.ko.tok")
        ok = [i for i in range(len(S)) if S[i].strip() and K[i].strip()]
        info["aligned_pairs"] = len(ok)
        write_lines(f"{W}/keep.idx", [str(i) for i in ok])
        for name, src in (("surf", S), ("lem", L), ("tok", K)):
            write_lines(f"{W}/a.{name}", [src[i] for i in ok])
        NK = read_lines(f"{W}/train.ko.norm")
        write_lines(f"{W}/a.ko.txt", [NK[i] for i in ok])
        write_lines(f"{W}/fa.in", [f"{L[i]} ||| {K[i]}" for i in ok])
        with open(f"{W}/fwd.al", "w") as fo:
            subprocess.run([f"{FA}/fast_align", "-i", f"{W}/fa.in", "-d", "-o", "-v"], stdout=fo,
                           stderr=subprocess.DEVNULL, check=True)
        with open(f"{W}/rev.al", "w") as fo:
            subprocess.run([f"{FA}/fast_align", "-i", f"{W}/fa.in", "-d", "-o", "-v", "-r"], stdout=fo,
                           stderr=subprocess.DEVNULL, check=True)
        with open(f"{W}/sym.al", "w") as fo:
            subprocess.run([f"{FA}/atools", "-i", f"{W}/fwd.al", "-j", f"{W}/rev.al", "-c", "grow-diag-final-and"],
                           stdout=fo, check=True)
        log("align")
    if start <= 4:
        subprocess.run([PY, "-I", f"{HERE}/phrases.py", f"{W}/a.surf", f"{W}/a.tok", f"{W}/sym.al",
                        f"{a.out}/phrases.tsv", "--sig", a.sig, "--prov", a.prov, "--jobs", str(a.jobs)], check=True)
        subprocess.run([PY, "-I", f"{HERE}/tm.py", f"{W}/a.surf", f"{W}/a.tok", f"{a.out}/tm.tsv", "--prov",
                        a.prov.replace("smt:", "tm:")], check=True)
        log("phrases")
    if start <= 5:
        subprocess.run([PY, "-I", f"{HERE}/lm.py", f"{W}/a.tok", f"{a.out}/lm.bin", "--order", str(a.order),
                        "--prune", a.prune], check=True)
        log("lm")
    if start <= 6:
        hd8smt = os.environ.get("HD8SMT", os.path.join(smtlib.ROOT, "build", "mac", "poc08", "smt", "hd8smt"))
        rt = read_lines(f"{W}/heldout.ko.rt")
        p = subprocess.run([hd8smt, "ko-join"], input="\n".join(rt) + "\n", capture_output=True, text=True, check=True)
        got = p.stdout.split("\n")
        ref = read_lines(f"{W}/heldout.ko.norm")
        exact = sum(1 for x, y in zip(ref, got) if x == y)
        info["roundtrip"] = {"lines": len(ref), "exact": exact, "rate": exact / max(1, len(ref))}
        log("round trip", exact, "/", len(ref))
    if not os.path.exists(f"{a.out}/weights.tsv"):
        shutil.copy(f"{HERE}/../data/weights.default.tsv", f"{a.out}/weights.tsv")
    info["seconds"] = info.get("seconds", 0) + round(time.time() - t0)
    json.dump(info, open(ip, "w"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
