#!/usr/bin/env python3
"""Hold-out manifest of the fan-disc title: the judged sample and the cast-data-off list.

  holdout.py build [--out DIR] [--n 600] [--seed S]

The corpus layout (pairs dir, hold-out title, its main title, the output dir and a hand-written list of cast
names) comes from the private config $HD08_VN_CONF. Reads PAIRS_DIR/train/<title>.tsv (eval/eval2/gc/flip overlaps already removed) and the full title file for
context, and writes:
  DIR/manifest.tsv   id scene kind status speaker  — the sample: N lines, half dialogue, half narration,
                     stratified by scene (largest remainder), fixed seed. Only reviewed lines (status
                     starting 검수 완료) are eligible; 보류 and 미검수 lines are excluded. Lines with fewer than
                     two kana/kanji are excluded (nothing to judge).
  DIR/set/lines.tsv  judge.py set: the sample lines (scored=1) with up to three previous lines of the same
                     scene (scored=0) as context; ref_ko is the reviewed Korean, shown only as an anchor.
  DIR/cast.tsv       surface kind holdout_lines main_lines: the shared cast. Speakers of the main title who
                     speak in the hold-out; person-name tokens (UniDic 人名, with a kanji, length >= 2) found
                     >= 3 times in both titles; and the hand-written names of the config's cast_extra file.
                     Cast-blind runs drop name rows and voices entries whose key is listed here.
  DIR/README.md      counts, seed and the cast-blind contract.
"""
import argparse
import collections
import csv
import json
import os
import random
import re
import subprocess
import sys
import tempfile

csv.field_size_limit(1 << 30)
V08 = "/Users/sokcuri/hdwork/v08"
CONF = os.environ.get("HD08_VN_CONF", os.path.join(V08, "wsB", "vn.json"))
KANJI = re.compile(r"[一-鿿]")
BIN = "/Users/sokcuri/hdwork/wt/poc08-integration/build/mac/poc08/hdtrans08-ir"
JA = re.compile(r"[ぁ-ヿ一-鿿]")


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def one(s):
    return " ".join(s.replace("\r", " ").split("\n")).strip()


def scene_of(rid):
    return rid.rsplit(":", 1)[0]


