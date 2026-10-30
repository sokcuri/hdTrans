#!/usr/bin/env python3
"""Blind judging harness for the frozen held-out sets (evaluation only).

  judge.py --set EVALDIR/SET --sys NAME=KO.txt [--sys ...] [--baselines A,B,...] [--out RUN.json]
           [--model1 claude-sonnet-5-5] [--model2 claude-opus-5-5] [--frac2 0.2] [--jobs 8]
           [--batch 12] [--no-human] [--dry-run] [--no-pairs]
  judge.py --self-test

KO.txt files hold one Korean line per scored line (aligned with SET/scored.ja.txt). When the set has
references (game), the reference joins as system `human` and is shown in every other packet as an
anchor. Every MQM batch also holds one item of the human anchor pool (ANCHORS: human translations
and human translations with an injected meaning flip), so each judge's calibration is measured.

What is judged:
- MQM-lite per (line, output): errors with category (accuracy, omission, addition, fluency, style,
  terminology) and severity (critical 25, major 5, minor 1), plus a 0-100 score.
- pairwise preference per (line, output X, output Y) for each compared pair: every system against
  each baseline, and the baselines among themselves. X/Y order is balanced by a hash.
Judge 1 sees every item; judge 2 (a different model tier) sees the lines whose id hash falls in
--frac2 (and all anchors). Judge 3 (--model3) sees the double-judged items where judges 1 and 2
disagree, and the report adds the 2-of-3 majority on the double-judged lines (--frac2 1 for a verdict
run gives the majority on every line). Items are cached by (judge, rubric, context, source, reference, output(s))
in CACHE, so identical outputs and re-runs cost nothing. Batches mix systems and are balanced by
source length. Context = up to 3 previous source rows of the document (and the speaker if known).

The report (stdout markdown, --out JSON) gives per system: error lines, critical/major lines,
critical/major/minor per 100 lines, MQM penalty per line with a bootstrap CI, mean score; chrF,
chrF on punctuation-stripped text and morpheme chrF (Kiwi, when the eval venv exists) against the
reference with bootstrap CIs; per pair: wins/ties/losses, win rate = wins/(wins+losses) with a
bootstrap CI, judge-2 agreement and lines worse by both judges, and the 2:1 policy verdict
(better >= 2x worse and no line worse by both judges); judge agreement (kappa on has-critical-or-major
and has-critical, pairwise sign agreement), position bias and anchor calibration per judge.
"""
import argparse
import collections
import csv
import hashlib
import json
import os
import random
import re
import subprocess
import sys
import unicodedata
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import api  # noqa: E402

csv.field_size_limit(1 << 30)
RUNS = "/Users/sokcuri/hdwork/v08/eval-runs"
CACHE = os.path.join(RUNS, "cache")
ANCHORS = os.path.join(RUNS, "anchors", "anchors.tsv")
VENV_PY = os.path.join(RUNS, "venv", "bin", "python")
VERSION = "mqm-v1"
W = {"critical": 25, "major": 5, "minor": 1}
CATS = ["accuracy", "omission", "addition", "fluency", "style", "terminology"]

MQM_RUBRIC = """You are a blind reviewer of Japanese-to-Korean translations. Each item has a Japanese source line,
sometimes the previous source lines as context, sometimes a human reference translation as an anchor
(it may itself be imperfect; other correct renderings are fine), and ONE Korean translation to judge.
Items come from different people and programs in random order; judge each on its own merits.

List the translation's errors (MQM-lite). Each error has a category, a severity and the short Korean
span (or "(missing)" for an omission).
Categories: accuracy (mistranslation, wrong sense, flipped negation/tense/voice/direction, wrong name
or number), omission, addition, fluency (grammar, broken or unnatural Korean, 번역투 that a Korean reader
stumbles on), style (speech level or register wrong for the line, awkward but correct), terminology
(name or term rendered wrongly or inconsistently with the context).
Severity:
- critical: the reader comes away believing something false that matters (opposite meaning, wrong
  actor or direction, wrong number), or the line is unusable;
- major: meaning distorted or unclear, or Korean broken enough that the reader must reread;
- minor: meaning intact but a visible flaw (stiff, unnatural, small spacing or particle issue).
Do not count legitimate alternatives (word order, synonyms, a freer but faithful rendering) as errors.
Accept any reasonable reading of an ambiguous line. Then give a 0-100 score (100 = what a skilled human
translator would publish). Return one result per item id."""

PAIR_RUBRIC = """You are a blind reviewer of Japanese-to-Korean translations. Each item has a Japanese source line,
sometimes the previous source lines as context, sometimes a human reference translation as an anchor
(it may itself be imperfect), and two Korean translations A and B from different people or programs,
in random order. Say which one a Korean reader should get: meaning first (a meaning error outweighs
any amount of polish), then natural, fluent Korean, then style and speech level fitting the line.
Answer "same" only when they are equally good or equally bad; length is not a quality.
Return one result per item id."""

