#!/usr/bin/env python3
"""The one-command gate: pairwise on dev and on the hold-out title, the flip suite, and the verdict.

  gate.py --name NAME --cand 'CMD' [--flip-cand 'CMD'] [--base 'CMD'] [--slices game,fiction,captions,holdout]
          [--out DIR] [--sample N] [--jobs 8] [--dry-run] [--no-flip]
  gate.py --self-test

CMD is a shell-split command that reads UTF-8 lines on stdin and writes the same number of lines on stdout
(document mode, as `hdtrans` and `hdtrans08` run without --each). It runs with HOME set to a fresh temporary
dir and HDTRANS_CACHE_DIR=off. The flip suite runs `--flip-cand` (default: CMD + ' --each'), one item per line.
The baseline is 0.7.6 (--base, default below); its outputs are cached per (command, binary hash, input hash).

What runs:
1. Every slice is translated by base and candidate. Dev slices: game (gc 32001-36000, 4,000 lines with human
   references) and captions (4,000), frozen copies of the rule-dev2 slices; fiction = the web-novel dev chapters
   (two sources, one document per chapter, Japanese only; it contains every line of the older rule-dev2 fiction
   slice, still callable as `fiction-rd2`). Hold-outs: the VN sample (600 reviewed lines of the held-out title
   with three lines of scene context), translated twice, cast-aware (as given) and cast-blind (environment
   HD08_CAST_OFF = the manifest's cast.tsv; an engine with name rows or voices authored from the main title must
   drop the listed keys); and `fiction-held`, five held-out chapters of a web novel whose other chapters are dev.
   Fiction slices are judged with the house narration rules appended to the rubric (notes/narration.txt).
   `fiction-c2` (a further 45 chapters, overlap-checked) is available but not run by default.
2. Changed lines (output differs from base) are judged blind by judge.py: judge 1 (sonnet) and judge 2 (opus) on
   every changed line (MQM-lite for both outputs, pairwise for the pair), judge 3 on their disagreements.
   --sample N judges a fixed-seed sample of N changed lines per slice instead (reported as such).
3. The flip suite (poc08/eval/flip/suite.tsv) is run for base and candidate; an item lost = passes for base,
   fails for the candidate.

Verdict (DESIGN-v3 §4.3, PLAN-v3 §0):
- per slice, on the changed lines: wins W and losses L are the 2-of-3 majority (judges 1 and 2 when they agree,
  judge 3 otherwise; three different answers = tie). The slice passes when W >= 2 L, no line is worse by both
  judges 1 and 2, and kappa(has critical/major) between judges 1 and 2 is >= 0.6. With fewer than 10 decisive
  lines (W + L) the 2:1 test is reported but not enforced; with fewer than 40 judged outputs kappa is reported
  but not enforced. No changed line = n/a.
- dev pooled (game + fiction + captions): W >= 2 L on the summed majority counts.
- hold-out: the cast-blind run is binding (the grammar result); cast-aware is reported beside it. fiction-held
  is binding. Hold-out lines are never printed, only counts and ids.
- flip: no item lost; every cat and sub is reported.
- PASS = dev pooled passes, every slice run passes (cast-blind for the VN hold-out; cast-aware is reported
  only), flip loses nothing.
Exit code 0 = PASS, 1 = FAIL, 2 = error. Writes OUT/gate.md, OUT/gate.json, OUT/changed/<dev slice>.tsv
(id, ja, base, cand, judge 1, judge 2, majority) and the judge sets and reports under OUT/judge/.
"""
import argparse
import collections
import csv
import datetime
import hashlib
import json
import os
import random
import shlex
import subprocess
import sys
import tempfile

csv.field_size_limit(1 << 30)
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flip  # noqa: E402

V08 = "/Users/sokcuri/hdwork/v08"
CONF = os.environ.get("HD08_VN_CONF", os.path.join(V08, "wsB", "vn.json"))
DEV = os.path.join(V08, "wsB", "dev")
GATES = os.path.join(V08, "wsB", "gates")
LEVELS = os.path.join(V08, "wsB", "levels")
BASE = ("/Users/sokcuri/hdwork/wt/fix-hare/build/mac/hdtrans --dicts /Users/sokcuri/hdwork/wt/fix-hare/dicts")
DEV_SLICES = ["game", "fiction", "captions"]          # pooled for the dev verdict
FILE_SLICES = {"game": "game", "captions": "captions", "fiction-rd2": "fiction"}   # frozen copies in wsB/dev
FICTION = os.path.join(V08, "wsB", "fiction", "sets")
NARRATION_NOTE = os.path.join(HERE, "notes", "narration.txt")
MIN_DECISIVE, MIN_KAPPA_N, KAPPA = 10, 40, 0.6
KST = datetime.timezone(datetime.timedelta(hours=9))


