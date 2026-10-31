#!/usr/bin/env python3
"""Agreement of the run-time 0.7.x class (IR-REQUESTS R1) with a reference class per decider item.

  poc08/tools/r1check.py ITEMS.tsv [--bin HDTRANS08] [--dicts DIR] [--show N]

ITEMS.tsv: id, family key (rareru, tame, youni, tokorode), character offset of the point in the line,
reference class, Japanese line. Every line is translated on its own with the deciders on
(--poc08 decide); the class StageD read from 0.7.x is the runnerUp t075:<class> of its decision at the
point. Gate: >= 95% agreement.
"""
import argparse
import collections
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FAMILY = {"rareru": "voice", "tame": "tame", "youni": "youni", "tokorode": "tokorode"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("items")
    ap.add_argument("--bin", default=str(ROOT / "build/mac/poc08/hdtrans08"))
    ap.add_argument("--dicts", default=str(ROOT / "dicts"))
    ap.add_argument("--show", type=int, default=0)
    ap.add_argument("--classes", help="write id<TAB>0.7.x class read at run time for every item")
    ap.add_argument("--dump", help="write the 0.7.x facts at each item's point as JSON Lines (for r1fit.py)")
    a = ap.parse_args()
    items = [l.rstrip("\n").split("\t", 4) for l in open(a.items, encoding="utf-8") if l.strip()]
    tmp = Path(tempfile.mkdtemp())
    (tmp / "in.txt").write_text("".join(i[4] + "\n" for i in items), encoding="utf-8")
    env = dict(os.environ, HOME=str(tmp), HDTRANS_CACHE_DIR="off")
    subprocess.run([a.bin, "--dicts", a.dicts, "--each", "--poc08", "decide", "--poc08-stages", str(tmp / "ir.jsonl"),
                    "--in", str(tmp / "in.txt")], env=env, check=True, stdout=subprocess.DEVNULL)
    per_doc, raw_docs, rec_docs = [], [], []
    for line in open(tmp / "ir.jsonl", encoding="utf-8"):
        r = json.loads(line)
        if "case" not in r:
            per_doc.append([])
            raw_docs.append([])
            rec_docs.append([])
            continue
        raw_docs[-1].extend(r.get("decisions", []))
        rec_docs[-1].append(r)
        for d in r.get("decisions", []):
            if d.get("rule", "").startswith("dec:"):
                per_doc[-1].append(d)
    raw = {i[0]: decs for i, decs in zip(items, raw_docs)}
    raw_recs = {i[0]: recs for i, recs in zip(items, rec_docs)}
    if len(per_doc) != len(items):
        sys.exit(f"{len(per_doc)} documents for {len(items)} items")
    dump = open(a.dump, "w", encoding="utf-8") if a.dump else None
    classes = open(a.classes, "w", encoding="utf-8") if a.classes else None
    agree = collections.Counter()
    total = collections.Counter()
    confusion = collections.Counter()
    missing = collections.Counter()
    shown = 0
    for (iid, key, start, ref, ja), decs in zip(items, per_doc):
        fam = FAMILY[key]
        at = len(ja[: int(start)].encode("utf-8"))
        hit = [d for d in decs if d["rule"].startswith("dec:" + fam) and d["src"][0] == at]
        if dump:
            facts = collections.defaultdict(list)
            for d in raw[iid]:
                if d.get("stage") == "T" and not d.get("rule", "").startswith("dec:") and d["src"][0] <= at < d["src"][1]:
                    for b in d.get("because", []):
                        if b.startswith("t075."):
                            k, _, v = b[5:].partition("=")
                            facts[k].append(v)
            for r in raw_recs[iid][:1]:
                toks = r.get("toks", [])
                for k, t in enumerate(toks):
                    if t["src"][0] == at and k > 0:
                        facts["jlem"].append(toks[k - 1].get("lemma", ""))
            dump.write(json.dumps({"id": iid, "family": fam, "ref": ref, "facts": facts}, ensure_ascii=False) + "\n")
        total[fam] += 1
        if classes:
            classes.write(f"{iid}\t{hit[0].get('runnerUp', 't075:NONE').split(':', 1)[1] if hit else ''}\n")
        if not hit:
            missing[fam] += 1
            continue
        got = hit[0].get("runnerUp", "t075:NONE").split(":", 1)[1]
        ok = got == ref or (ref in ("NONE", "OTHER") and got in ("NONE", "OTHER", ""))
        agree[fam] += ok
        if not ok:
            confusion[(fam, ref, got)] += 1
            if shown < a.show:
                print(f"  {iid} {fam} ref={ref} got={got} {ja}")
                shown += 1
    print("family\titems\tpoint found\tagree (of found)\tagree (of all)")
    for fam in ["voice", "tame", "youni", "tokorode"]:
        found = total[fam] - missing[fam]
        print(f"{fam}\t{total[fam]}\t{found}\t{100 * agree[fam] / max(1, found):.1f}%\t{100 * agree[fam] / max(1, total[fam]):.1f}%")
    n, f, g = sum(total.values()), sum(total.values()) - sum(missing.values()), sum(agree.values())
    print(f"all\t{n}\t{f}\t{100 * g / max(1, f):.1f}%\t{100 * g / max(1, n):.1f}%")
    print("top disagreements (family, reference, 0.7.x read):")
    for k, c in confusion.most_common(15):
        print(f"  {c}\t{k}")


if __name__ == "__main__":
    main()
