#!/usr/bin/env python3
"""Coverage report (PLAN-v3 §2.5): the share of VN lines whose every construction is handled by rules.

  coverage.py --index DIR [--rules DIR ...] [--handled FILE] [--lexicon DIR ...] [--out DIR] [--counts-only]
              [--table constructions.tsv]

Reads DIR/raw.jsonl (index.py run) and tags it with the construction table. Three measures per line level and
per title:
- native: the engine routed the line natively (IR `Line.route` = native, DESIGN-v3 §1/§8). Only builds with the
  native transfer write `route`; for older builds the share is 0 and the column says so.
- grammar: every construction family of the line (kind g or s) has a rule, and no raw key is unmapped. A family
  has a rule when a row of a table under --rules (any *.tsv with an `id` column; ids map to families through the
  table's `cx:<id>` patterns, else by the longest family name that prefixes the id) or a line of --handled names
  it.
- grammar + lexicon: also every content lemma has a reviewed lexicon row (st r or m) in the --lexicon tables,
  and no token is unknown.
Then, per level: the families without a rule ranked by the number of lines of that level containing them, with
the lines each would unlock alone (`sole`), and the greedy cover curve (families taken in the order that unlocks
the most lines next); raw keys no table row maps; tokens L left unfolded; dead rule rows (ids under --rules that
never decided anything on these lines). The order is a work queue; it writes no rule.
--counts-only (for the hold-out) prints numbers only, never a line.
Writes OUT/coverage.md and OUT/coverage.json (default OUT = DIR).
"""
import argparse
import collections
import csv
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from index import LEVELS, RANK, Tagger, load_table, line_tags  # noqa: E402

csv.field_size_limit(1 << 30)


def rule_ids(dirs):
    ids = {}
    for d in dirs:
        for p in sorted(glob.glob(os.path.join(d, "**", "*.tsv"), recursive=True)):
            if p.endswith(".cases.tsv"):
                continue
            with open(p, encoding="utf-8") as f:
                hdr = None
                for line in f:
                    if line.startswith("#") or not line.strip():
                        continue
                    c = line.rstrip("\n").split("\t")
                    if hdr is None:
                        hdr = c
                        if "id" not in hdr:
                            break
                        continue
                    rid = c[hdr.index("id")] if len(c) > hdr.index("id") else ""
                    if rid:
                        ids.setdefault(rid, os.path.relpath(p, d))
    return ids


def family_of_rule(rid, tagger, families):
    t = tagger("cx:" + rid)
    if t and t[0] != "-":
        return t[0]
    best = ""
    for f in families:
        if (rid == f or rid.startswith(f + ".")) and len(f) > len(best):
            best = f
    return best or None