MQM_SCHEMA = {
    "type": "object",
    "properties": {"items": {"type": "array", "items": {
        "type": "object",
        "properties": {
            "id": {"type": "integer"},
            "errors": {"type": "array", "items": {
                "type": "object",
                "properties": {"cat": {"type": "string", "enum": CATS},
                               "sev": {"type": "string", "enum": list(W)},
                               "span": {"type": "string"}},
                "required": ["cat", "sev", "span"], "additionalProperties": False}},
            "score": {"type": "integer"}},
        "required": ["id", "errors", "score"], "additionalProperties": False}}},
    "required": ["items"], "additionalProperties": False,
}
PAIR_SCHEMA = {
    "type": "object",
    "properties": {"items": {"type": "array", "items": {
        "type": "object",
        "properties": {"id": {"type": "integer"}, "better": {"type": "string", "enum": ["A", "B", "same"]}},
        "required": ["id", "better"], "additionalProperties": False}}},
    "required": ["items"], "additionalProperties": False,
}


def h(s):
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


def read_lines(path):
    with open(path, encoding="utf-8") as f:
        data = f.read()
    return (data[:-1] if data.endswith("\n") else data).split("\n")


def load_set(set_dir):
    with open(os.path.join(set_dir, "lines.tsv"), encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))
    lines = []
    for n, r in enumerate(rows):
        if r["scored"] != "1":
            continue
        ctx = []
        for p in rows[max(0, n - 3):n]:
            if p["doc_id"] == r["doc_id"] and p["ja"].strip():
                ctx.append(p["ja"].strip())
        meta = json.loads(r["meta"]) if r["meta"] else {}
        lines.append({"id": r["id"], "ja": r["ja"].strip(), "ref": r["ref_ko"].strip(), "ctx": ctx,
                      "speaker": meta.get("speaker", "")})
    return lines


