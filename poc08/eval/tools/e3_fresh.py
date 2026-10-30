#!/usr/bin/env python3
"""E3: draw fresh dev lines (never judged, not held out) for the regression check of a lexicon audit.

  e3_fresh.py --out FRESH_DIR [--seed 803] [--offline]

Exclusion: the study pool of the held-out builder (its excluded.py, imported read-only by path), every
line of the held-out sets' lines.tsv, and every work id named in the held-out SOURCES.tsv or in
FRESH_DIR/raw/known-ncodes.txt (work ids found in the dev corpora and study notes).

- Captions (150 lines): 10 windows from units of the caption bundle that the held-out set did not use
  (anime 3, drama 3, movie 2, variety 2). A window has 5 lead lines of context and runs until 15 lines
  are scorable.
- Fiction (150 lines): 10 web-novel chapters of works outside the pool: 6 from genres the held-out set
  did not use, 4 from deeper weekly-ranking pages (ranks 101-300) of genres it used. A serial gives a
  random chapter among its first 30. One window per chapter: 3 lead lines, then 15 scorable lines.
  Any chapter that shares a line of 8+ characters with the pool is dropped with its work.
Scorable: 4-160 characters, at least 3 kana/kanji, not in the pool, not a held-out line.
Network: one request per 3 s; every response is kept in FRESH_DIR/raw/, so --offline rebuilds the
same set from raw/ alone.
"""
import argparse
import csv
import glob
import html
import importlib.util
import json
import os
import random
import re
import sys
import time
import urllib.parse
import urllib.request

EVAL = "/Users/sokcuri/hdwork/v08/eval"
JP = re.compile(r"[぀-ヿ㐀-鿿豈-﫿ｦ-ﾟ]")
UA = "Mozilla/5.0 (compatible; dev-corpus sampler; one request per 3 s)"
UNUSED_GENRES = [203, 204, 401, 402, 404, 9901]          # 純文学 アクション VRゲーム 宇宙 パニック 童話
DEEP_GENRES = [101, 201, 102, 302]                        # used by the held-out set; ranks 101-300
CAP_PLAN = [("anime", 3), ("drama", 3), ("movie", 2), ("variety", 2)]
_last = [0.0]

csv.field_size_limit(1 << 30)