def sha(path_or_bytes):
    h = hashlib.sha256()
    if isinstance(path_or_bytes, bytes):
        h.update(path_or_bytes)
    else:
        with open(path_or_bytes, "rb") as f:
            for b in iter(lambda: f.read(1 << 20), b""):
                h.update(b)
    return h.hexdigest()


def read_lines(path):
    with open(path, encoding="utf-8") as f:
        data = f.read()
    return (data[:-1] if data.endswith("\n") else data).split("\n")


def write_lines(path, lines):
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def translate(cmd, lines, extra_env=None):
    env = dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off")
    env.pop("HD08_CAST_OFF", None)
    env.update(extra_env or {})
    p = subprocess.run(shlex.split(cmd), input=("\n".join(lines) + "\n").encode("utf-8"), capture_output=True,
                       env=env)
    out = p.stdout.decode("utf-8").split("\n")
    if out and out[-1] == "":
        out.pop()
    if p.returncode != 0 or len(out) != len(lines):
        raise SystemExit(f"gate: `{cmd}` returned {len(out)} lines for {len(lines)} (rc {p.returncode}): "
                         f"{p.stderr.decode('utf-8', 'replace')[-600:]}")
    return [o.strip() for o in out]


def cmd_key(cmd):
    parts = shlex.split(cmd)
    b = parts[0] if parts else ""
    return sha((cmd + "\x1f" + (sha(b) if os.path.isfile(b) else "")).encode("utf-8"))[:16]


def cached_base(cmd, slice_name, lines, extra_env=None):
    d = os.path.join(GATES, ".base", cmd_key(cmd))
    os.makedirs(d, exist_ok=True)
    key = sha(("\n".join(lines) + json.dumps(extra_env or {}, sort_keys=True)).encode("utf-8"))[:16]
    p = os.path.join(d, f"{slice_name}.{key}.ko")
    if os.path.exists(p):
        out = read_lines(p)
        if len(out) == len(lines):
            return out
    out = translate(cmd, lines, extra_env)
    write_lines(p, out)
    with open(os.path.join(d, "CMD"), "w", encoding="utf-8") as f:
        f.write(cmd + "\n")
    return out


# ---------------------------------------------------------------- slices

def read_set(path):
    with open(path, encoding="utf-8", newline="") as f:
        rr = list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))
    rows = [{"id": r["id"], "doc": r["doc_id"], "ja": r["ja"], "ref": r["ref_ko"], "meta": r["meta"],
             "scored0": r["scored"] == "1"} for r in rr]
    return rows, [i for i, r in enumerate(rows) if r["scored0"]]


def load_slices(names, level=""):
    """Each slice: rows [{id, doc, ja, ref, meta}], scored indexes (the lines the gate compares)."""
    sl = {}
    with open(CONF, encoding="utf-8") as f:
        c = json.load(f)
    for s in names:
        if s in FILE_SLICES:
            ja = read_lines(os.path.join(DEV, f"{FILE_SLICES[s]}.ja"))
            ref_p = os.path.join(DEV, f"{FILE_SLICES[s]}.ref")
            ref = read_lines(ref_p) if os.path.exists(ref_p) else [""] * len(ja)
            rows = [{"id": f"{s}:{i + 1}", "doc": s, "ja": t, "ref": r, "meta": ""} for i, (t, r) in
                    enumerate(zip(ja, ref))]
            sl[s] = {"rows": rows, "scored": [i for i, r in enumerate(rows) if r["ja"].strip()], "holdout": False}
            if s == "fiction-rd2":
                sl[s]["note"] = NARRATION_NOTE
        elif s in ("fiction", "fiction-held", "fiction-c2"):
            rows, scored = read_set(os.path.join(FICTION, {"fiction": "dev", "fiction-held": "held",
                                                           "fiction-c2": "c2"}[s], "lines.tsv"))
            sl[s] = {"rows": rows, "scored": scored, "holdout": s == "fiction-held", "note": NARRATION_NOTE}
        elif s == "holdout":
            hd = c["holdout_dir"]
            p = os.path.join(LEVELS, level, "holdout", "lines.tsv") if level else os.path.join(hd, "set", "lines.tsv")
            rows, scored = read_set(p)
            sl[f"holdout-{level}" if level else s] = {"rows": rows, "scored": scored, "holdout": True,
                                                      "cast": os.path.join(hd, "cast.tsv")}
        elif s == "pool":
            if not level:
                raise SystemExit("gate: the pool slice needs --level")
            rows, scored = read_set(os.path.join(LEVELS, level, "pool", "lines.tsv"))
            sl[f"pool-{level}"] = {"rows": rows, "scored": scored, "holdout": False}
        else:
            raise SystemExit(f"gate: unknown slice {s}")
    return sl