def reviewed_lemmas(dirs):
    out = set()
    for d in dirs:
        for p in sorted(glob.glob(os.path.join(d, "**", "*.tsv"), recursive=True)):
            with open(p, encoding="utf-8") as f:
                hdr = None
                for line in f:
                    if line.startswith("#") or not line.strip():
                        continue
                    c = line.rstrip("\n").split("\t")
                    if hdr is None:
                        hdr = c
                        if "ja" not in hdr or "st" not in hdr:
                            break
                        continue
                    if len(c) > hdr.index("st") and c[hdr.index("st")] in ("r", "m"):
                        out.add(c[hdr.index("ja")])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index", required=True)
    ap.add_argument("--rules", action="append", default=[])
    ap.add_argument("--handled", default="")
    ap.add_argument("--lexicon", action="append", default=[])
    ap.add_argument("--out", default="")
    ap.add_argument("--counts-only", action="store_true")
    ap.add_argument("--table", default=os.path.join(HERE, "constructions.tsv"))
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()
    out = a.out or a.index
    rows = load_table(a.table)
    tagger = Tagger(rows)
    fam_level = {r[1]: (r[2], r[3]) for r in rows if r[1] != "-"}
    ids = rule_ids(a.rules)
    handled, rule_fam = set(), {}
    for rid in ids:
        f = family_of_rule(rid, tagger, fam_level)
        rule_fam[rid] = f
        if f:
            handled.add(f)
    if a.handled:
        with open(a.handled, encoding="utf-8") as f:
            handled |= {x.strip() for x in f if x.strip() and not x.startswith("#")}
    lex = reviewed_lemmas(a.lexicon) if a.lexicon else None

    per = collections.defaultdict(lambda: collections.Counter())   # level -> counters
    per_title = collections.defaultdict(lambda: collections.Counter())
    missing = collections.defaultdict(collections.Counter)        # level -> family -> lines
    sole = collections.defaultdict(collections.Counter)
    line_missing = collections.defaultdict(list)                  # level -> [frozenset of missing families]
    unmapped, unfolded, unmatched_engine, fired, lex_missing = (collections.Counter() for _ in range(5))
    has_route = False
    n = 0
    with open(os.path.join(a.index, "raw.jsonl"), encoding="utf-8") as f:
        for row in f:
            r = json.loads(row)
            n += 1
            fams, unm = line_tags(tagger, r["keys"])
            g = {fm for fm, v in fams.items() if v[1] in ("g", "s")}
            level = max((fams[fm][0] for fm in g), key=lambda l: RANK[l], default="N5")
            miss = g - handled
            gram = not miss and not unm
            unknown = any(k == "tok:unknown" for k in r["keys"])
            lex_ok = lex is not None and not unknown and all(x.split("/")[0] in lex for x in r.get("lem", []))
            if lex is not None and not unknown:
                for x in r.get("lem", []):
                    if x.split("/")[0] not in lex:
                        lex_missing[x] += 1
            native = r.get("route") == "native"
            has_route |= "route" in r
            for c in (per[level], per_title[r["title"]], per["all"]):
                c["lines"] += 1
                c["native"] += native
                c["grammar"] += gram
                c["grammar_lex"] += gram and lex_ok
                c["empty"] += not g
            for fm in miss:
                missing[level][fm] += 1
            if len(miss) == 1:
                sole[level][next(iter(miss))] += 1
            line_missing[level].append(frozenset(miss) if not unm else None)
            for k in unm:
                unmapped[k] += 1
            for u in r.get("unf", []):
                unfolded[u] += 1
            for u in r.get("unmatched", []):
                unmatched_engine[u] += 1
            for rid in r.get("rules", []):
                fired[rid.split(":", 1)[1] if ":" in rid else rid] += 1
            for k in r["keys"]:
                if k.startswith("cx:"):
                    fired[k[3:]] += 1

    def pct(c, k):
        return f"{100 * c[k] / c['lines']:.1f}%" if c["lines"] else "–"

    res = {"lines": n, "rules": len(ids), "handled_families": sorted(handled), "has_route": has_route,
           "lexicon_lemmas": None if lex is None else len(lex), "by_level": {}, "by_title": {}}
    md = ["# Coverage of the VN lines", "",
          f"{n} lines. Rule rows read: {len(ids)} from {', '.join(a.rules) or 'none'}; families with a rule: "
          f"{len(handled)} of {len(fam_level)}. Lexicon: "
          + ("not given" if lex is None else f"{len(lex)} reviewed lemmas (st r/m)") + ".",
          "" if has_route else "The engine wrote no `route` field (no native transfer in this build): native = 0.",
          "", "grammar = every construction family of the line has a rule row (a proxy until the native route "
          "writes `route`); `no constr.` = lines with no construction family at all (counted as covered).",
          "", "| level | lines | no constr. | native | grammar | grammar + lexicon |", "|---|---:|---:|---:|---:|---:|"]
    for lev in LEVELS + ["all"]:
        c = per.get(lev)
        if not c:
            continue
        res["by_level"][lev] = dict(c)
        md.append(f"| {lev} | {c['lines']} | {pct(c, 'empty')} | {pct(c, 'native')} | {pct(c, 'grammar')} | "
                  f"{pct(c, 'grammar_lex') if lex is not None else '–'} |")
    md += ["", "| title | lines | no constr. | native | grammar | grammar + lexicon |", "|---|---:|---:|---:|---:|---:|"]
    for t, c in sorted(per_title.items()):
        res["by_title"][t] = dict(c)
        md.append(f"| {t} | {c['lines']} | {pct(c, 'empty')} | {pct(c, 'native')} | {pct(c, 'grammar')} | "
                  f"{pct(c, 'grammar_lex') if lex is not None else '–'} |")
    # per level: families without a rule, the greedy cover curve. The curve takes the lower levels as built
    # (their families count as handled), so it shows what this level's own families unlock.
    res["missing"], res["curve"] = {}, {}
    for lev in LEVELS:
        if lev not in per:
            continue
        top = missing[lev].most_common()
        res["missing"][lev] = [{"family": fm, "lines": v, "sole": sole[lev][fm]} for fm, v in top]
        tot = len(line_missing[lev])
        rem = [frozenset(x for x in s if RANK[fam_level.get(x, ("VN",))[0]] >= RANK[lev])
               for s in line_missing[lev] if s is not None]
        cur = sum(1 for s in rem if not s)
        base_share = 100 * cur / tot if tot else 0
        rem = [s for s in rem if s]
        curve = []
        for _ in range(a.top):
            if not rem:
                break
            single = collections.Counter(next(iter(s)) for s in rem if len(s) == 1)
            fm = (single or collections.Counter(x for s in rem for x in s)).most_common(1)[0][0]
            rem = [s - {fm} for s in rem]
            cur += sum(1 for s in rem if not s)
            rem = [s for s in rem if s]
            curve.append({"family": fm, "covered": cur, "share": 100 * cur / tot if tot else 0})
        res["curve"][lev] = {"start_share": base_share, "steps": curve}
        md += ["", f"## {lev}: families without a rule ({per[lev]['lines']} lines at this level)", "",
               "| family | level | lines | sole | greedy step | level covered after the step |",
               "|---|---|---:|---:|---:|---:|"]
        step = {c["family"]: (i + 1, c) for i, c in enumerate(curve)}
        for fm, v in top[:a.top]:
            st = step.get(fm)
            after = f"{st[1]['covered']} ({st[1]['share']:.1f}%)" if st else ""
            md.append(f"| {fm} | {fam_level.get(fm, ('?',))[0]} | {v} | {sole[lev][fm]} | "
                      f"{st[0] if st else ''} | {after} |")
        if curve:
            md += ["", f"Greedy order (lower levels taken as built; start {base_share:.1f}%): "
                   + " → ".join(f"{c['family']} {c['share']:.1f}%" for c in curve[:15])]
    dead = sorted(rid for rid in ids if fired.get(rid, 0) == 0)
    res["dead_rules"] = dead
    res["unmapped"] = unmapped.most_common(50)
    res["unfolded"] = unfolded.most_common(50)
    res["unmatched_engine"] = unmatched_engine.most_common(100)
    res["lexicon_missing"] = [] if a.counts_only else lex_missing.most_common(200)
    md += ["", "## Unmatched forms", "",
           "Raw keys no construction row maps (by lines): " + (", ".join(f"`{k}` {v}" for k, v in unmapped.most_common(20))
                                                               or "none"),
           "", "Grammatical tokens L left unfolded: " + (", ".join(f"`{k}` {v}" for k, v in unfolded.most_common(20))
                                                       or "none"),
           "", "Engine-reported unmatched forms: " + (", ".join(f"`{k}` {v}" for k, v in
                                                          unmatched_engine.most_common(30)) or "none (field absent)")]
    if lex is not None and a.counts_only:
        md += ["", f"Content lemmas without a reviewed row: {len(lex_missing)} distinct (not listed: hold-out)"]
    elif lex is not None:
        md += ["", "Content lemmas without a reviewed row (top 30 by lines): "
               + ", ".join(f"`{k}` {v}" for k, v in lex_missing.most_common(30))]
    md += ["", f"## Dead rule rows ({len(dead)} of {len(ids)} never fired on these lines)", "",
           ", ".join(f"`{d}`" for d in dead[:200]) or "none"]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "coverage.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    with open(os.path.join(out, "coverage.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md) + "\n")
    print("\n".join(md[:30]))


if __name__ == "__main__":
    main()
