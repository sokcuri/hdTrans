#!/usr/bin/env python3
"""Level-gate sets: real VN lines whose every construction lies within the levels built so far (PLAN-v3 §1.3).

  levelsets.py build --level N5 [--pool-index DIR] [--holdout-index DIR] [--out DIR] [--n-pool 600] [--n-holdout 300]

From the pool index (index.py tag) and the hold-out index, picks lines whose index level is at or below LEVEL
(lines with keys no table row maps are left out), with >= 2 kana/kanji and at least one grammar family, half
dialogue and half narration, stratified by title (pool) or by scene (hold-out) with a fixed seed. Hold-out lines
must be reviewed (status 검수 완료). Writes judge.py sets with three lines of scene context:
  OUT/<LEVEL>/pool/lines.tsv      authoring-pool sample (the family agents read these lines; in-sample check)
  OUT/<LEVEL>/holdout/lines.tsv   hold-out sample (read only by the gate)
  OUT/<LEVEL>/README.md           counts
References are shown to judges as anchors, except unreviewed ones (NotReviewed, NeedsRevision, 보류, 미검수).
The corpus layout comes from the private config $HD08_VN_CONF.
"""
import argparse
import collections
import csv
import json
import os
import random
import re

csv.field_size_limit(1 << 30)
V08 = "/Users/sokcuri/hdwork/v08"
CONF = os.environ.get("HD08_VN_CONF", os.path.join(V08, "wsB", "vn.json"))
LEVELS = ["N5", "N4", "N3", "N2", "N1", "VN"]
JA = re.compile(r"[ぁ-ヿ一-鿿]")
UNREVIEWED = ("NotReviewed", "NeedsRevision", "보류", "미검수")


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def one(s):
    return " ".join(s.replace("\r", " ").split("\n")).strip()


def scene_of(rid):
    return rid.rsplit(":", 1)[0]


def largest_remainder(counts, n):
    tot = sum(counts.values()) or 1
    raw = {k: n * v / tot for k, v in counts.items()}
    got = {k: int(x) for k, x in raw.items()}
    for k in sorted(raw, key=lambda k: (-(raw[k] - got[k]), k))[:n - sum(got.values())]:
        got[k] += 1
    return got


def eligible(index_dir, level, rows_by_id, holdout):
    top = LEVELS.index(level)
    out = []
    for r in rd(os.path.join(index_dir, "index.tsv")):
        lev = r["level"]
        if lev.endswith("+?") or LEVELS.index(lev) > top or r["kind"] not in ("dialogue", "narration"):
            continue
        if not any(f for f in r["families"].split(",") if f and not f.startswith(("intj@", "tok.", "spoken."))):
            continue
        src = rows_by_id.get((r["title"], r["id"]))
        if not src or len(JA.findall(src["ja"])) < 2:
            continue
        if holdout and not src["status"].startswith("검수 완료"):
            continue
        out.append(dict(r, status=src["status"]))
    return out


def pick(cands, n, stratum, seed):
    rnd = random.Random(seed)
    picked = []
    for kind in ("dialogue", "narration"):
        pool = [c for c in cands if c["kind"] == kind]
        by = collections.defaultdict(list)
        for c in pool:
            by[stratum(c)].append(c)
        quota = largest_remainder({s: len(v) for s, v in by.items()}, min(n // 2, len(pool)))
        for s in sorted(by):
            picked += rnd.sample(by[s], min(quota[s], len(by[s])))
    return picked


def write_set(d, picked, full_by_title):
    os.makedirs(d, exist_ok=True)
    need = {}
    for p in picked:
        rows, order = full_by_title[p["title"]]
        i = order[p["id"]]
        need[(p["title"], i)] = 1
        for k in range(max(0, i - 3), i):
            if scene_of(rows[k]["id"]) == scene_of(p["id"]):
                need.setdefault((p["title"], k), 0)
    with open(os.path.join(d, "lines.tsv"), "w", encoding="utf-8") as w:
        w.write("id\tdoc_id\tja\tref_ko\tscored\tmeta\n")
        for (title, i) in sorted(need):
            r = full_by_title[title][0][i]
            ref = one(r["ko"]) if need[(title, i)] and not any(u in r["status"] for u in UNREVIEWED) else ""
            meta = json.dumps({"speaker": r["speaker"], "kind": r["kind"]}, ensure_ascii=False)
            w.write(f"{title}/{r['id']}\t{title}/{scene_of(r['id'])}\t{one(r['ja'])}\t{ref}\t{need[(title, i)]}\t"
                    f"{meta}\n")


def build(a):
    with open(CONF, encoding="utf-8") as f:
        c = json.load(f)
    pairs = c["pairs"]
    pool_index = a.pool_index or os.path.join(V08, "wsB", "index")
    hold_index = a.holdout_index or os.path.join(c["holdout_dir"], "index")
    out = os.path.join(a.out or os.path.join(V08, "wsB", "levels"), a.level)
    readme = [f"# Level-gate sets for {a.level} (lines at or below {a.level})", ""]
    for name, titles, idx, n, holdout in (("pool", c["pool"], pool_index, a.n_pool, False),
                                          ("holdout", [c["holdout"]], hold_index, a.n_holdout, True)):
        full_by_title, rows_by_id = {}, {}
        for t in titles:
            tr = rd(os.path.join(pairs, "train", t + ".tsv"))
            full = rd(os.path.join(pairs, t + ".tsv")) if holdout else tr
            full_by_title[t] = (full, {r["id"]: i for i, r in enumerate(full)})
            for r in tr:
                rows_by_id[(t, r["id"])] = r
        cands = eligible(idx, a.level, rows_by_id, holdout)
        strat = (lambda x: scene_of(x["id"])) if holdout else (lambda x: x["title"])
        picked = pick(cands, n, strat, f"{a.level}:{name}:{a.seed}")
        write_set(os.path.join(out, name), picked, full_by_title)
        kinds = collections.Counter(p["kind"] for p in picked)
        by_lev = collections.Counter(p["level"] for p in picked)
        line = (f"- {name}: {len(picked)} lines ({kinds['dialogue']} dialogue, {kinds['narration']} narration) "
                f"of {len(cands)} eligible; by level {dict(sorted(by_lev.items()))}")
        if not holdout:
            line += f"; by title {dict(collections.Counter(p['title'] for p in picked))}"
        readme.append(line)
        print(line)
    readme += ["", f"Seed {a.seed}. Eligible = index level <= {a.level} with no unmapped key, dialogue or narration, "
               ">= 2 kana/kanji, at least one grammar family; hold-out lines reviewed only."]
    with open(os.path.join(out, "README.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(readme) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build"])
    ap.add_argument("--level", required=True, choices=LEVELS)
    ap.add_argument("--pool-index", default="")
    ap.add_argument("--holdout-index", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--n-pool", type=int, default=600)
    ap.add_argument("--n-holdout", type=int, default=300)
    ap.add_argument("--seed", type=int, default=20261031)
    build(ap.parse_args())


if __name__ == "__main__":
    main()