def write_judge_set(d, rows, judged):
    """judge.py set: every row as context, scored = 1 on the judged lines."""
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "lines.tsv"), "w", encoding="utf-8") as f:
        f.write("id\tdoc_id\tja\tref_ko\tscored\tmeta\n")
        js = set(judged)
        for i, r in enumerate(rows):
            f.write(f"{r['id']}\t{r['doc']}\t{r['ja']}\t{r['ref'] if i in js else ''}\t{int(i in js)}\t"
                    f"{r['meta']}\n")


def run_judge(set_dir, systems, jobs, log_path, note="", rubric="v3"):
    """systems: [(name, [ko per judged line])]; the last one is the baseline."""
    args = [sys.executable, os.path.join(HERE, "judge.py"), "--set", set_dir, "--frac2", "1", "--no-human",
            "--jobs", str(jobs), "--baselines", systems[-1][0], "--out", os.path.join(set_dir, "report.json")]
    args += ["--rubric", rubric]
    if note:
        args += ["--style-note", note]
    for name, outs in systems:
        p = os.path.join(set_dir, f"{name}.ko")
        write_lines(p, outs)
        args += ["--sys", f"{name}={p}"]
    with open(log_path, "w", encoding="utf-8") as lg, open(os.path.join(set_dir, "report.md"), "w") as md:
        r = subprocess.run(args, stdout=md, stderr=lg)
    if r.returncode != 0:
        raise SystemExit(f"gate: judge.py failed (rc {r.returncode}); see {log_path}")
    with open(os.path.join(set_dir, "report.json"), encoding="utf-8") as f:
        return json.load(f)


def pair_stats(rep, x, y):
    """Judge 1, judge 2 and majority counts for pair 'x vs y'; per-line verdicts; kappa."""
    pname = f"{x} vs {y}"
    m1, m2 = rep["judges"][0]["model"], rep["judges"][1]["model"]
    d = rep["pairs"].get(pname, {})
    l1 = d.get(m1, {}).get("lines", {})
    l2 = d.get(m2, {}).get("lines", {})
    third = rep.get("third", {}).get("pairs", {}).get(pname, {})
    maj = {"win": third.get("win", 0), "tie": third.get("tie", 0), "loss": third.get("loss", 0)}
    wbb = sorted(i for i in l2 if l1.get(i) == -1 and l2[i] == -1)
    bbb = sorted(i for i in l2 if l1.get(i) == 1 and l2[i] == 1)
    return {"j1": {k: d.get(m1, {}).get(k, 0) for k in ("win", "tie", "loss")},
            "j2": {k: d.get(m2, {}).get(k, 0) for k in ("win", "tie", "loss")},
            "majority": maj, "worse_by_both": wbb, "better_by_both": bbb,
            "kappa": rep.get("agreement", {}).get("cm_kappa"), "kappa_n": rep.get("agreement", {}).get("n_outputs", 0),
            "lines_j1": l1, "lines_j2": l2,
            "cm_pct": {s: rep["mqm"].get(m1, {}).get(s, {}).get("cm_lines_pct") for s in (x, y)}}


def slice_verdict(st, n_changed):
    if n_changed == 0:
        return {"verdict": "n/a", "why": "no changed line"}
    w, l_ = st["majority"]["win"], st["majority"]["loss"]
    why, ok = [], True
    if w + l_ >= MIN_DECISIVE:
        if w < 2 * l_:
            ok = False
            why.append(f"2:1 fails ({w}:{l_})")
    else:
        why.append(f"2:1 not enforced ({w}:{l_}, < {MIN_DECISIVE} decisive)")
    if st["worse_by_both"]:
        ok = False
        why.append(f"{len(st['worse_by_both'])} worse by both")
    k = st["kappa"]
    if st["kappa_n"] >= MIN_KAPPA_N:
        if k is None or k < KAPPA:
            ok = False
            why.append(f"kappa {k if k is None else round(k, 2)} < {KAPPA}")
    else:
        why.append(f"kappa not enforced (n {st['kappa_n']})")
    return {"verdict": "PASS" if ok else "FAIL", "why": "; ".join(why) or "ok"}


