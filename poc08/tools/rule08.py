#!/usr/bin/env python3
"""The "0.8-rule" package as one line-in/line-out command (measurement harness):
the 0.7.x engine with the analyzer's segmentation (hdtrans08 --poc08 segment), then the generator's Korean-side
retrofit (hd8retro: speech level, K rewrite rules) on the lines the text marks as dialogue.

  poc08/tools/rule08.py [--bin HDTRANS08] [--retro HD8RETRO] [--dicts DIR] [--data DIR] [--scope dialogue|all]
                        < document > lines

--scope dialogue (default): the retrofit replaces only lines that hold 「 or start with 『 or ≪; other lines keep
the segment output. --scope all: every line the retrofit rebuilt. Lines the retrofit could not read keep the
0.7.x output. Reads a document on stdin and writes one line per input line.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def dialogue(ja):
    s = ja.lstrip()
    return "「" in ja or s.startswith("『") or s.startswith("≪")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=str(ROOT / "build/mac/poc08/hdtrans08"))
    ap.add_argument("--retro", default=str(ROOT / "build/mac/poc08/generator/hd8retro"))
    ap.add_argument("--dicts", default=str(ROOT / "dicts"))
    ap.add_argument("--data", default=str(ROOT / "poc08"))
    ap.add_argument("--scope", choices=["dialogue", "all"], default="dialogue")
    a = ap.parse_args()
    text = sys.stdin.read()
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    tmp = Path(tempfile.mkdtemp(prefix="rule08-"))
    (tmp / "in.txt").write_text("".join(l + "\n" for l in lines), encoding="utf-8")
    env = dict(os.environ, HOME=str(tmp), HDTRANS_CACHE_DIR="off", HD8_FAMILIES=str(ROOT / "src/core/output/ko_families_gen.cpp"))
    with open(tmp / "why.txt", "w") as why:
        subprocess.run([a.bin, "--dicts", a.dicts, "--poc08-data", a.data, "--poc08", "segment", "--why", "--stages",
                        str(tmp / "stg"), "--in", str(tmp / "in.txt"), "--out", str(tmp / "out075.txt")],
                       stderr=why, env=env, check=True)
    out075 = (tmp / "out075.txt").read_text(encoding="utf-8").split("\n")
    with open(tmp / "meta.jsonl", "w", encoding="utf-8") as f:
        for ja in lines:
            f.write(json.dumps({"speaker": "", "scene": "doc", "work": "doc", "dialogue": dialogue(ja)}, ensure_ascii=False) + "\n")
    pre = subprocess.run([sys.executable, str(ROOT / "poc08/generator/tools/retro_convert.py"), str(tmp / "in.txt"),
                          str(tmp / "why.txt"), str(tmp / "out075.txt"), str(tmp / "meta.jsonl"),
                          str(tmp / "stg/input.C.got.tsv")], capture_output=True, text=True, env=env, cwd=ROOT, check=True).stdout
    (tmp / "pre.jsonl").write_text(pre, encoding="utf-8")
    retro = subprocess.run([a.retro, "--data", str(Path(a.data) / "generator/data"), str(tmp / "pre.jsonl")],
                           capture_output=True, text=True, env=env, check=True).stdout
    rebuilt = {}
    for row in retro.splitlines():
        r = json.loads(row)
        if r.get("ok"):
            rebuilt[int(r["line"]) - 1] = r["out"]
    out = []
    for i, ja in enumerate(lines):
        base = out075[i] if i < len(out075) else ""
        use = i in rebuilt and (a.scope == "all" or dialogue(ja))
        out.append(rebuilt[i] if use else base)
    sys.stdout.write("".join(l + "\n" for l in out))


if __name__ == "__main__":
    main()
