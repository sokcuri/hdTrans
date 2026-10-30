#!/usr/bin/env python3
"""Translate a frozen held-out set with any line-in/line-out engine and keep the scored lines.

  run_system.py --set EVALDIR/SET --cmd 'ENGINE ARGS' --out OUT.ko.txt [--mode doc|line]

doc (default): every docs/<doc>.ja.txt goes through the engine on stdin as one document (context and
speech level carry across lines); the scored rows are picked by line_no from lines.tsv.
line: scored.ja.txt goes through the engine in one call; the engine must keep the line count.
OUT has one line per scored line, in id order (aligned with scored.ja.txt). Engines run with
HOME=<tmp> and HDTRANS_CACHE_DIR=off. Prints lines/s.
"""
import argparse
import csv
import os
import shlex
import subprocess
import sys
import tempfile
import time

csv.field_size_limit(1 << 30)


def rows(set_dir):
    with open(os.path.join(set_dir, "lines.tsv"), encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def engine(cmd, text):
    p = subprocess.run(shlex.split(cmd), input=text.encode("utf-8"), capture_output=True,
                       env=dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off"))
    if p.returncode:
        sys.exit(f"engine failed ({p.returncode}): {p.stderr.decode()[-800:]}")
    out = p.stdout.decode("utf-8").split("\n")
    if out and out[-1] == "":
        out.pop()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--set", required=True)
    ap.add_argument("--cmd", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=["doc", "line"], default="doc")
    a = ap.parse_args()
    t0 = time.time()
    scored = [r for r in rows(a.set) if r["scored"] == "1"]
    n_in = 0
    if a.mode == "line":
        with open(os.path.join(a.set, "scored.ja.txt"), encoding="utf-8") as f:
            src = f.read().rstrip("\n").split("\n")
        out = engine(a.cmd, "\n".join(src) + "\n")
        n_in = len(src)
        if len(out) != len(src):
            sys.exit(f"line count {len(out)} != {len(src)}")
    else:
        docs = {}
        for r in scored:
            if r["doc_id"] not in docs:
                path = os.path.join(a.set, "docs", r["doc_id"] + ".ja.txt")
                with open(path, encoding="utf-8") as f:
                    lines = f.read().split("\n")
                if lines and lines[-1] == "":
                    lines.pop()
                ko = engine(a.cmd, "\n".join(lines) + "\n")
                if len(ko) != len(lines):
                    sys.exit(f"{path}: {len(ko)} lines out for {len(lines)}")
                docs[r["doc_id"]] = ko
                n_in += len(lines)
        out = [docs[r["doc_id"]][int(r["line_no"]) - 1].strip() for r in scored]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(o.replace("\t", " ") for o in out) + "\n")
    dt = time.time() - t0
    print(f"{a.out}: {len(out)} scored lines ({n_in} input lines, {dt:.1f} s, {n_in / max(dt, 1e-9):.0f} lines/s)")


if __name__ == "__main__":
    main()