# ---------------------------------------------------------------- flip

def run_flip(base_cmd, cand_cmd):
    items = flip.load(flip.DEFAULT)
    lines = [i["ja"] for i in items]
    b = cached_base(base_cmd, "flip", lines)
    c = translate(cand_cmd, lines)
    res = {"n": len(items), "base": 0, "cand": 0, "lost": [], "gained": [], "by_cat": {}, "by_sub": {}}
    cat = collections.defaultdict(lambda: [0, 0, 0])
    sub = collections.defaultdict(lambda: [0, 0, 0])
    for it, ob, oc in zip(items, b, c):
        pb, pc = flip.verdict(it, ob)[0], flip.verdict(it, oc)[0]
        res["base"] += pb
        res["cand"] += pc
        if pb and not pc:
            res["lost"].append({"id": it["id"], "sub": it["sub"], "ja": it["ja"], "base": ob, "cand": oc})
        if pc and not pb:
            res["gained"].append(it["id"])
        for d, k in ((cat, it["cat"]), (sub, it["sub"])):
            d[k][0] += pb
            d[k][1] += pc
            d[k][2] += 1
    res["by_cat"], res["by_sub"] = dict(cat), dict(sub)
    return res


# ---------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="short change name, e.g. wsA-n5-case")
    ap.add_argument("--cand", required=True)
    ap.add_argument("--flip-cand", default="")
    ap.add_argument("--base", default=BASE)
    ap.add_argument("--slices", default="", help="default game,fiction,captions,holdout,fiction-held; with --level "
                    "pool,holdout,game,fiction,captions,fiction-held; also fiction-rd2, fiction-c2")
    ap.add_argument("--level", default="", help="level gate (PLAN-v3 §1.3): the pool and hold-out slices hold "
                    "lines at or below this level (wsB/levels/<LEVEL>)")
    ap.add_argument("--out", default="")
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--dry-run", action="store_true", help="translate, count changed lines and run flip; no judging")
    ap.add_argument("--no-flip", action="store_true")
    ap.add_argument("--rubric", default="v3", choices=["v1", "v2", "v3"], help="judge rubric (judge.py): v3 = natural "
                    "Korean within faithfulness bounds (default); v2 = literal target; v1 = the frozen eval runs")
    a = ap.parse_args(argv)
    stamp = datetime.datetime.now(KST).strftime("%Y%m%d-%H%M")
    out = a.out or os.path.join(GATES, f"{a.name}-{stamp}")
    os.makedirs(os.path.join(out, "changed"), exist_ok=True)
    log = lambda m: print(m, file=sys.stderr, flush=True)  # noqa: E731
    names = a.slices or ("pool,holdout,game,fiction,captions,fiction-held" if a.level
                         else "game,fiction,captions,holdout,fiction-held")
    slices = load_slices([s for s in names.split(",") if s], a.level)
    G = {"name": a.name, "time_kst": stamp, "cand": a.cand, "base": a.base, "slices": {}, "sample": a.sample,
         "level": a.level, "rubric": rubric_label(a.rubric), "rubrics": {},
         "holdout_leaks": leak_count()}
    pooled = {"win": 0, "loss": 0, "tie": 0}
    for sname, S in slices.items():
        rows, scored = S["rows"], S["scored"]
        src = [r["ja"] for r in rows]
        log(f"{sname}: translating {len(src)} lines")
        b_all = cached_base(a.base, sname, src)
        runs = {"cand": translate(a.cand, src)}
        cast = S["holdout"] and S.get("cast")
        if cast:
            runs = {"blind": translate(a.cand, src, {"HD08_CAST_OFF": S["cast"]}), "aware": runs["cand"]}
        changed = sorted({i for i in scored for o in runs.values() if o[i] != b_all[i]})
        info = {"lines": len(scored), "changed": len(changed)}
        if cast:
            info["blind_equals_aware"] = runs["blind"] == runs["aware"]
            info["changed_blind"] = sum(runs["blind"][i] != b_all[i] for i in scored)
            info["changed_aware"] = sum(runs["aware"][i] != b_all[i] for i in scored)
        judged = changed
        if a.sample and len(changed) > a.sample:
            judged = sorted(random.Random(f"{a.name}:{sname}").sample(changed, a.sample))
            info["judged_sample"] = len(judged)
        G["slices"][sname] = info
        log(f"{sname}: {len(changed)} of {len(scored)} lines changed")
        if a.dry_run or not judged:
            if not judged:
                info.update(slice_verdict({}, 0))
            continue
        sd = os.path.join(out, "judge", sname)
        write_judge_set(sd, rows, judged)
        systems = [(k, [runs[k][i] for i in judged]) for k in runs] + [("base", [b_all[i] for i in judged])]
        rep = run_judge(sd, systems, a.jobs, os.path.join(out, "judge", f"{sname}.log"), S.get("note", ""),
                        a.rubric)
        G["rubrics"][sname] = rep.get("rubric", "?")
        for k in runs:
            st = pair_stats(rep, k, "base")
            key = sname if k == "cand" else f"{sname}-{k}"
            n_ch = sum(runs[k][i] != b_all[i] for i in judged)
            v = slice_verdict(st, n_ch)
            G["slices"].setdefault(key, dict(info))
            G["slices"][key].update({kk: st[kk] for kk in ("j1", "j2", "majority", "worse_by_both", "better_by_both",
                                                            "kappa", "kappa_n", "cm_pct")}, **v)
            if not S["holdout"]:
                if sname in DEV_SLICES:
                    for kk in pooled:
                        pooled[kk] += st["majority"][kk]
                with open(os.path.join(out, "changed", f"{sname}.tsv"), "w", encoding="utf-8") as f:
                    f.write("id\tja\tbase\tcand\tj1\tj2\tmajority_hint\n")
                    for i in judged:
                        lid = rows[i]["id"]
                        j1, j2 = st["lines_j1"].get(lid, ""), st["lines_j2"].get(lid, "")
                        hint = j1 if j1 == j2 else "3rd"
                        f.write(f"{lid}\t{rows[i]['ja']}\t{b_all[i]}\t{runs[k][i]}\t{j1}\t{j2}\t{hint}\n")
        if cast:
            G["slices"].pop(sname, None)
    # pooled dev verdict
    dev_dec = pooled["win"] + pooled["loss"]
    G["dev_pooled"] = dict(pooled, verdict=("n/a" if dev_dec == 0 else
                                            "PASS" if pooled["win"] >= 2 * pooled["loss"] else "FAIL"))
    if not a.no_flip:
        log("flip suite")
        G["flip"] = run_flip(a.base, a.flip_cand or (a.cand + " --each"))
    binding = [k for k in G["slices"] if not k.endswith("-aware")]
    fails = [k for k in binding if G["slices"][k].get("verdict") == "FAIL"]
    if a.dry_run:
        verdict = "DRY-RUN"
    else:
        ok = not fails and G["dev_pooled"]["verdict"] != "FAIL" and (a.no_flip or not G["flip"]["lost"])
        verdict = "PASS" if ok else "FAIL"
    G["verdict"], G["failed"] = verdict, fails
    with open(os.path.join(out, "gate.json"), "w", encoding="utf-8") as f:
        json.dump(G, f, ensure_ascii=False, indent=1, default=lambda o: sorted(o) if isinstance(o, set) else str(o))
    md = render(G)
    with open(os.path.join(out, "gate.md"), "w", encoding="utf-8") as f:
        f.write(md + "\n")
    print(md)
    print(f"\ngate output: {out}", file=sys.stderr)
    return 0 if verdict in ("PASS", "DRY-RUN") else 1