def excluded_module():
    spec = importlib.util.spec_from_file_location("excluded", os.path.join(EVAL, "scripts", "excluded.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def eval_pool():
    lines, ncodes, titles, units = set(), set(), set(), set()
    for path in glob.glob(os.path.join(EVAL, "*", "lines.tsv")):
        with open(path, encoding="utf-8", newline="") as f:
            for r in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
                lines.add(r["ja"].strip())
    with open(os.path.join(EVAL, "SOURCES.tsv"), encoding="utf-8") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            ncodes.update(x.lower() for x in re.findall(r"n\d{4}[a-z]{1,2}", " ".join(p[1:3])))
            m = re.search(r"work: (.*?);", p[3] if len(p) > 3 else "")
            if m:
                titles.add(m.group(1))
            m = re.match(r"cap-([a-z]+)-([a-z]\d+)-", p[1])
            if m:
                units.add((m.group(1), m.group(2)))
    return lines, ncodes, titles, units


def scorable(s, pool):
    s = s.strip()
    return 4 <= len(s) <= 160 and len(JP.findall(s)) >= 3 and s not in pool


def fetch(url, path, offline):
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            return f.read()
    if offline:
        raise SystemExit(f"--offline: {path} missing")
    wait = _last[0] + 3.0 - time.time()
    if wait > 0:
        time.sleep(wait)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            text = r.read().decode("utf-8", "replace")
    finally:
        _last[0] = time.time()
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return text


def api(raw, offline, **q):
    q = dict(out="json", of="n-t-g-nt-ga", **q)
    name = "api-" + "-".join(f"{k}{v}" for k, v in sorted(q.items()) if k not in ("out", "of")) + ".json"
    text = fetch("https://api.syosetu.com/novelapi/api/?" + urllib.parse.urlencode(q),
                 os.path.join(raw, name), offline)
    return json.loads(text)[1:]


def chapter_lines(page):
    m = re.search(r'<div class="js-novel-text p-novel__text">(.*?)</div>', page, re.S)
    if not m:
        return []
    out = []
    for p in re.findall(r'<p id="L\d+">(.*?)</p>', m.group(1), re.S):
        p = re.sub(r"<rp>.*?</rp>|<rt>.*?</rt>", "", p, flags=re.S)
        p = html.unescape(re.sub(r"<[^>]+>", "", p)).strip()
        if p:
            out.append(p)
    return out


def window(lines, pool, rng, lead, want):
    """Pick a start so that `want` scorable lines follow `lead` context lines; None if the text is short."""
    starts = list(range(lead, len(lines)))
    rng.shuffle(starts)
    for s in starts:
        n, e = 0, s
        while e < len(lines) and n < want:
            n += scorable(lines[e], pool)
            e += 1
        if n == want:
            return s - lead, s, e
    return None


def fiction(raw, pool, ev_ncodes, ev_titles, known, rng, offline):
    picks = []
    plan = [("unused-genre", g, 1) for g in UNUSED_GENRES] + [("deep-rank", g, 101) for g in DEEP_GENRES]
    used = set()
    for kind, genre, st in plan:
        cands = []
        for off in ((st,) if kind == "unused-genre" else (st, st + 100)):
            for i, w in enumerate(api(raw, offline, genre=genre, order="weekly", lim=100, st=off)):
                nc = w["ncode"].lower()
                if nc in ev_ncodes or nc in known or nc in used or w["title"] in ev_titles:
                    continue
                cands.append(dict(w, ncode=nc, rank=off + i))
        cands.sort(key=lambda w: w["ncode"])
        rng.shuffle(cands)
        for w in cands[:6]:
            serial = w["noveltype"] == 1
            ch = rng.randint(1, min(30, max(1, w["general_all_no"]))) if serial else 0
            url = f"https://ncode.syosetu.com/{w['ncode']}/" + (f"{ch}/" if serial else "")
            page = fetch(url, os.path.join(raw, f"{w['ncode']}-{ch}.html"), offline)
            lines = chapter_lines(page)
            if any(len(x) >= 8 and x in pool for x in lines):
                continue                                   # the work was seen by a study
            win = window(lines, pool, rng, 3, 15)
            if not win:
                continue
            used.add(w["ncode"])
            picks.append({"doc_id": f"e3f-{w['ncode']}-{ch}", "lines": lines[win[0]:win[2]],
                          "lead": win[1] - win[0], "meta": {"genre": w["genre"], "title": w["title"],
                          "url": url, "pick": kind, "weekly_rank": w["rank"]}})
            break
        else:
            print(f"fiction: no usable work for genre {genre}", file=sys.stderr)
    return picks


def captions(ex, pool, ev_units, rng):
    by_unit = {}
    for r in ex.bundle_rows():
        if r[2] == "live":
            continue
        by_unit.setdefault((r[1], r[3]), []).append(r)
    picks = []
    for corpus, n in CAP_PLAN:
        units = sorted(u for u in by_unit if u[0] == corpus and u not in ev_units)
        rng.shuffle(units)
        for u in units:
            if sum(p["corpus"] == corpus for p in picks) == n:
                break
            rows = [r for r in by_unit[u]]
            texts = [r[6] if r[5] == "speech" else "" for r in rows]
            win = window(texts, pool, rng, 5, 15)
            if not win:
                continue
            sel = rows[win[0]:win[2]]
            picks.append({"corpus": corpus, "doc_id": f"e3c-{corpus}-{u[1]}-{sel[0][0]}",
                          "lines": [r[6] for r in sel], "lead": win[1] - win[0],
                          "metas": [{"corpus": r[1], "layout": r[2], "unit": r[3], "input_line": r[0],
                                     "start": r[4], "kind": r[5]} for r in sel]})
    return picks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=803)
    ap.add_argument("--offline", action="store_true")
    a = ap.parse_args()
    raw = os.path.join(a.out, "raw")
    os.makedirs(raw, exist_ok=True)
    os.makedirs(os.path.join(a.out, "docs"), exist_ok=True)
    ex = excluded_module()
    pool, _ = ex.load()
    ev_lines, ev_ncodes, ev_titles, ev_units = eval_pool()
    pool = set(pool) | ev_lines
    known = set()
    kpath = os.path.join(raw, "known-ncodes.txt")
    if os.path.exists(kpath):
        known = {x.strip().lower() for x in open(kpath, encoding="utf-8") if x.strip()}
    rows = []
    cap = captions(ex, pool, ev_units, random.Random(a.seed))
    fic = fiction(raw, pool, ev_ncodes, ev_titles, known, random.Random(a.seed + 1), a.offline)
    for prefix, docs in (("e3c", cap), ("e3f", fic)):
        n = 0
        for d in docs:
            with open(os.path.join(a.out, "docs", d["doc_id"] + ".ja.txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(d["lines"]) + "\n")
            for i, s in enumerate(d["lines"]):
                sc = i >= d["lead"] and scorable(s, pool)
                meta = dict(d["metas"][i]) if "metas" in d else dict(d["meta"])
                meta["window"] = docs.index(d) + 1
                n += sc
                rows.append([f"{prefix}-{len([r for r in rows if r[0].startswith(prefix)]) + 1:04d}",
                             d["doc_id"], str(i + 1), "1" if sc else "0", s.replace("\t", " "),
                             json.dumps(meta, ensure_ascii=False, sort_keys=True)])
        print(f"{prefix}: {len(docs)} docs, {n} scored lines")
    with open(os.path.join(a.out, "lines.tsv"), "w", encoding="utf-8") as f:
        f.write("id\tdoc_id\tline_no\tscored\tja\tmeta\n")
        for r in rows:
            f.write("\t".join(r) + "\n")


if __name__ == "__main__":
    main()
