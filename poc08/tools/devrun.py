#!/usr/bin/env python3
"""End-to-end dev-set run of hdtrans08: coverage, invariant flags, evidence, speed and memory.

  poc08/tools/devrun.py [--bin HDTRANS08] [--out DIR] [--mode each|doc] [--threads N] FILE...

Each FILE is UTF-8 text, one line per line. Writes DIR/<name>.json (the --json output) and
DIR/<name>.txt and prints one summary row per file plus the totals. Gates (POC-PLAN G11, G12):
traced output bytes 100%, >= 1,500 lines/s with --each, startup <= 0.5 s, RSS <= 200 MB.
"""
import argparse
import collections
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def run(binary, path, mode, threads, outdir):
    args = [binary, "--json", "--stats", "--in", str(path)]
    if mode == "each":
        args.insert(1, "--each")
        if threads > 1:
            args[1:1] = ["--threads", str(threads)]
    env = dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off")
    start = time.monotonic()
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        proc = subprocess.Popen(args, stdout=fo, stderr=fe, env=env)
        _, status, usage = os.wait4(proc.pid, 0)  # this child's own peak RSS
        proc.returncode = os.waitstatus_to_exitcode(status)
        wall = time.monotonic() - start
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read(), fe.read()
    if proc.returncode != 0:
        sys.exit(f"{binary} failed on {path}: {err.decode(errors='replace')}")
    data = json.loads(out)
    name = Path(path).stem
    (outdir / f"{name}.json").write_bytes(out)
    (outdir / f"{name}.txt").write_text(data["text"] + ("" if data["text"].endswith("\n") else "\n"))
    stats = {}
    for line in err.decode(errors="replace").splitlines():
        if line.startswith("hdtrans08 stats: ") or line.startswith("hdtrans08 stages: "):
            for part in line.split(": ", 1)[1].split():
                k, _, v = part.partition("=")
                stats[k] = v
    return data, stats, wall, usage.ru_maxrss


def summarize(data):
    s = collections.Counter()
    flags = collections.Counter()
    evidence = collections.Counter()
    for ln in data["lines"]:
        s["lines"] += 1
        s["covered"] += ln["covered"]
        s["traced"] += ln["traced"][0]
        s["outBytes"] += ln["traced"][1]
        kinds = set()
        for f in ln["flags"]:
            key = f["kind"] + (":" + f["inv"] if "inv" in f else "")
            flags[key] += 1
            kinds.add(key)
        for k in kinds:
            flags["lines:" + k] += 1
        for k, v in ln["evidence"].items():
            evidence[k] += v
        s["inv_lines"] += any(f.get("inv") for f in ln["flags"])
    return s, flags, evidence


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=str(ROOT / "build/mac/poc08/hdtrans08"))
    ap.add_argument("--out", default=None)
    ap.add_argument("--mode", default="each", choices=["each", "doc"])
    ap.add_argument("--threads", type=int, default=1)
    ap.add_argument("--determinism", action="store_true",
                    help="also run each file 3 times with 1 and 2 threads (--each) and compare outputs (G11)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    outdir = Path(a.out or tempfile.mkdtemp(prefix="hd08-dev-"))
    outdir.mkdir(parents=True, exist_ok=True)
    print(f"output: {outdir}")
    print("file\tlines\tcovered\ttraced\tinv-lines\tlines/s\tstartup\twall\tmaxrss-MB")
    total_flags = collections.Counter()
    total_ev = collections.Counter()
    for f in a.files:
        data, stats, wall, rss = run(a.bin, f, a.mode, a.threads, outdir)
        s, flags, ev = summarize(data)
        total_flags.update(flags)
        total_ev.update(ev)
        rss_mb = rss / (1024 * 1024) if sys.platform == "darwin" else rss / 1024
        print(f"{Path(f).name}\t{s['lines']}\t{s['covered']} ({100 * s['covered'] / max(1, s['lines']):.1f}%)\t"
              f"{100 * s['traced'] / max(1, s['outBytes']):.2f}%\t{s['inv_lines']}\t{stats.get('lines_per_s', '?')}"
              f"\t{stats.get('startup_s', '?')}\t{wall:.2f}s\t{rss_mb:.0f}")
        print(f"  stats: {stats}")
    print("flags (count; lines:<kind> = lines with it):")
    for k, v in sorted(total_flags.items()):
        print(f"  {k}\t{v}")
    print("evidence:", dict(total_ev))
    if a.determinism:
        for f in a.files:
            seen = set()
            for threads in (1, 2):
                for _ in range(3):
                    env = dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off")
                    out = subprocess.run([a.bin, "--each", "--threads", str(threads), "--json", "--in", f],
                                         capture_output=True, env=env, check=True).stdout
                    seen.add(out)
            print(f"determinism {Path(f).name}: {'same' if len(seen) == 1 else f'{len(seen)} different outputs'}")
    print(f"stubs: {data.get('stubs') or 'none'}")


if __name__ == "__main__":
    main()