def person_names(lines):
    """Person-name tokens (UniDic 名詞-固有名詞-人名) per line, from the analyzer only."""
    with tempfile.TemporaryDirectory() as td:
        inp = os.path.join(td, "in.txt")
        open(inp, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        os.mkdir(os.path.join(td, "st"))
        subprocess.run([BIN, "--in", inp, "--each", "--threads", "12", "--stages", os.path.join(td, "st")],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
                       env=dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off"))
        out = []
        for row in open(os.path.join(td, "st", "ir.jsonl"), encoding="utf-8"):
            j = json.loads(row)
            if "line" in j:
                out.append({t["surf"] for t in j["toks"] if "人名" in t.get("ud", "")})
        return out


def largest_remainder(counts, n):
    tot = sum(counts.values())
    raw = {k: n * v / tot for k, v in counts.items()}
    got = {k: int(x) for k, x in raw.items()}
    rest = n - sum(got.values())
    for k in sorted(raw, key=lambda k: (-(raw[k] - got[k]), k))[:rest]:
        got[k] += 1
    return got


def build(a):
    os.makedirs(os.path.join(a.out, "set"), exist_ok=True)
    train = rd(os.path.join(a.pairs, "train", a.title + ".tsv"))
    full = rd(os.path.join(a.pairs, a.title + ".tsv"))
    elig = [r for r in train if r["status"].startswith("검수 완료") and r["kind"] in ("dialogue", "narration")
            and len(JA.findall(r["ja"])) >= 2]
    rnd = random.Random(a.seed)
    picked = []
    for kind in ("dialogue", "narration"):
        pool = [r for r in elig if r["kind"] == kind]
        by_scene = collections.defaultdict(list)
        for r in pool:
            by_scene[scene_of(r["id"])].append(r)
        quota = largest_remainder({s: len(v) for s, v in by_scene.items()}, a.n // 2)
        for s in sorted(by_scene):
            picked += rnd.sample(by_scene[s], min(quota[s], len(by_scene[s])))
    order = {r["id"]: i for i, r in enumerate(full)}
    picked.sort(key=lambda r: order[r["id"]])
    with open(os.path.join(a.out, "manifest.tsv"), "w", encoding="utf-8") as w:
        w.write("id\tscene\tkind\tstatus\tspeaker\n")
        for r in picked:
            w.write(f"{r['id']}\t{scene_of(r['id'])}\t{r['kind']}\t{r['status']}\t{r['speaker']}\n")
    # judge set: context rows (scored 0) + sample rows (scored 1), in document order
    need = {}
    for r in picked:
        i = order[r["id"]]
        need[i] = 1
        for k in range(max(0, i - 3), i):
            if scene_of(full[k]["id"]) == scene_of(r["id"]):
                need.setdefault(k, 0)
    with open(os.path.join(a.out, "set", "lines.tsv"), "w", encoding="utf-8") as w:
        w.write("id\tdoc_id\tja\tref_ko\tscored\tmeta\n")
        for i in sorted(need):
            r = full[i]
            ref = one(r["ko"]) if need[i] and r["status"].startswith("검수 완료") else ""
            meta = json.dumps({"speaker": r["speaker"], "kind": r["kind"]}, ensure_ascii=False)
            w.write(f"{r['id']}\t{scene_of(r['id'])}\t{one(r['ja'])}\t{ref}\t{need[i]}\t{meta}\n")
    # cast: shared speakers + person-name tokens in both titles
    main_full = rd(os.path.join(a.pairs, a.main + ".tsv"))
    hs = collections.Counter(r["speaker"] for r in full if r["speaker"])
    ms = collections.Counter(r["speaker"] for r in main_full if r["speaker"])
    hn = collections.Counter(x for s in person_names([one(r["ja"]) for r in full]) for x in s)
    mn = collections.Counter(x for s in person_names([one(r["ja"]) for r in main_full]) for x in s)
    cast = {}
    for s in hs:
        if s in ms:
            cast[s] = ("speaker", hs[s], ms[s])
    for s in hn:
        if s not in cast and len(s) >= 2 and KANJI.search(s) and hn[s] >= 3 and mn.get(s, 0) >= 3:
            cast[s] = ("name", hn[s], mn[s])
    if a.cast_extra and os.path.exists(a.cast_extra):
        htxt = [one(r["ja"]) for r in full]
        mtxt = [one(r["ja"]) for r in main_full]
        for r in rd(a.cast_extra):
            s = r["surface"]
            if s and s not in cast:
                cast[s] = (r.get("kind") or "name", sum(s in t for t in htxt), sum(s in t for t in mtxt))
    with open(os.path.join(a.out, "cast.tsv"), "w", encoding="utf-8") as w:
        w.write("surface\tkind\tholdout_lines\tmain_lines\n")
        for s, (k, x, y) in sorted(cast.items(), key=lambda kv: (kv[1][0], -kv[1][1])):
            w.write(f"{s}\t{k}\t{x}\t{y}\n")
    names = [s for s in cast]
    with_cast = sum(1 for r in picked if r["speaker"] in cast or any(s in r["ja"] for s in names))
    st = collections.Counter(r["status"] for r in picked)
    kinds = collections.Counter(r["kind"] for r in picked)
    with open(os.path.join(a.out, "README.md"), "w", encoding="utf-8") as w:
        w.write(f"""# Hold-out manifest ({a.title}; private, outside the repo)

Read only by the WS-B gate (poc08/eval/tools/gate.py) at gate time. Never used for authoring.

- Source: pairs/train/{a.title}.tsv ({len(train)} lines after the eval/eval2/gc/flip overlap removal);
  eligible = reviewed (검수 완료·유지/수정), dialogue or narration, >= 2 kana/kanji: {len(elig)} lines.
- Sample: {len(picked)} lines ({kinds['dialogue']} dialogue, {kinds['narration']} narration), stratified by scene
  ({len({scene_of(r['id']) for r in picked})} scenes), seed {a.seed}. Status: {dict(st)}.
- Judges see up to three previous source lines of the scene as context and the reviewed Korean as an anchor.
- Cast: {sum(1 for v in cast.values() if v[0] == 'speaker')} shared speakers and
  {sum(1 for v in cast.values() if v[0] == 'name')} person-name tokens found in both titles (cast.tsv).
  {with_cast} of the {len(picked)} sample lines have a shared speaker or a cast name.

## Cast-blind contract

The gate runs the hold-out twice: cast-aware (as configured) and cast-blind, with the environment variable
`HD08_CAST_OFF` set to the absolute path of cast.tsv. An engine that loads name rows or voices.tsv entries
authored from the main title must drop every row whose key (Japanese surface or speaker) is listed in column
`surface`. The cast-blind number is the grammar result. When both runs give identical output the gate reports
that the engine has no cast data (or does not honour the variable).
""")
    print(f"sample {len(picked)} ({dict(kinds)}), eligible {len(elig)}, cast {len(cast)}, lines with cast {with_cast}",
          file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build"])
    ap.add_argument("--out", default="")
    ap.add_argument("--pairs", default="")
    ap.add_argument("--title", default="")
    ap.add_argument("--main", default="")
    ap.add_argument("--cast-extra", default="")
    ap.add_argument("--n", type=int, default=600)
    ap.add_argument("--seed", type=int, default=20261031)
    a = ap.parse_args()
    with open(CONF, encoding="utf-8") as f:
        c = json.load(f)
    a.out = a.out or c["holdout_dir"]
    a.pairs = a.pairs or c["pairs"]
    a.title = a.title or c["holdout"]
    a.main = a.main or c["holdout_main"]
    a.cast_extra = a.cast_extra or c.get("cast_extra", "")
    build(a)


if __name__ == "__main__":
    main()