def load_anchors(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


# ---------------------------------------------------------------- items

def item_text(n, it, kind):
    out = [f"### item {n}"]
    if it.get("ctx"):
        out.append("context (previous source lines): " + " / ".join(it["ctx"]))
    if it.get("speaker"):
        out.append("speaker: " + it["speaker"])
    out.append("source: " + it["ja"])
    if it.get("anchor"):
        out.append("human reference (anchor): " + it["anchor"])
    if kind == "mqm":
        out.append("translation: " + it["ko"])
    else:
        out.append("A: " + it["a"])
        out.append("B: " + it["b"])
    return "\n".join(out)


def mqm_key(model, effort, it):
    return api.key(VERSION, "mqm", model, effort, " / ".join(it.get("ctx", [])), it.get("speaker", ""), it["ja"],
                   it.get("anchor", ""), it["ko"])


def pair_key(model, effort, it, x, y):
    lo, hi = sorted([x, y])
    return api.key(VERSION, "pair", model, effort, " / ".join(it.get("ctx", [])), it.get("speaker", ""), it["ja"],
                   it.get("anchor", ""), lo, hi)


def batches(items, size, seed):
    """Deal items into batches balanced by source length; shuffle inside each batch."""
    rnd = random.Random(seed)
    items = sorted(items, key=lambda x: (len(x["ja"]), x["key"]))
    nb = max(1, -(-len(items) // size))
    out = [[] for _ in range(nb)]
    for i, it in enumerate(items):
        out[i % nb].append(it)
    for b in out:
        rnd.shuffle(b)
    return [b for b in out if b]


def run_batches(kind, model, effort, todo, cache, anchors, jobs, size, log):
    if not todo:
        return
    rubric, schema = (MQM_RUBRIC, MQM_SCHEMA) if kind == "mqm" else (PAIR_RUBRIC, PAIR_SCHEMA)
    bs = batches(todo, size, f"{kind}:{model}")
    done = [0]

    def one(bi_b):
        bi, b = bi_b
        b = list(b)
        if kind == "mqm" and anchors:
            a = anchors[int(h(f"{model}:{bi}:{b[0]['key']}"), 16) % len(anchors)]
            ak = {"ja": a["ja"], "ko": a["ko"], "ctx": [], "anchor_id": a["id"]}
            ak["key"] = mqm_key(model, effort, ak)
            if cache.get(ak["key"]) is None:
                pos = int(h(ak["key"]), 16) % (len(b) + 1)
                b.insert(pos, ak)
        prompt = rubric + "\n\n" + "\n\n".join(item_text(n + 1, it, kind) for n, it in enumerate(b))

        def check(obj, size=len(b)):
            ids = sorted(x["id"] for x in obj["items"])
            if ids != list(range(1, size + 1)):
                raise ValueError(f"ids {ids[:6]} for {size} items")
        obj = api.call(model, prompt, schema, effort=effort, max_tokens=32000, validate=check)
        res = {x["id"]: x for x in obj["items"]}
        for n, it in enumerate(b, 1):
            r = res[n]
            if kind == "mqm":
                cache.put(it["key"], {"errors": [[e["cat"], e["sev"], e["span"]] for e in r["errors"]],
                                      "score": r["score"]})
            else:
                win = {"A": it["a"], "B": it["b"]}.get(r["better"])
                cache.put(it["key"], {"win": h(win) if win is not None else "same", "first": r["better"]})
        done[0] += 1
        if done[0] % 10 == 0 or done[0] == len(bs):
            log(f"  {kind} {model}: {done[0]}/{len(bs)} batches")

    with ThreadPoolExecutor(jobs) as ex:
        list(ex.map(one, enumerate(bs)))


# ---------------------------------------------------------------- metrics

def boot(values_fn, n_lines, reps=1000, seed=7):
    rnd = random.Random(seed)
    vals = []
    for _ in range(reps):
        idx = [rnd.randrange(n_lines) for _ in range(n_lines)]
        v = values_fn(idx)
        if v is not None:
            vals.append(v)
    if not vals:
        return None
    vals.sort()
    return [vals[int(0.025 * len(vals))], vals[min(len(vals) - 1, int(0.975 * len(vals)))]]


def chrf_stats(hyp, ref, nmax=6, tokens=False):
    if tokens:
        hyp, ref = hyp.split(), ref.split()
    else:
        hyp, ref = hyp.replace(" ", ""), ref.replace(" ", "")
    out = []
    for n in range(1, nmax + 1):
        hc = collections.Counter(tuple(hyp[i:i + n]) for i in range(len(hyp) - n + 1))
        rc = collections.Counter(tuple(ref[i:i + n]) for i in range(len(ref) - n + 1))
        out.append((sum((hc & rc).values()), sum(hc.values()), sum(rc.values())))
    return out


def chrf_total(stats_list, beta=2.0):
    nmax = len(stats_list[0]) if stats_list else 6
    tot = [[0, 0, 0] for _ in range(nmax)]
    for st in stats_list:
        for k, s in enumerate(st):
            for j in range(3):
                tot[k][j] += s[j]
    p = r = 0.0
    k = 0
    for m, hy, rf in tot:
        if hy and rf:
            p += m / hy
            r += m / rf
            k += 1
    if not k:
        return 0.0
    p, r = p / k, r / k
    return 0.0 if p + r == 0 else 100 * (1 + beta ** 2) * p * r / (beta ** 2 * p + r)


def strip_punct(s):
    s = "".join(" " if unicodedata.category(c)[0] in "PS" else c for c in s)
    return re.sub(r"\s+", " ", s).strip()


def morphs(lines):
    if not os.path.exists(VENV_PY):
        return None
    p = subprocess.run([VENV_PY, "-I", os.path.join(HERE, "morph.py")], input="\n".join(lines) + "\n",
                       capture_output=True, text=True)
    out = p.stdout.split("\n")[:len(lines)]
    return out if p.returncode == 0 and len(out) == len(lines) else None


def kappa(pairs):
    n = len(pairs)
    if not n:
        return None, None
    po = sum(a == b for a, b in pairs) / n
    p1 = sum(a for a, _ in pairs) / n
    p2 = sum(b for _, b in pairs) / n
    pe = p1 * p2 + (1 - p1) * (1 - p2)
    return po, (None if pe == 1 else (po - pe) / (1 - pe))


def summarize_mqm(v):
    sev = collections.Counter(e[1] for e in v["errors"])
    return {"crit": sev["critical"], "major": sev["major"], "minor": sev["minor"],
            "pen": sum(W[e[1]] for e in v["errors"]), "score": v["score"], "any": bool(v["errors"]),
            "cm": bool(sev["critical"] or sev["major"]),
            "cats": [e[0] for e in v["errors"] if e[1] in ("critical", "major")]}


def majority(a1, a2, a3):
    """Pairwise verdicts in {-1, 0, 1}: the value two of three judges give, else 0."""
    for v in (a1, a2, a3):
        if [a1, a2, a3].count(v) >= 2:
            return v
    return 0


def third(a, rep, lines, names, pairs, systems, mq, base_item, caches, anchors, m1, m2, log):
    """Judge 3 sees the double-judged items where judges 1 and 2 disagree (has critical/major for MQM,
    the verdict for pairs); report the 2-of-3 majority on the double-judged lines."""
    m3, e3 = a.model3, a.effort3
    caches[m3] = caches.get(m3) or api.Cache(os.path.join(CACHE, f"judge.{m3}.jsonl"))
    c3 = caches[m3]
    li_of = {L["id"]: li for li, L in enumerate(lines)}
    dis_m = [(s, li) for s in names for li in range(len(lines))
             if (0, s, li) in mq and (1, s, li) in mq and mq[(0, s, li)]["cm"] != mq[(1, s, li)]["cm"]]
    dis_p = []
    for x, y in pairs:
        d = rep["pairs"].get(f"{x} vs {y}", {})
        if m1 in d and m2 in d:
            for lid, r2 in d[m2]["lines"].items():
                if lid in d[m1]["lines"] and d[m1]["lines"][lid] != r2:
                    dis_p.append((x, y, li_of[lid]))
    todo_m, todo_p, seen = [], [], set()
    for s, li in dis_m:
        it = dict(base_item(li, [systems[s][li]]), ko=systems[s][li])
        it["key"] = mqm_key(m3, e3, it)
        if it["key"] not in seen and c3.get(it["key"]) is None:
            seen.add(it["key"])
            todo_m.append(it)
    for x, y, li in dis_p:
        ox, oy = systems[x][li], systems[y][li]
        it = base_item(li, [ox, oy])
        k = pair_key(m3, e3, it, ox, oy)
        if k not in seen and c3.get(k) is None:
            seen.add(k)
            first = int(k, 16) % 2 == 0
            todo_p.append(dict(it, key=k, a=ox if first else oy, b=oy if first else ox))
    log(f"{rep['set']} {m3} (third): {len(dis_m)} MQM and {len(dis_p)} pair disagreements, "
        f"{len(todo_m) + len(todo_p)} to judge")
    run_batches("mqm", m3, e3, todo_m, c3, anchors, a.jobs, a.batch, log)
    run_batches("pair", m3, e3, todo_p, c3, anchors, a.jobs, a.batch, log)
    out = {"model": m3, "effort": e3, "mqm": {}, "pairs": {}, "mqm_disagreements": len(dis_m),
           "pair_disagreements": len(dis_p)}
    sided = [0, 0]
    for s in names:
        rows = []
        for li in range(len(lines)):
            if (0, s, li) not in mq or (1, s, li) not in mq:
                continue
            v1, v2 = mq[(0, s, li)]["cm"], mq[(1, s, li)]["cm"]
            if v1 == v2:
                rows.append((v1, v1))
                continue
            it = dict(base_item(li, [systems[s][li]]), ko=systems[s][li])
            v = c3.get(mqm_key(m3, e3, it))
            if v is None:
                continue
            v3 = summarize_mqm(v)["cm"]
            sided[0] += v3 == v1
            sided[1] += 1
            rows.append((v3, v1))
        if rows:
            n = len(rows)
            out["mqm"][s] = {"n": n, "cm_majority_pct": 100 * sum(r[0] for r in rows) / n,
                             "cm_majority_ci": boot(lambda ix: 100 * sum(rows[i][0] for i in ix) / n, n),
                             "cm_judge1_pct": 100 * sum(r[1] for r in rows) / n}
    out["mqm_third_sides_with_judge1"] = None if not sided[1] else sided[0] / sided[1]
    sided = [0, 0]
    for x, y in pairs:
        pname = f"{x} vs {y}"
        d = rep["pairs"].get(pname, {})
        if m1 not in d or m2 not in d:
            continue
        vals = []
        for lid, r2 in d[m2]["lines"].items():
            if lid not in d[m1]["lines"]:
                continue
            r1 = d[m1]["lines"][lid]
            if r1 == r2:
                vals.append(r1)
                continue
            li = li_of[lid]
            ox, oy = systems[x][li], systems[y][li]
            v = c3.get(pair_key(m3, e3, base_item(li, [ox, oy]), ox, oy))
            if v is None:
                continue
            r3 = 0 if v["win"] == "same" else (1 if v["win"] == h(ox) else -1)
            sided[0] += r3 == r1
            sided[1] += 1
            vals.append(majority(r1, r2, r3))
        w, l_ = sum(v == 1 for v in vals), sum(v == -1 for v in vals)

        def wr(ix, vals=vals):
            ww = sum(vals[i] == 1 for i in ix)
            ll = sum(vals[i] == -1 for i in ix)
            return None if ww + ll == 0 else 100 * ww / (ww + ll)
        out["pairs"][pname] = {"n": len(vals), "win": w, "tie": len(vals) - w - l_, "loss": l_,
                               "win_rate": None if w + l_ == 0 else 100 * w / (w + l_),
                               "win_rate_ci": boot(wr, len(vals)), "worse_by_majority": l_}
        if "policy_2to1" in rep["pairs"][pname]:
            rep["pairs"][pname]["policy_2to1"]["worse_by_majority_on_double_judged"] = l_
    out["pair_third_sides_with_judge1"] = None if not sided[1] else sided[0] / sided[1]
    rep["third"] = out


# ---------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--set", required=True)
    ap.add_argument("--sys", action="append", default=[], help="NAME=KO.txt")
    ap.add_argument("--baselines", default="", help="comma list; default: every system given")
    ap.add_argument("--out")
    ap.add_argument("--model1", default="claude-sonnet-5-5")
    ap.add_argument("--model2", default="claude-opus-5-5")
    ap.add_argument("--effort1", default="medium")
    ap.add_argument("--effort2", default="low")
    ap.add_argument("--frac2", type=float, default=0.2)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--batch", type=int, default=12)
    ap.add_argument("--no-human", action="store_true")
    ap.add_argument("--no-pairs", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--anchors", default=ANCHORS)
    ap.add_argument("--model3", default="claude-opus-4-8",
                    help="third judge on the double-judged items where judges 1 and 2 disagree; '' = none")
    ap.add_argument("--effort3", default="medium")
    a = ap.parse_args(argv)
    log = lambda m: print(m, file=sys.stderr, flush=True)  # noqa: E731

    set_name = os.path.basename(os.path.normpath(a.set))
    lines = load_set(a.set)
    systems = collections.OrderedDict()
    for spec in a.sys:
        name, path = spec.split("=", 1)
        out = read_lines(path)
        if len(out) != len(lines):
            sys.exit(f"{path}: {len(out)} lines for {len(lines)} scored lines")
        systems[name] = [o.strip() for o in out]
    has_ref = all(l["ref"] for l in lines)
    if has_ref and not a.no_human:
        systems["human"] = [l["ref"] for l in lines]
    base = [b for b in a.baselines.split(",") if b] or list(systems)
    if has_ref and not a.no_human and "human" not in base:
        base.append("human")
    names = list(systems)
    pairs = []
    for x in names:
        for y in base:
            if x != y and (x, y) not in pairs and (y, x) not in pairs and not (x not in base and y not in base):
                pairs.append((x, y))
    anchors = load_anchors(a.anchors)
    judges = [(a.model1, a.effort1, 1.0), (a.model2, a.effort2, a.frac2)]
    caches = {m: api.Cache(os.path.join(CACHE, f"judge.{m}.jsonl")) for m, _, _ in judges}

    def picked(li, frac):
        return frac >= 1 or int(h(f"{set_name}:{lines[li]['id']}"), 16) % 1000 < frac * 1000

    def base_item(li, out_texts):
        L = lines[li]
        it = {"ja": L["ja"], "ctx": L["ctx"], "speaker": L["speaker"]}
        if has_ref and L["ref"] not in out_texts:
            it["anchor"] = L["ref"]
        return it

    # build the work
    plan = {}
    for m, eff, frac in judges:
        mq, pq, seen = [], [], set()
        for li in range(len(lines)):
            if not picked(li, frac):
                continue
            for s in names:
                it = dict(base_item(li, [systems[s][li]]), ko=systems[s][li])
                it["key"] = mqm_key(m, eff, it)
                if it["key"] not in seen and caches[m].get(it["key"]) is None:
                    seen.add(it["key"])
                    mq.append(it)
            if a.no_pairs:
                continue
            for x, y in pairs:
                ox, oy = systems[x][li], systems[y][li]
                if ox == oy:
                    continue
                it = base_item(li, [ox, oy])
                k = pair_key(m, eff, it, ox, oy)
                if k in seen or caches[m].get(k) is not None:
                    continue
                seen.add(k)
                first = int(k, 16) % 2 == 0
                pq.append(dict(it, key=k, a=ox if first else oy, b=oy if first else ox))
        plan[m] = (eff, mq, pq)
        log(f"{set_name} {m}: {len(mq)} MQM items and {len(pq)} pairs to judge (rest cached)")
    if a.dry_run:
        return 0
    for m, (eff, mq, pq) in plan.items():
        run_batches("mqm", m, eff, mq, caches[m], anchors, a.jobs, a.batch, log)
        run_batches("pair", m, eff, pq, caches[m], anchors, a.jobs, a.batch, log)

    # ------------------------------------------------ collect
    rep = {"set": set_name, "lines": len(lines), "systems": names, "baselines": base,
           "judges": [{"model": m, "effort": e, "frac": f} for m, e, f in judges], "mqm": {}, "pairs": {},
           "chrf": {}, "agreement": {}, "anchors": {}, "position": {}}
    mq = {}  # (judge, sys, li) -> summary
    for ji, (m, eff, frac) in enumerate(judges):
        for li in range(len(lines)):
            if not picked(li, frac):
                continue
            for s in names:
                it = dict(base_item(li, [systems[s][li]]), ko=systems[s][li])
                v = caches[m].get(mqm_key(m, eff, it))
                if v is not None:
                    mq[(ji, s, li)] = summarize_mqm(v)
    for ji, (m, eff, frac) in enumerate(judges):
        per = {}
        for s in names:
            idx = [li for li in range(len(lines)) if (ji, s, li) in mq]
            if not idx:
                continue
            v = [mq[(ji, s, li)] for li in idx]
            n = len(v)
            cats = collections.Counter(c for x in v for c in x["cats"])
            per[s] = {
                "n": n,
                "error_lines_pct": 100 * sum(x["any"] for x in v) / n,
                "cm_lines_pct": 100 * sum(x["cm"] for x in v) / n,
                "cm_lines_ci": boot(lambda ix: 100 * sum(v[i]["cm"] for i in ix) / n, n),
                "crit_per100": 100 * sum(x["crit"] for x in v) / n,
                "crit_per100_ci": boot(lambda ix: 100 * sum(v[i]["crit"] for i in ix) / n, n),
                "major_per100": 100 * sum(x["major"] for x in v) / n,
                "minor_per100": 100 * sum(x["minor"] for x in v) / n,
                "pen": sum(x["pen"] for x in v) / n,
                "pen_ci": boot(lambda ix: sum(v[i]["pen"] for i in ix) / n, n),
                "score": sum(x["score"] for x in v) / n,
                "cm_by_cat": dict(cats),
                "lines": {lines[li]["id"]: [x["crit"], x["major"], x["minor"], x["pen"], x["score"]]
                          for li, x in zip(idx, v)},
            }
        rep["mqm"][m] = per
    # pairwise
    for ji, (m, eff, frac) in enumerate(judges):
        pos_first = [0, 0]
        for x, y in pairs:
            res = []
            for li in range(len(lines)):
                if not picked(li, frac):
                    continue
                ox, oy = systems[x][li], systems[y][li]
                if ox == oy:
                    res.append((li, 0))
                    continue
                it = base_item(li, [ox, oy])
                k = pair_key(m, eff, it, ox, oy)
                v = caches[m].get(k)
                if v is None:
                    continue
                if v["win"] != "same":
                    pos_first[0] += v["first"] == "A"
                    pos_first[1] += 1
                res.append((li, 0 if v["win"] == "same" else (1 if v["win"] == h(ox) else -1)))
            if not res:
                continue
            w = sum(r == 1 for _, r in res)
            l_ = sum(r == -1 for _, r in res)
            t = len(res) - w - l_
            vals = [r for _, r in res]

            def wr(ix, vals=vals):
                ww = sum(vals[i] == 1 for i in ix)
                ll = sum(vals[i] == -1 for i in ix)
                return None if ww + ll == 0 else 100 * ww / (ww + ll)
            rep["pairs"].setdefault(f"{x} vs {y}", {})[m] = {
                "n": len(res), "win": w, "tie": t, "loss": l_,
                "win_rate": None if w + l_ == 0 else 100 * w / (w + l_),
                "win_rate_ci": boot(wr, len(vals)), "tie_pct": 100 * t / len(res),
                "lines": {lines[li]["id"]: r for li, r in res}}
        rep["position"][m] = {"decisive": pos_first[1],
                              "first_shown_wins_pct": None if not pos_first[1] else 100 * pos_first[0] / pos_first[1]}
    # agreement between judges and the 2:1 policy
    m1, m2 = judges[0][0], judges[1][0]
    cm_pairs, cr_pairs = [], []
    for s in names:
        for li in range(len(lines)):
            if (0, s, li) in mq and (1, s, li) in mq:
                cm_pairs.append((mq[(0, s, li)]["cm"], mq[(1, s, li)]["cm"]))
                cr_pairs.append((mq[(0, s, li)]["crit"] > 0, mq[(1, s, li)]["crit"] > 0))
    po, k_cm = kappa(cm_pairs)
    po2, k_cr = kappa(cr_pairs)
    rep["agreement"] = {"n_outputs": len(cm_pairs), "cm_agree": po, "cm_kappa": k_cm, "crit_agree": po2,
                        "crit_kappa": k_cr, "pairs": {}}
    for pname, d in rep["pairs"].items():
        if m1 in d and m2 in d:
            a1, a2 = d[m1]["lines"], d[m2]["lines"]
            both = [(a1[i], a2[i]) for i in a2 if i in a1]
            dec = [(p, q) for p, q in both if p and q]
            rep["agreement"]["pairs"][pname] = {
                "n": len(both), "exact": sum(p == q for p, q in both) / max(1, len(both)),
                "sign_where_both_decisive": None if not dec else sum(p == q for p, q in dec) / len(dec),
                "n_decisive": len(dec), "x_worse_by_both": sum(p == -1 and q == -1 for p, q in both),
                "x_better_by_both": sum(p == 1 and q == 1 for p, q in both)}
            j1 = d[m1]
            rep["pairs"][pname]["policy_2to1"] = {
                "better": j1["win"], "worse": j1["loss"],
                "worse_by_both": rep["agreement"]["pairs"][pname]["x_worse_by_both"],
                "pass": j1["win"] >= 2 * j1["loss"] and rep["agreement"]["pairs"][pname]["x_worse_by_both"] == 0}
    if a.model3 and not a.dry_run:
        third(a, rep, lines, names, pairs, systems, mq, base_item, caches, anchors, m1, m2, log)
    # anchors
    for m, eff, _ in judges:
        fl = [0, 0]
        hu = [0, 0]
        for an in anchors:
            it = {"ja": an["ja"], "ko": an["ko"], "ctx": []}
            v = caches[m].get(mqm_key(m, eff, it))
            if v is None:
                continue
            s = summarize_mqm(v)
            if an["kind"] == "flip":
                fl[0] += s["cm"]
                fl[1] += 1
            else:
                hu[0] += s["cm"]
                hu[1] += 1
        rep["anchors"][m] = {"flips_caught": fl[0], "flips": fl[1], "human_flagged": hu[0], "human": hu[1]}
    # chrF
    if has_ref:
        refs = [l["ref"] for l in lines]
        mr = morphs(refs)
        for s in names:
            if s == "human":
                continue
            hyp = systems[s]
            st = [chrf_stats(x, r) for x, r in zip(hyp, refs)]
            sn = [chrf_stats(strip_punct(x), strip_punct(r)) for x, r in zip(hyp, refs)]
            d = {"chrf": chrf_total(st), "chrf_ci": boot(lambda ix: chrf_total([st[i] for i in ix]), len(st), 300),
                 "chrf_nopunct": chrf_total(sn),
                 "chrf_nopunct_ci": boot(lambda ix: chrf_total([sn[i] for i in ix]), len(sn), 300)}
            mh = morphs(hyp) if mr else None
            if mh:
                sm = [chrf_stats(x, r, 4, tokens=True) for x, r in zip(mh, mr)]
                d["morph_chrf"] = chrf_total(sm)
                d["morph_chrf_ci"] = boot(lambda ix: chrf_total([sm[i] for i in ix]), len(sm), 300)
            rep["chrf"][s] = d
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
    print(render(rep))
    return 0


def fmt_ci(ci, digits=1):
    return "" if not ci else f" [{ci[0]:.{digits}f}, {ci[1]:.{digits}f}]"


def render(rep):
    out = [f"## {rep['set']} ({rep['lines']} lines)"]
    for m, per in rep["mqm"].items():
        out.append(f"\nMQM-lite, judge {m}:\n")
        out.append("| system | n | error lines | crit/major lines | critical /100 | major /100 | minor /100 | "
                   "penalty/line | score |")
        out.append("|---|---|---|---|---|---|---|---|---|")
        for s, d in per.items():
            out.append(f"| {s} | {d['n']} | {d['error_lines_pct']:.0f}% | {d['cm_lines_pct']:.1f}%"
                       f"{fmt_ci(d['cm_lines_ci'])} | {d['crit_per100']:.1f}{fmt_ci(d['crit_per100_ci'])} | "
                       f"{d['major_per100']:.1f} | {d['minor_per100']:.1f} | {d['pen']:.2f}{fmt_ci(d['pen_ci'], 2)} | "
                       f"{d['score']:.1f} |")
    if rep["pairs"]:
        out.append("\nPairwise (win rate = wins/(wins+losses), 95% bootstrap CI; ties apart):\n")
        out.append("| pair | judge | n | win | tie | loss | win rate | 2:1 policy |")
        out.append("|---|---|---|---|---|---|---|---|")
        for p, d in rep["pairs"].items():
            for m, v in d.items():
                if m == "policy_2to1":
                    continue
                pol = d.get("policy_2to1")
                pol_s = ("pass" if pol["pass"] else "fail") + f" ({pol['better']}:{pol['worse']}, both-worse " \
                        f"{pol['worse_by_both']})" if pol and m == rep["judges"][0]["model"] else ""
                wr = "–" if v["win_rate"] is None else f"{v['win_rate']:.1f}%{fmt_ci(v['win_rate_ci'])}"
                out.append(f"| {p} | {m} | {v['n']} | {v['win']} | {v['tie']} | {v['loss']} | {wr} | {pol_s} |")
    if rep["chrf"]:
        out.append("\nchrF against the human reference (95% bootstrap CI):\n")
        out.append("| system | chrF | chrF, punctuation stripped | morpheme chrF (Kiwi, 1-4) |")
        out.append("|---|---|---|---|")
        for s, d in rep["chrf"].items():
            mc = f"{d['morph_chrf']:.1f}{fmt_ci(d.get('morph_chrf_ci'))}" if "morph_chrf" in d else "–"
            out.append(f"| {s} | {d['chrf']:.1f}{fmt_ci(d['chrf_ci'])} | "
                       f"{d['chrf_nopunct']:.1f}{fmt_ci(d['chrf_nopunct_ci'])} | {mc} |")
    ag = rep["agreement"]
    if ag.get("n_outputs"):
        out.append(f"\nJudge agreement on {ag['n_outputs']} outputs: has critical/major {ag['cm_agree']:.2f} "
                   f"(kappa {ag['cm_kappa'] if ag['cm_kappa'] is None else round(ag['cm_kappa'], 2)}), "
                   f"has critical {ag['crit_agree']:.2f} "
                   f"(kappa {ag['crit_kappa'] if ag['crit_kappa'] is None else round(ag['crit_kappa'], 2)})")
        for p, d in ag["pairs"].items():
            sg = "–" if d["sign_where_both_decisive"] is None else f"{d['sign_where_both_decisive']:.2f}"
            out.append(f"- {p}: exact {d['exact']:.2f}, sign where both decisive {sg} (n={d['n_decisive']}), "
                       f"worse by both {d['x_worse_by_both']}, better by both {d['x_better_by_both']}")
    t3 = rep.get("third")
    if t3:
        sw = lambda x: "–" if x is None else f"{100 * x:.0f}%"  # noqa: E731
        out.append(f"\nThird judge {t3['model']} on {t3['mqm_disagreements']} MQM and {t3['pair_disagreements']} "
                   f"pair disagreements (sides with judge 1: MQM {sw(t3['mqm_third_sides_with_judge1'])}, pairs "
                   f"{sw(t3['pair_third_sides_with_judge1'])}); 2-of-3 majority on the double-judged lines:\n")
        out.append("| system | n | crit/major lines, majority | judge 1 on the same lines |")
        out.append("|---|---|---|---|")
        for s, d in t3["mqm"].items():
            out.append(f"| {s} | {d['n']} | {d['cm_majority_pct']:.1f}%{fmt_ci(d['cm_majority_ci'])} | "
                       f"{d['cm_judge1_pct']:.1f}% |")
        out.append("\n| pair | n | win | tie | loss | win rate, majority |")
        out.append("|---|---|---|---|---|---|")
        for p, d in t3["pairs"].items():
            wr = "–" if d["win_rate"] is None else f"{d['win_rate']:.1f}%{fmt_ci(d['win_rate_ci'])}"
            out.append(f"| {p} | {d['n']} | {d['win']} | {d['tie']} | {d['loss']} | {wr} |")
    for m, d in rep["anchors"].items():
        pos = rep["position"].get(m, {})
        fs = "–" if pos.get("first_shown_wins_pct") is None else f"{pos['first_shown_wins_pct']:.0f}%"
        out.append(f"- judge {m}: anchors: injected flips caught {d['flips_caught']}/{d['flips']}, human "
                   f"translations flagged critical/major {d['human_flagged']}/{d['human']}; first-shown wins {fs} "
                   f"of {pos.get('decisive', 0)} decisive pairs")
    return "\n".join(out)


def self_test():
    assert strip_punct("「그래, 좋아!」") == "그래 좋아"
    assert chrf_total([chrf_stats("고양이", "고양이")]) == 100.0
    assert 0 < chrf_total([chrf_stats("고양이다", "고양이이다")]) < 100
    assert chrf_total([chrf_stats("가 나 다", "가 나 다", 4, tokens=True)]) == 100.0
    po, k = kappa([(True, True), (False, False), (True, False), (False, False)])
    assert po == 0.75 and abs(k - 0.5) < 1e-9
    b = batches([{"ja": "あ" * i, "key": str(i)} for i in range(25)], 10, "s")
    assert len(b) == 3 and sorted(len(x) for x in b) == [8, 8, 9]
    assert {len(x["ja"]) for x in b[0]} != {len(x["ja"]) for x in b[1]}
    assert majority(1, -1, 1) == 1 and majority(1, -1, 0) == 0 and majority(0, -1, -1) == -1
    v = summarize_mqm({"errors": [["accuracy", "critical", "x"], ["fluency", "minor", "y"]], "score": 40})
    assert v["pen"] == 26 and v["cm"] and v["cats"] == ["accuracy"]
    print("judge self-test OK")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(main())
