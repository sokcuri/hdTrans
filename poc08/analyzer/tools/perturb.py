#!/usr/bin/env python3
"""Seeded noise for the input-repair gate (ja/repair.tsv).

    perturb.py IN.ja OUTDIR [--seed N] [--kinds elong,kata,...]

Writes OUTDIR/clean.ja (a copy of IN) and OUTDIR/<kind>.ja for each kind, line for line. Each kind
mimics one kind of noisy input:
  elong      one ー, ーー, 〜 or っ inserted after a kana
  kata       each hiragana letter written in katakana with p = 0.6
  small2big  each small kana written big with p = 0.93
  dakuten    each voiced or half-voiced kana loses its mark with p = 0.45
  swap       one pair of adjacent different kana (not small, not ー) transposed
  space      one space inserted between two characters
  repeat     one character written three times
The same seed and input give the same output.
"""
import argparse
import os
import random
import sys
import unicodedata

KINDS = ["elong", "kata", "small2big", "dakuten", "swap", "space", "repeat"]
SMALL = "ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶ"
BIG = "あいうえおつやゆよわかけアイウエオツヤユヨワカケ"
SMALL2BIG = dict(zip(SMALL, BIG))


def is_hira(c):
    return "ぁ" <= c <= "ゖ"


def is_kata(c):
    return "ァ" <= c <= "ヺ"


def is_kana(c):
    return is_hira(c) or is_kata(c)


def unvoice(c):
    d = unicodedata.decomposition(c).split()
    if len(d) == 2 and d[1] in ("3099", "309A"):
        return chr(int(d[0], 16))
    return None


def elong(line, rng):
    pos = [i + 1 for i, c in enumerate(line) if is_kana(c) and c not in SMALL]
    if not pos:
        return line
    p = rng.choice(pos)
    return line[:p] + rng.choice(["ー", "ーー", "〜", "っ"]) + line[p:]


def kata(line, rng):
    return "".join(chr(ord(c) + 0x60) if is_hira(c) and rng.random() < 0.6 else c for c in line)


def small2big(line, rng):
    return "".join(SMALL2BIG[c] if c in SMALL2BIG and rng.random() < 0.93 else c for c in line)


def dakuten(line, rng):
    out = []
    for c in line:
        u = unvoice(c)
        out.append(u if u and is_kana(c) and rng.random() < 0.45 else c)
    return "".join(out)


def swap(line, rng):
    ok = lambda c: is_kana(c) and c not in SMALL and c != "ー"
    pos = [i for i in range(len(line) - 1) if ok(line[i]) and ok(line[i + 1]) and line[i] != line[i + 1]]
    if not pos:
        return line
    i = rng.choice(pos)
    return line[:i] + line[i + 1] + line[i] + line[i + 2:]


def space(line, rng):
    if len(line) < 2:
        return line
    p = rng.randrange(1, len(line))
    return line[:p] + " " + line[p:]


def repeat(line, rng):
    pos = [i for i, c in enumerate(line) if not c.isspace()]
    if not pos:
        return line
    i = rng.choice(pos)
    return line[:i + 1] + line[i] * 2 + line[i + 1:]


FUNCS = {"elong": elong, "kata": kata, "small2big": small2big, "dakuten": dakuten, "swap": swap,
         "space": space, "repeat": repeat}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("outdir")
    ap.add_argument("--seed", type=int, default=8)
    ap.add_argument("--kinds", default=",".join(KINDS))
    a = ap.parse_args()
    with open(a.input, encoding="utf-8") as f:
        lines = f.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    os.makedirs(a.outdir, exist_ok=True)
    with open(os.path.join(a.outdir, "clean.ja"), "w", encoding="utf-8") as f:
        f.write("".join(l + "\n" for l in lines))
    for k in a.kinds.split(","):
        if k not in FUNCS:
            sys.exit("unknown kind " + k)
        # one stream per kind, so adding a kind does not change the others
        rng = random.Random(f"{a.seed}:{k}")
        with open(os.path.join(a.outdir, k + ".ja"), "w", encoding="utf-8") as f:
            for l in lines:
                f.write(FUNCS[k](l, rng) + "\n")


if __name__ == "__main__":
    main()
