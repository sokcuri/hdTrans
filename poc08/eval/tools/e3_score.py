#!/usr/bin/env python3
"""E3 scoring: does a lexicon audit fix the tail LEX faults without regressions? (dev data, not eval/)

  e3_score.py --base 'ENGINE ARGS' --cand 'ENGINE ARGS' [--faults lex-faults.tsv] [--fresh FRESHDIR]
              [--out RUN.json] [--jobs 8]
  e3_score.py --self-test

Both engines translate line by line (--each is added): the fault lines (e3_faults.py) and the scored
fresh dev lines (e3_fresh.py).
- Fault fixes: a fault whose line did not change is not fixed. A changed line goes to a blind judge
  (claude-sonnet-5-5) that sees the source, the context, the fault (Japanese span, the old Korean, the
  reviewer's suggestion as a hint) and the old and new output, and answers fixed, not-fixed or worse.
  Fix rate by stratum (head <500, tail-a 500-2,000, tail-b 2,001-20,000, rare) with a bootstrap CI;
  the E3 number is the tail (tail-a + tail-b).
- Regressions: every changed line (fresh lines and fault lines) is judged pairwise old vs new by two
  judges (claude-sonnet-5-5 and claude-opus-5-5, order by hash). A regression is a line judge 1 calls
  worse; lines worse by both are listed.
- Kill (PIVOT E3): tail fix rate < 30%, or more than 1 regression per 5 fixes.
Judgments are cached in eval-runs/cache/e3.<model>.jsonl.
"""
import argparse
import collections
import csv
import hashlib
import json
import os
import random
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import api  # noqa: E402

csv.field_size_limit(1 << 30)
E3 = "/Users/sokcuri/hdwork/v08/eval-runs/e3"
CACHE = "/Users/sokcuri/hdwork/v08/eval-runs/cache"
J1, J2 = "claude-sonnet-5-5", "claude-opus-5-5"

FIX_RUBRIC = """You check whether a dictionary change fixed a known word-choice fault in a Japanese-to-Korean
translation. Each item gives the Japanese line (with context), the fault a reviewer found in the old
translation (the Japanese span, the old Korean for it, and the reviewer's suggestion, which is a hint:
other correct renderings count), and the old and new translations of the whole line.
Answer per item: "fixed" when the new translation renders the faulty span correctly (the fault is gone),
"not-fixed" when the fault is still there in some form, "worse" when the span is now rendered worse.
Judge only this span; other differences are judged elsewhere."""

PAIR_RUBRIC = """You compare two Korean translations A and B of the same Japanese line (context given when known),
from different systems in random order. Say which one a Korean reader should get: meaning first, then
natural fluent Korean, then style. Answer "same" when they are equally good or equally bad."""

FIX_SCHEMA = {"type": "object", "properties": {"items": {"type": "array", "items": {
    "type": "object", "properties": {"id": {"type": "integer"},
                                     "verdict": {"type": "string", "enum": ["fixed", "not-fixed", "worse"]}},
    "required": ["id", "verdict"], "additionalProperties": False}}}, "required": ["items"], "additionalProperties": False}
PAIR_SCHEMA = {"type": "object", "properties": {"items": {"type": "array", "items": {
    "type": "object", "properties": {"id": {"type": "integer"},
                                     "better": {"type": "string", "enum": ["A", "B", "same"]}},
    "required": ["id", "better"], "additionalProperties": False}}}, "required": ["items"], "additionalProperties": False}
TAIL = ("tail-a", "tail-b")


def h(s):
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