def rubric_label(r):
    import judge  # noqa: E402 (only for the version label)
    return {"v1": "mqm-v1", "v2": "mqm-v2", "v3": judge.VERSION}[r]


def leak_count():
    p = os.path.join(GATES, "LEAKS.log")
    if not os.path.exists(p):
        return 0
    with open(p, encoding="utf-8") as f:
        return sum(1 for line in f if line.strip())


def render(G):
    lv = f", level gate {G['level']}" if G.get("level") else ""
    o = [f"# Gate {G['name']} ({G['time_kst']} KST{lv}): **{G['verdict']}**", "",
         f"- candidate: `{G['cand']}`", f"- base: `{G['base']}`",
         f"- judge rubric: {G['rubric']}" + (" (per slice: " + ", ".join(f"{k} {v}" for k, v in G["rubrics"].items())
                                             + ")" if G.get("rubrics") else ""),
         f"- hold-out leak log: {G.get('holdout_leaks', 0)} disclosure(s) recorded (wsB/gates/LEAKS.log)"]
    if G.get("sample"):
        o.append(f"- judged a sample of {G['sample']} changed lines per slice")
    o += ["", "| slice | lines | changed | majority W:T:L | judge 1 W:T:L | judge 2 W:T:L | worse by both | "
              "kappa (n) | crit/major cand / base (j1) | verdict |", "|---|---:|---:|---|---|---|---:|---|---|---|"]
    for k, s in G["slices"].items():
        f = lambda d: f"{d['win']}:{d['tie']}:{d['loss']}" if d else "–"  # noqa: E731
        kap = "–" if s.get("kappa") is None else f"{s['kappa']:.2f} ({s.get('kappa_n', 0)})"
        cm = s.get("cm_pct") or {}
        cms = " / ".join("–" if v is None else f"{v:.0f}%" for v in cm.values()) if cm else "–"
        o.append(f"| {k} | {s['lines']} | {s['changed']} | {f(s.get('majority'))} | {f(s.get('j1'))} | "
                 f"{f(s.get('j2'))} | {len(s.get('worse_by_both', []))} | {kap} | {cms} | "
                 f"{s.get('verdict', '–')} {('(' + s['why'] + ')') if s.get('why') else ''} |")
    d = G["dev_pooled"]
    o += ["", f"Dev pooled (majority): {d['win']}:{d['tie']}:{d['loss']} → {d['verdict']}"]
    for k, s in G["slices"].items():
        if s.get("worse_by_both"):
            o.append(f"- worse by both on {k}: {', '.join(s['worse_by_both'][:40])}")
    hb = next((v for k, v in G["slices"].items() if k.startswith("holdout") and k.endswith("-blind")), None)
    if hb and hb.get("blind_equals_aware"):
        o.append("- hold-out: cast-blind output equals cast-aware output (the engine has no cast data, or ignores "
                 "HD08_CAST_OFF)")
    if "flip" in G:
        fl = G["flip"]
        o += ["", f"Flip suite: base {fl['base']}/{fl['n']}, candidate {fl['cand']}/{fl['n']}, lost {len(fl['lost'])}, "
                  f"gained {len(fl['gained'])}",
              "  by cat (base/cand/n): " + ", ".join(f"{k} {v[0]}/{v[1]}/{v[2]}" for k, v in sorted(fl["by_cat"].items())),
              "  by sub (base/cand/n): " + ", ".join(f"{k} {v[0]}/{v[1]}/{v[2]}" for k, v in sorted(fl["by_sub"].items()))]
        for it in fl["lost"][:30]:
            o.append(f"  - LOST {it['id']} [{it['sub']}] {it['ja']} → base {it['base']} | cand {it['cand']}")
    if G.get("failed"):
        o.append(f"\nFailed slices: {', '.join(G['failed'])}")
    return "\n".join(o)


