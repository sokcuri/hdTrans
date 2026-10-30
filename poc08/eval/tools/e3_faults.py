#!/usr/bin/env python3
"""E3: list the lexical (LEX) faults of the 0.7.5 anatomy study, with the frequency stratum of each span.

  C/venv/bin/python e3_faults.py [--a A_DIR] [--lemmas LEMMAS.tsv] --out lex-faults.tsv

One row per error that judge 'a' tagged stage LEX. The span's content lemmas come from UniDic
(fugashi + unidic_lite): the whole line is tagged and the tokens overlapping the span are kept, so the
analysis sees the context; a span not found in the line is tagged alone. Ranks are the content-lemma
ranks over all content lemmas of the lexicon study's counts (its content POS without numerals and
proper nouns, no ASCII lemmas; 37,792 lemmas, so ranks reach past 20k). `rank` is the rank of the rarest content lemma of the span;
proper nouns and numerals count only when the span has nothing else; a span of function words only
is ranked among all lemmas (so it is head). Strata: head (<500), tail-a
(500-2,000), tail-b (2,001-20,000), rare (>20,000 or unseen).
`lex_conf`: where judge 'b' reviewed the line, `agree` if b has an overlapping error tagged LEX,
`other:<STAGE>` if b's overlapping error has another stage, `unflagged` if b has none; else `na`.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

import fugashi

CONTENT = {"名詞", "動詞", "形容詞", "形状詞", "副詞", "連体詞", "代名詞", "接続詞", "感動詞", "接尾辞", "接頭辞"}
SKIP2 = {"数詞", "固有名詞"}
ASCII = re.compile(r"[\x00-\x7f]+")
COLS = ["fault_id", "line_id", "set", "ctx", "ja", "ko075", "ja_span", "ko_span", "fix", "sev", "type",
        "lemmas", "rank", "stratum", "lex_conf"]


def stratum(rank):
    if rank is None or rank > 20000:
        return "rare"
    if rank < 500:
        return "head"
    return "tail-a" if rank <= 2000 else "tail-b"


def load_ranks(path):
    ranks, by_lemma, func = {}, {}, {}
    n = 0
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            func.setdefault(p[0], len(func) + 1)
            if p[2] not in CONTENT or p[3] in SKIP2:
                continue
            if ASCII.fullmatch(p[0]) or ASCII.fullmatch(p[1]):
                continue
            n += 1
            ranks.setdefault(tuple(p[:5]), n)
            by_lemma.setdefault((p[0], p[2]), n)
    return ranks, by_lemma, func


def tokens(tagger, text):
    out, pos = [], 0
    for w in tagger(text):
        i = text.find(w.surface, pos)
        if i < 0:
            i = pos
        pos = i + len(w.surface)
        ft = w.feature
        out.append((i, pos, w.surface, ft.lemma or w.surface, ft.lForm or "*", ft.pos1, ft.pos2 or "*",
                    ft.cType or "*"))
    return out


def find_span(ja, span):
    span = span.strip()
    if not span:
        return None
    i = ja.find(span)
    if i >= 0:
        return i, i + len(span)
    # judges sometimes drop spaces or punctuation inside the span
    core = re.sub(r"[\s　…、。・]", "", span)
    if core and core in ja:
        i = ja.find(core)
        return i, i + len(core)
    return None


def overlap(ja, s1, s2):
    a, b = find_span(ja, s1), find_span(ja, s2)
    if a and b:
        return a[0] < b[1] and b[0] < a[1]
    return s1.strip() and s2.strip() and (s1.strip() in s2 or s2.strip() in s1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", default="/Users/sokcuri/hdwork/v08/A")
    ap.add_argument("--lemmas", default="/Users/sokcuri/hdwork/v08/C/work/lemmas.tsv")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    ranks, by_lemma, func = load_ranks(a.lemmas)
    tagger = fugashi.Tagger()
    sample = {}
    with open(os.path.join(a.a, "data", "sample.tsv"), encoding="utf-8") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            sample[p[0]] = p[1]
    items, judged_a, judged_b = {}, {}, {}
    for path in sorted(glob.glob(os.path.join(a.a, "judge", "in*.jsonl"))):
        for line in open(path, encoding="utf-8"):
            o = json.loads(line)
            items[o["id"]] = o
    for path in sorted(glob.glob(os.path.join(a.a, "judge", "out*.*.jsonl"))):
        target = judged_b if path.endswith(".b.jsonl") else judged_a
        for line in open(path, encoding="utf-8"):
            o = json.loads(line)
            target[o["id"]] = o
    rows = []
    for lid in sorted(judged_a):
        it = items[lid]
        ja = it["ja"]
        toks = None
        for k, e in enumerate(judged_a[lid]["errors"], 1):
            if e["stage"] != "LEX":
                continue
            if toks is None:
                toks = tokens(tagger, ja)
            sp = find_span(ja, e["ja"])
            span_toks = [t for t in toks if sp and t[0] < sp[1] and sp[0] < t[1]] if sp else []
            if not span_toks:
                span_toks = tokens(tagger, e["ja"])
            content, fallback = [], []
            for t in span_toks:
                if t[5] not in CONTENT:
                    continue
                r = ranks.get(tuple(t[3:8])) or by_lemma.get((t[3], t[5]))
                (fallback if t[6] in SKIP2 else content).append((t[3], t[5], r))
            use = content or fallback
            if not use:  # a function-word span (だから, てか): rank among all lemmas, so it lands in head
                use = [(t[3], t[5], func.get(t[3])) for t in span_toks if t[5] not in ("補助記号", "空白")]
            rank = None
            if use:
                rank = max((r if r is not None else 10 ** 9) for _, _, r in use)
                rank = None if rank == 10 ** 9 else rank
            if lid in judged_b:
                conf = "unflagged"
                for eb in judged_b[lid]["errors"]:
                    if overlap(ja, e["ja"], eb["ja"]):
                        conf = "agree" if eb["stage"] == "LEX" else "other:" + eb["stage"]
                        if conf == "agree":
                            break
            else:
                conf = "na"
            rows.append({
                "fault_id": f"{lid}.e{k}", "line_id": lid, "set": sample.get(lid, "?"),
                "ctx": " ⏎ ".join(it.get("ctx") or []), "ja": ja, "ko075": it["ko"],
                "ja_span": e["ja"], "ko_span": e["ko"], "fix": e.get("fix", ""), "sev": e["sev"],
                "type": e["type"],
                "lemmas": " ".join(f"{l}/{p}:{r if r else '-'}" for l, p, r in use),
                "rank": "" if rank is None else str(rank), "stratum": stratum(rank), "lex_conf": conf})
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\t".join(COLS) + "\n")
        for r in rows:
            f.write("\t".join(str(r[c]).replace("\t", " ").replace("\n", " ") for c in COLS) + "\n")
    by = collections.Counter((r["stratum"], r["sev"]) for r in rows)
    strata = ["head", "tail-a", "tail-b", "rare"]
    sevs = ["minor", "major", "critical"]
    print(f"{len(rows)} LEX faults on {len({r['line_id'] for r in rows})} lines -> {a.out}")
    print("| stratum | " + " | ".join(sevs) + " | all | b agrees/b judged |")
    print("|---|" + "---|" * (len(sevs) + 2))
    for s in strata + ["all"]:
        sel = [r for r in rows if s == "all" or r["stratum"] == s]
        cells = [str(sum(1 for r in sel if r["sev"] == v)) for v in sevs]
        bj = [r for r in sel if r["lex_conf"] != "na"]
        print(f"| {s} | " + " | ".join(cells) + f" | {len(sel)} | {sum(r['lex_conf'] == 'agree' for r in bj)}/{len(bj)} |")
    by_set = collections.Counter((r["set"], r["stratum"]) for r in rows)
    print("by set:", {k: v for k, v in sorted(by_set.items())})
    del by


if __name__ == "__main__":
    sys.exit(main())
