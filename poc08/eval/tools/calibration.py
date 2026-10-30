#!/usr/bin/env python3
"""Owner calibration sheet: N lines per set with blind candidates, to be rated by a person.

  calibration.py --evaldir EVAL --baselines DIR --systems a,b,c --per-set 20 --out SHEET.tsv
DIR/<set>/<system>.ko.txt are the outputs. The game reference joins as a candidate. SHEET.tsv has
one row per (line, candidate) with empty columns rating (ok|minor|major|critical) and best (x on
the preferred candidate of the line) and note; SHEET.key.tsv maps candidate letters to systems.
Identical outputs of a line are shown once. Lines are drawn with seed 808, spread over documents.
"""
import argparse
import csv
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from judge import load_set, read_lines  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evaldir", required=True)
    ap.add_argument("--baselines", required=True)
    ap.add_argument("--systems", required=True)
    ap.add_argument("--per-set", type=int, default=20)
    ap.add_argument("--sets", default="fiction,game,captions,broadcast,web")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    rnd = random.Random(808)
    sheet = [["cid", "set", "line_id", "context", "ja", "cand", "ko", "rating", "best", "note"]]
    key = [["cid", "line_id", "cand", "systems"]]
    for st in a.sets.split(","):
        lines = load_set(os.path.join(a.evaldir, st))
        outs = {s: read_lines(os.path.join(a.baselines, st, s + ".ko.txt")) for s in a.systems.split(",")}
        if all(l["ref"] for l in lines):
            outs["human"] = [l["ref"] for l in lines]
        pick = sorted(rnd.sample(range(len(lines)), a.per_set))
        for li in pick:
            L = lines[li]
            cands = {}
            for s, o in outs.items():
                cands.setdefault(o[li].strip(), []).append(s)
            order = list(cands)
            rnd.shuffle(order)
            for n, ko in enumerate(order):
                c = "ABCDE"[n]
                cid = f"{L['id']}{c}"
                ctx = " / ".join(L["ctx"]) + (f" [speaker {L['speaker']}]" if L["speaker"] else "")
                sheet.append([cid, st, L["id"], ctx, L["ja"], c, ko, "", "", ""])
                key.append([cid, L["id"], c, "+".join(cands[ko])])
    for path, rows in ((a.out, sheet), (a.out.replace(".tsv", ".key.tsv"), key)):
        with open(path, "w", encoding="utf-8", newline="") as f:
            csv.writer(f, delimiter="\t", lineterminator="\n").writerows(rows)
    print(f"{a.out}: {len(sheet) - 1} candidates on {a.per_set * len(a.sets.split(','))} lines")


if __name__ == "__main__":
    main()
