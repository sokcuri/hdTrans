#!/usr/bin/env python3
"""Document consistency metrics on a frozen held-out set (evaluation only).

  consistency.py terms  --set EVALDIR/SET --sys NAME=KO.txt [...] [--out RUN.json]
  consistency.py speech --set EVALDIR/SET --sys NAME=KO.txt [...] [--out RUN.json]
  consistency.py --self-test

terms: name and term consistency inside a document. Source-side annotation, made once per document and
shared by every system: an LLM lists the names (people, places, groups, works) and specific terms
(titles, invented or technical terms) that occur on at least two scored lines of the document, with
those lines. Per system an LLM then reads the Korean of those lines and gives the rendering of the term
on each line (empty when the term is not rendered). A term is consistent when all its non-empty
renderings are equal (spaces ignored). Reported: share of consistent terms [95% CI], share of
occurrences that match the term's majority rendering, share of occurrences dropped.

speech: speech-level consistency per (speaker, addressee, scene) (gate G14, POC-PLAN L7). Source-side
annotation, once per scene and shared: the LLM marks every scored dialogue line with its speaker and
addressee (the game corpus speaker is given to it). The Japanese level of a line (polite: です/ます
forms; plain otherwise) is computed by rule; within a group, lines whose Japanese level is not the
group's majority are switches and are excluded. The Korean level of an output line is read by Kiwi
from its rightmost final ending (EF) or polite 요: hapsyo (-습니다/-ㅂ니까/-십시오), haeyo (-요, -죠), plain;
lines without a readable final ending are skipped. Consistency = share of classified lines in groups of
≥2 that carry their group's majority Korean level. Also reported: the same on polite vs plain only, and
on game the agreement of the level with the human reference.
Annotations and renderings are cached in eval-runs/cache/consistency.jsonl.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import api  # noqa: E402
from judge import CACHE, VENV_PY, boot, read_lines  # noqa: E402
import csv  # noqa: E402

csv.field_size_limit(1 << 30)
MODEL = "claude-sonnet-5-5"
JA_POLITE = re.compile(r"(です|でした|でしょう|ます|ました|ません|ましょう|ございま|ください|下さい|いただけ|頂け|くださ)")


def load(set_dir):
    with open(os.path.join(set_dir, "lines.tsv"), encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))
    out = []
    for r in rows:
        if r["scored"] != "1":
            continue
        meta = json.loads(r["meta"]) if r["meta"] else {}
        out.append({"id": r["id"], "doc": r["doc_id"], "ja": r["ja"].strip(), "ref": r["ref_ko"].strip(),
                    "speaker": meta.get("speaker", ""), "scene": f"{r['doc_id']}:{meta.get('scene', meta.get('window', ''))}"})
    return out


def systems_of(a, lines):
    sy = collections.OrderedDict()
    for spec in a.sys:
        name, path = spec.split("=", 1)
        o = read_lines(path)
        if len(o) != len(lines):
            sys.exit(f"{path}: {len(o)} lines for {len(lines)}")
        sy[name] = [x.strip() for x in o]
    if all(L["ref"] for L in lines):
        sy["human"] = [L["ref"] for L in lines]
    return sy


def cached_call(cache, tag, prompt, schema, validate):
    k = api.key("consistency-v1", tag, MODEL, prompt)
    v = cache.get(k)
    if v is None:
        v = api.call(MODEL, prompt, schema, effort="medium", validate=validate)
        cache.put(k, v)
    return v


# ---------------------------------------------------------------- terms

TERM_SCHEMA = {"type": "object", "properties": {"terms": {"type": "array", "items": {
    "type": "object", "properties": {"term": {"type": "string"}, "kind": {"type": "string", "enum": ["name", "term"]},
                                     "lines": {"type": "array", "items": {"type": "integer"}}},
    "required": ["term", "kind", "lines"], "additionalProperties": False}}},
    "required": ["terms"], "additionalProperties": False}
REND_SCHEMA = {"type": "object", "properties": {"items": {"type": "array", "items": {
    "type": "object", "properties": {"t": {"type": "integer"}, "n": {"type": "integer"}, "ko": {"type": "string"}},
    "required": ["t", "n", "ko"], "additionalProperties": False}}},
    "required": ["items"], "additionalProperties": False}


def doc_terms(cache, doc_lines):
    body = "\n".join(f"{n + 1}\t{L['ja']}" for n, L in enumerate(doc_lines))
    prompt = ("Below are numbered Japanese lines from one document. List every proper name (person, place, group, "
              "work, product) and every specific term (title, rank, skill, invented or technical term; not ordinary "
              "words) that occurs on at least two different lines. Give the term as written in Japanese and the "
              "numbers of the lines it occurs on (a name and its short form, e.g. full name and given name, are one "
              "term only if written the same). Return an empty list if there are none.\n\n" + body)

    def check(obj):
        for t in obj["terms"]:
            if any(not 1 <= n <= len(doc_lines) for n in t["lines"]):
                raise ValueError("line out of range")
    got = cached_call(cache, "terms", prompt, TERM_SCHEMA, check)
    terms = []
    for t in got["terms"]:
        ns = sorted({n for n in t["lines"] if t["term"] in doc_lines[n - 1]["ja"]})
        if len(ns) >= 2:
            terms.append({"term": t["term"], "kind": t["kind"], "lines": ns})
    return terms


def renderings(cache, doc_lines, ko, terms):
    want = [(ti + 1, n) for ti, t in enumerate(terms) for n in t["lines"]]
    if not want:
        return {}
    used = sorted({n for _, n in want})
    body = "\n".join(f"{n}\tja: {doc_lines[n - 1]['ja']}\n\tko: {ko[n - 1]}" for n in used)
    tl = "\n".join(f"T{ti + 1}\t{t['term']}" for ti, t in enumerate(terms))
    asks = "\n".join(f"T{t} on line {n}" for t, n in want)
    prompt = ("For each requested (term, line) give the Korean word(s) the translation uses for that Japanese term "
              "on that line, copied exactly from the Korean line but without particles or endings (e.g. 「아마네는」 → "
              "아마네). If the term is left out or cannot be found in the Korean, give an empty string. Do not "
              "correct the translation.\n\nTerms:\n" + tl + "\n\nLines:\n" + body + "\n\nRequested:\n" + asks)

    def check(obj):
        got = {(x["t"], x["n"]) for x in obj["items"]}
        if not set(want) <= got:
            raise ValueError("missing items")
    got = cached_call(cache, "render", prompt, REND_SCHEMA, check)
    return {(x["t"], x["n"]): x["ko"].strip() for x in got["items"]}


def run_terms(a):
    lines = load(a.set)
    sy = systems_of(a, lines)
    cache = api.Cache(os.path.join(CACHE, "consistency.jsonl"))
    docs = collections.OrderedDict()
    for i, L in enumerate(lines):
        docs.setdefault(L["doc"], []).append(i)
    rep = {"set": os.path.basename(os.path.normpath(a.set)), "metric": "terms", "systems": {}, "terms": 0}
    per_sys = {s: [] for s in sy}
    for doc, idx in docs.items():
        dl = [lines[i] for i in idx]
        terms = doc_terms(cache, dl)
        rep["terms"] += len(terms)
        for s, out in sy.items():
            r = renderings(cache, dl, [out[i] for i in idx], terms)
            for ti, t in enumerate(terms):
                rs = [re.sub(r"\s+", "", r.get((ti + 1, n), "")) for n in t["lines"]]
                per_sys[s].append({"doc": doc, "term": t["term"], "kind": t["kind"], "r": rs})
    for s, items in per_sys.items():
        n = len(items)
        cons = [len({x for x in it["r"] if x}) <= 1 for it in items]
        occ = sum(len(it["r"]) for it in items)
        major = sum(collections.Counter(x for x in it["r"] if x).most_common(1)[0][1] if any(it["r"]) else 0
                    for it in items)
        drop = sum(1 for it in items for x in it["r"] if not x)
        rep["systems"][s] = {
            "terms": n, "consistent_pct": 100 * sum(cons) / max(1, n),
            "consistent_ci": boot(lambda ix: 100 * sum(cons[i] for i in ix) / n, n) if n else None,
            "names_consistent_pct": 100 * sum(c for c, it in zip(cons, items) if it["kind"] == "name") /
            max(1, sum(it["kind"] == "name" for it in items)),
            "occurrences": occ, "majority_pct": 100 * major / max(1, occ - drop), "dropped_pct": 100 * drop / max(1, occ),
            "inconsistent": [[it["term"], sorted({x for x in it["r"] if x})] for c, it in zip(cons, items) if not c][:40]}
    return rep


# ---------------------------------------------------------------- speech

SPK_SCHEMA = {"type": "object", "properties": {"items": {"type": "array", "items": {
    "type": "object", "properties": {"n": {"type": "integer"}, "dialogue": {"type": "boolean"},
                                     "speaker": {"type": "string"}, "addressee": {"type": "string"}},
    "required": ["n", "dialogue", "speaker", "addressee"], "additionalProperties": False}}},
    "required": ["items"], "additionalProperties": False}


def annotate_scene(cache, sl):
    body = "\n".join(f"{n + 1}\t" + (f"[{L['speaker']}] " if L["speaker"] else "") + L["ja"] for n, L in enumerate(sl))
    prompt = ("Below are numbered consecutive Japanese lines of one scene (a speaker name in [ ] is given where known). "
              "For every line say whether it is spoken dialogue (true) or narration/description (false). For "
              "dialogue give the speaker and the addressee as short names or labels used consistently within the "
              "scene (e.g. the character name, 'narrator', 'audience', 'unknown', 'several'); for narration give "
              "empty strings.\n\n" + body)

    def check(obj):
        if sorted(x["n"] for x in obj["items"]) != list(range(1, len(sl) + 1)):
            raise ValueError("numbering")
    got = cached_call(cache, "speakers", prompt, SPK_SCHEMA, check)
    return {x["n"]: x for x in got["items"]}


def ja_level(ja):
    return "polite" if JA_POLITE.search(ja) else "plain"


def ko_levels(texts):
    """Korean level of each line: hapsyo, haeyo, plain or '' (no readable final ending)."""
    if not os.path.exists(VENV_PY):
        sys.exit("needs the eval venv with kiwipiepy")
    code = r'''
import sys, json
from kiwipiepy import Kiwi
k = Kiwi()
lines = json.loads(sys.stdin.read())
out = []
for s in lines:
    toks = k.tokenize(s) if s.strip() else []
    lv = ""
    for j in range(len(toks) - 1, -1, -1):
        t = toks[j]
        final_ec = t.tag == "EC" and (j + 1 == len(toks) or toks[j + 1].tag in ("SF", "SP", "SE", "SS"))
        if t.tag == "EF" or final_ec:
            f = t.form
            if f.endswith(("요", "죠")):
                lv = "haeyo"
            elif ("니다" in f or f.endswith(("습니까", "\u11b8니까", "십시오", "읍시다", "\u11b8시다", "습디다"))
                  or f in ("시오", "오")):
                lv = "hapsyo"
            else:
                lv = "plain"
            break
        if t.form in ("요", "죠") and t.tag in ("JX", "EC"):
            lv = "haeyo"
            break
    out.append(lv)
print(json.dumps(out))
'''
    p = subprocess.run([VENV_PY, "-I", "-c", code], input=json.dumps(texts), capture_output=True, text=True)
    if p.returncode:
        sys.exit(p.stderr[-500:])
    return json.loads(p.stdout)


def speech_part(ko):
    """The spoken part of a Korean line: the text inside the outer quote brackets when present."""
    m = re.search(r"[「『“\"](.+?)[」』”\"]?\s*$", ko)
    return m.group(1) if m else ko


def consistency(groups):
    tot = ok = 0
    for g in groups.values():
        vals = [v for v in g if v]
        if len(vals) < 2:
            continue
        top = collections.Counter(vals).most_common(1)[0][1]
        tot += len(vals)
        ok += top
    return ok, tot


def run_speech(a):
    lines = load(a.set)
    sy = systems_of(a, lines)
    cache = api.Cache(os.path.join(CACHE, "consistency.jsonl"))
    scenes = collections.OrderedDict()
    for i, L in enumerate(lines):
        scenes.setdefault(L["scene"], []).append(i)
    ann = {}
    for sc, idx in scenes.items():
        got = annotate_scene(cache, [lines[i] for i in idx])
        for n, i in enumerate(idx, 1):
            ann[i] = got[n]
    dlg = [i for i in range(len(lines)) if ann[i]["dialogue"] and ann[i]["speaker"] not in ("", "narrator")]
    # group by (scene, speaker, addressee); drop Japanese switches (minority Japanese level in the group)
    gkey = {i: (lines[i]["scene"], ann[i]["speaker"], ann[i]["addressee"]) for i in dlg}
    jl = {i: ja_level(lines[i]["ja"]) for i in dlg}
    by = collections.defaultdict(list)
    for i in dlg:
        by[gkey[i]].append(i)
    keep = []
    for g, idx in by.items():
        top = collections.Counter(jl[i] for i in idx).most_common(1)[0][0]
        keep += [i for i in idx if jl[i] == top]
    rep = {"set": os.path.basename(os.path.normpath(a.set)), "metric": "speech", "dialogue_lines": len(dlg),
           "kept_lines": len(keep), "groups": sum(1 for idx in by.values() if len(idx) >= 2), "systems": {}}
    refl = ko_levels([speech_part(lines[i]["ref"]) for i in keep]) if all(L["ref"] for L in lines) else None
    for s, out in sy.items():
        lv = ko_levels([speech_part(out[i]) for i in keep])
        g3, g2 = collections.defaultdict(list), collections.defaultdict(list)
        for i, v in zip(keep, lv):
            g3[gkey[i]].append(v)
            g2[gkey[i]].append({"hapsyo": "polite", "haeyo": "polite", "plain": "plain"}.get(v, ""))
        ok3, t3 = consistency(g3)
        ok2, t2 = consistency(g2)
        gl = [g for g in g3.values() if len([v for v in g if v]) >= 2]

        def cons(ix, gl=gl):
            o = t = 0
            for i in ix:
                vals = [v for v in gl[i] if v]
                o += collections.Counter(vals).most_common(1)[0][1]
                t += len(vals)
            return None if not t else 100 * o / t
        d = {"classified": sum(1 for v in lv if v), "consistency_pct": 100 * ok3 / max(1, t3), "lines_in_groups": t3,
             "consistency_ci": boot(cons, len(gl)) if gl else None,
             "polite_plain_consistency_pct": 100 * ok2 / max(1, t2),
             "levels": dict(collections.Counter(v or "unread" for v in lv))}
        if refl is not None and s != "human":
            both = [(x, y) for x, y in zip(lv, refl) if x and y]
            d["ref_agreement_pct"] = 100 * sum(x == y for x, y in both) / max(1, len(both))
            d["ref_agreement_polite_plain_pct"] = 100 * sum(
                (x == "plain") == (y == "plain") for x, y in both) / max(1, len(both))
            d["ref_agreement_n"] = len(both)
        rep["systems"][s] = d
    return rep


def render(rep):
    out = [f"## {rep['set']} {rep['metric']}"]
    if rep["metric"] == "terms":
        out.append(f"\n{rep['terms']} repeated names/terms\n")
        out.append("| system | terms consistent | names consistent | occurrences = majority rendering | dropped |")
        out.append("|---|---|---|---|---|")
        for s, d in rep["systems"].items():
            c = d["consistent_ci"]
            out.append(f"| {s} | {d['consistent_pct']:.1f}%" + (f" [{c[0]:.1f}, {c[1]:.1f}]" if c else "") +
                       f" | {d['names_consistent_pct']:.1f}% | {d['majority_pct']:.1f}% | {d['dropped_pct']:.1f}% |")
    else:
        out.append(f"\n{rep['dialogue_lines']} dialogue lines, {rep['kept_lines']} kept after Japanese switches, "
                   f"{rep['groups']} (speaker, addressee, scene) groups of ≥2\n")
        out.append("| system | level consistency | polite/plain consistency | lines in groups | agreement with ref "
                   "(3 levels / polite-plain) | levels |")
        out.append("|---|---|---|---|---|---|")
        for s, d in rep["systems"].items():
            c = d["consistency_ci"]
            ra = (f"{d['ref_agreement_pct']:.1f}% / {d['ref_agreement_polite_plain_pct']:.1f}% (n={d['ref_agreement_n']})"
                  if "ref_agreement_pct" in d else "–")
            out.append(f"| {s} | {d['consistency_pct']:.1f}%" + (f" [{c[0]:.1f}, {c[1]:.1f}]" if c else "") +
                       f" | {d['polite_plain_consistency_pct']:.1f}% | {d['lines_in_groups']} | {ra} | {d['levels']} |")
    return "\n".join(out)


def self_test():
    assert ja_level("「行きます」") == "polite" and ja_level("「行くよ」") == "plain"
    assert speech_part("「그래, 가자」") == "그래, 가자"
    assert consistency({"a": ["plain", "plain", "haeyo"], "b": ["plain"]}) == (2, 3)
    if os.path.exists(VENV_PY):
        assert ko_levels(["가겠습니다.", "가요!", "가자.", "아마네", "뭐야, 얘.", "저요", "동아리니까.", "없으니까요.", "합니까?", "흥분된다니까"]) == ["hapsyo", "haeyo", "plain", "", "plain", "haeyo", "plain", "haeyo", "hapsyo", "plain"], \
            ko_levels(["가겠습니다.", "가요!", "가자.", "아마네"])
    print("consistency self-test OK")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("metric", choices=["terms", "speech"])
    ap.add_argument("--set", required=True)
    ap.add_argument("--sys", action="append", default=[])
    ap.add_argument("--out")
    a = ap.parse_args()
    rep = run_terms(a) if a.metric == "terms" else run_speech(a)
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
    print(render(rep))
    return 0


if __name__ == "__main__":
    sys.exit(main())