def self_test():
    st = {"majority": {"win": 20, "tie": 5, "loss": 10}, "worse_by_both": [], "kappa": 0.7, "kappa_n": 60}
    assert slice_verdict(st, 35)["verdict"] == "PASS"
    assert slice_verdict(dict(st, majority={"win": 19, "tie": 0, "loss": 10}), 29)["verdict"] == "FAIL"
    assert slice_verdict(dict(st, worse_by_both=["x:1"]), 35)["verdict"] == "FAIL"
    assert slice_verdict(dict(st, kappa=0.5), 35)["verdict"] == "FAIL"
    assert slice_verdict(dict(st, kappa=0.5, kappa_n=10), 35)["verdict"] == "PASS"
    assert slice_verdict(dict(st, majority={"win": 3, "tie": 0, "loss": 4}), 7)["verdict"] == "PASS"
    assert slice_verdict(st, 0)["verdict"] == "n/a"
    rep = {"judges": [{"model": "a"}, {"model": "b"}],
           "pairs": {"cand vs base": {"a": {"win": 1, "tie": 0, "loss": 1, "lines": {"1": 1, "2": -1}},
                                      "b": {"win": 0, "tie": 1, "loss": 1, "lines": {"1": 0, "2": -1}}}},
           "third": {"pairs": {"cand vs base": {"win": 0, "tie": 1, "loss": 1}}},
           "agreement": {"cm_kappa": 0.8, "n_outputs": 4}, "mqm": {}}
    s = pair_stats(rep, "cand", "base")
    assert s["worse_by_both"] == ["2"] and s["majority"]["loss"] == 1
    print("gate self-test OK")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(main())