def read_tsv(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def translate(cmd, lines):
    args = shlex.split(cmd)
    if "--each" not in args:
        args.append("--each")
    p = subprocess.run(args, input=("\n".join(l.replace("\n", " ") for l in lines) + "\n").encode(),
                       capture_output=True, env=dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off"))
    out = p.stdout.decode("utf-8").split("\n")
    if out and out[-1] == "":
        out.pop()
    if p.returncode or len(out) != len(lines):
        sys.exit(f"{cmd}: rc {p.returncode}, {len(out)} lines for {len(lines)}: {p.stderr.decode()[-400:]}")
    return [o.strip() for o in out]


def judge(model, kind, items, jobs, size=12):
    """items: dicts with key and text; returns {key: answer}; cached."""
    cache = api.Cache(os.path.join(CACHE, f"e3.{model}.jsonl"))
    todo = [it for it in items if cache.get(it["key"]) is None]
    rubric, schema, field = (FIX_RUBRIC, FIX_SCHEMA, "verdict") if kind == "fix" else (PAIR_RUBRIC, PAIR_SCHEMA, "better")
    random.Random(kind + model).shuffle(todo)
    batches = [todo[i:i + size] for i in range(0, len(todo), size)]

    def one(b):
        prompt = rubric + "\n\n" + "\n\n".join(f"### item {n + 1}\n{it['text']}" for n, it in enumerate(b))

        def check(obj, size=len(b)):
            if sorted(x["id"] for x in obj["items"]) != list(range(1, size + 1)):
                raise ValueError("ids")
        obj = api.call(model, prompt, schema, effort="medium", validate=check)
        for x in obj["items"]:
            cache.put(b[x["id"] - 1]["key"], x[field])
    with ThreadPoolExecutor(jobs) as ex:
        list(ex.map(one, batches))
    return {it["key"]: cache.get(it["key"]) for it in items}


def pair_items(model, rows):
    """rows: (rid, ja, ctx, old, new) → items with A/B by hash; maps back to 'new' better/worse/same."""
    items, back = [], {}
    for rid, ja, ctx, old, new in rows:
        k = api.key("e3-pair-v1", model, ctx, ja, old, new)
        first_new = int(k, 16) % 2 == 0
        a, b = (new, old) if first_new else (old, new)
        text = (f"context: {ctx}\n" if ctx else "") + f"source: {ja}\nA: {a}\nB: {b}"
        items.append({"key": k, "text": text})
        back[k] = (rid, first_new)
    return items, back


def boot_rate(vals, reps=2000, seed=5):
    if not vals:
        return None
    rnd = random.Random(seed)
    xs = sorted(100 * sum(rnd.choice(vals) for _ in vals) / len(vals) for _ in range(reps))
    return [xs[int(0.025 * reps)], xs[int(0.975 * reps)]]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True)
    ap.add_argument("--cand", required=True)
    ap.add_argument("--faults", default=os.path.join(E3, "lex-faults.tsv"))
    ap.add_argument("--fresh", default=os.path.join(E3, "fresh"))
    ap.add_argument("--out")
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()
    faults = read_tsv(a.faults)
    fresh = [r for r in read_tsv(os.path.join(a.fresh, "lines.tsv")) if r["scored"] == "1"]
    fl = list(dict.fromkeys(f["ja"] for f in faults))
    ctx_of = {f["ja"]: f["ctx"].replace(" ⏎ ", " / ") for f in faults}
    src = fl + [r["ja"] for r in fresh]
    old, new = translate(a.base, src), translate(a.cand, src)
    o, n = dict(zip(src, old)), dict(zip(src, new))
    # fault fixes
    fix_items, fmap = [], {}
    for f in faults:
        if o[f["ja"]] == n[f["ja"]]:
            continue
        text = (f"context: {ctx_of[f['ja']]}\nsource: {f['ja']}\nfault: {f['ja_span']} was rendered as "
                f"{f['ko_span']} (reviewer suggests: {f['fix']})\nold: {o[f['ja']]}\nnew: {n[f['ja']]}")
        k = api.key("e3-fix-v1", J1, text)
        fix_items.append({"key": k, "text": text})
        fmap[f["fault_id"]] = k
    fv = judge(J1, "fix", fix_items, a.jobs)
    verdict = {f["fault_id"]: (fv[fmap[f["fault_id"]]] if f["fault_id"] in fmap else "unchanged") for f in faults}
    strata = collections.OrderedDict((s, []) for s in ("head", "tail-a", "tail-b", "rare"))
    for f in faults:
        strata.setdefault(f["stratum"], []).append(verdict[f["fault_id"]])
    rep = {"faults": len(faults), "fresh_lines": len(fresh), "strata": {}, "regressions": {}}
    for s, vs in strata.items():
        bits = [v == "fixed" for v in vs]
        rep["strata"][s] = {"n": len(vs), "fixed": sum(bits), "fix_pct": 100 * sum(bits) / max(1, len(vs)),
                            "fix_ci": boot_rate(bits), "worse": sum(v == "worse" for v in vs),
                            "changed": sum(v != "unchanged" for v in vs)}
    tail = [verdict[f["fault_id"]] == "fixed" for f in faults if f["stratum"] in TAIL]
    fixes_all = sum(v == "fixed" for v in verdict.values())
    rep["tail"] = {"n": len(tail), "fixed": sum(tail), "fix_pct": 100 * sum(tail) / max(1, len(tail)),
                   "fix_ci": boot_rate(tail)}
    # regressions: pairwise old vs new on every changed line
    rows = []
    for ja in fl:
        if o[ja] != n[ja]:
            rows.append(("fault:" + h(ja)[:10], ja, ctx_of[ja], o[ja], n[ja]))
    for r in fresh:
        if o[r["ja"]] != n[r["ja"]]:
            rows.append(("fresh:" + r["id"], r["ja"], "", o[r["ja"]], n[r["ja"]]))
    res = {}
    for m in (J1, J2):
        items, back = pair_items(m, rows)
        got = judge(m, "pair", items, a.jobs)
        for k, v in got.items():
            rid, first_new = back[k]
            res.setdefault(rid, {})[m] = 0 if v == "same" else (1 if (v == "A") == first_new else -1)
    for part in ("fault", "fresh"):
        ids = [r[0] for r in rows if r[0].startswith(part)]
        w1 = [i for i in ids if res[i][J1] == -1]
        rep["regressions"][part] = {
            "changed": len(ids), "better_j1": sum(res[i][J1] == 1 for i in ids), "worse_j1": len(w1),
            "worse_both": sum(res[i][J1] == -1 and res[i][J2] == -1 for i in ids),
            "worse_both_lines": [[r[1], r[3], r[4]] for r in rows if r[0] in ids and res[r[0]][J1] == -1
                                 and res[r[0]][J2] == -1][:30]}
    regs = sum(d["worse_j1"] for d in rep["regressions"].values())
    rep["fixes"] = fixes_all
    rep["regressions_total"] = regs
    rep["regressions_per_5_fixes"] = None if not fixes_all else 5 * regs / fixes_all
    rep["kill"] = {"tail_fix_below_30": rep["tail"]["fix_pct"] < 30,
                   "over_1_regression_per_5_fixes": regs * 5 > fixes_all}
    rep["kill"]["fires"] = any(rep["kill"].values())
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
    print(render(rep))
    return 0


def render(rep):
    ci = lambda c: "" if not c else f" [{c[0]:.1f}, {c[1]:.1f}]"  # noqa: E731
    out = [f"## E3 lexicon audit: {rep['faults']} LEX faults, {rep['fresh_lines']} fresh dev lines\n",
           "| stratum | faults | changed | fixed | fix rate | worse |", "|---|---|---|---|---|---|"]
    for s, d in rep["strata"].items():
        out.append(f"| {s} | {d['n']} | {d['changed']} | {d['fixed']} | {d['fix_pct']:.1f}%{ci(d['fix_ci'])} | {d['worse']} |")
    t = rep["tail"]
    out.append(f"| **tail (500-20k)** | {t['n']} | | {t['fixed']} | **{t['fix_pct']:.1f}%{ci(t['fix_ci'])}** | |")
    out.append("\n| changed lines | n | better (judge 1) | worse (judge 1) | worse by both |\n|---|---|---|---|---|")
    for p, d in rep["regressions"].items():
        out.append(f"| {p} | {d['changed']} | {d['better_j1']} | {d['worse_j1']} | {d['worse_both']} |")
    r5 = rep["regressions_per_5_fixes"]
    out.append(f"\nfixes {rep['fixes']}, regressions {rep['regressions_total']} "
               f"({'–' if r5 is None else f'{r5:.2f}'} per 5 fixes); kill: tail fix < 30% "
               f"{rep['kill']['tail_fix_below_30']}, > 1 regression per 5 fixes "
               f"{rep['kill']['over_1_regression_per_5_fixes']} → {'FIRES' if rep['kill']['fires'] else 'holds'}")
    return "\n".join(out)


def self_test():
    assert boot_rate([True, False]) is not None and boot_rate([]) is None
    items, back = pair_items("m", [("x", "行く", "", "간다", "가")])
    k = items[0]["key"]
    assert ("A: 가" in items[0]["text"]) == back[k][1]
    print("e3_score self-test OK")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(main())
