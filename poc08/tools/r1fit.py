#!/usr/bin/env python3
"""Fits the decision list that reads 0.7.x's class at a decider point (IR-REQUESTS R1) from the facts the
bridge records, and writes it as poc08/transfer/data/decide/base075.tsv rows.

  poc08/tools/r1fit.py FACTS.jsonl [--out base075.tsv] [--folds 5]

FACTS.jsonl comes from r1check.py --dump: per item its family, reference class and the t075 facts
(voice, marker, jlem, ko, real). Candidate rows test one fact (voice and marker equal, ko and real by
substrings of 1-3 characters); rows are added greedily by correct minus five times wrong on the
items not yet covered. Prints the cross-validated agreement (folds by id) before writing the list fitted on all items.
"""
import argparse
import collections
import hashlib
import json

SOURCES = ("voice", "marker", "jlem", "ko", "real")


def norm(c):
    return "NONE" if c in ("", "OTHER") else c


def candidates(facts):
    out = set()
    for src in SOURCES:
        for v in facts.get(src, []):
            if src in ("voice", "marker", "jlem"):
                out.add((src, v))
                continue
            for n in (1, 2, 3):
                for i in range(len(v) - n + 1):
                    sub = v[i:i + n]
                    if sub.strip() and "\t" not in sub:
                        out.add((src, sub))
    return out


def fit(items, min_gain=3, penalty=5, min_support=4):
    rows, left = [], list(items)
    cand = [candidates(it["facts"]) for it in left]
    while left:
        stats = collections.defaultdict(collections.Counter)
        for it, cs in zip(left, cand):
            for c in cs:
                stats[c][norm(it["ref"])] += 1
        best, best_gain = None, min_gain - 1
        for c, cnt in stats.items():
            cls, n = cnt.most_common(1)[0]
            if n < min_support:
                continue
            gain = n - penalty * (sum(cnt.values()) - n)
            if gain > best_gain or (gain == best_gain and best and (c, cls) < best):
                best, best_gain = (c, cls), gain
        if best is None:
            break
        (src, pat), cls = best
        rows.append((src, pat, cls))
        keep = [(it, cs) for it, cs in zip(left, cand) if (src, pat) not in cs]
        left = [k[0] for k in keep]
        cand = [k[1] for k in keep]
    rest = collections.Counter(norm(it["ref"]) for it in left)
    rows.append(("*", "*", rest.most_common(1)[0][0] if rest else "NONE"))
    return rows


def apply(rows, facts):
    cs = candidates(facts)
    for src, pat, cls in rows:
        if src == "*" or (src, pat) in cs:
            return cls
    return "NONE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("facts")
    ap.add_argument("--out")
    ap.add_argument("--folds", type=int, default=5)
    a = ap.parse_args()
    items = [json.loads(l) for l in open(a.facts, encoding="utf-8")]
    fams = sorted({i["family"] for i in items})
    out_rows = []
    total_ok = total = 0
    for fam in fams:
        its = [i for i in items if i["family"] == fam]
        fold = lambda i: int(hashlib.sha1(i["id"].encode()).hexdigest(), 16) % a.folds
        ok = 0
        for k in range(a.folds):
            rows = fit([i for i in its if fold(i) != k])
            ok += sum(apply(rows, i["facts"]) == norm(i["ref"]) for i in its if fold(i) == k)
        rows = fit(its)
        train = sum(apply(rows, i["facts"]) == norm(i["ref"]) for i in its)
        print(f"{fam}\t{len(its)}\tcv {100 * ok / len(its):.1f}%\tfit {100 * train / len(its):.1f}%\trows {len(rows)}")
        total_ok += ok
        total += len(its)
        out_rows += [(fam,) + r for r in rows]
    print(f"all\t{total}\tcv {100 * total_ok / total:.1f}%")
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write("#! base075\n# family\tsource\tpattern\tclass: how 0.7.x's own decision at a decider point reads as a class\n"
                    "# (IR-REQUESTS R1). Fitted by poc08/tools/r1fit.py on mapper-labelled items; first matching row of the\n"
                    "# family wins. voice, marker, jlem: equal to the fact; ko, real: a substring of it; * any.\n")
            for r in out_rows:
                f.write("\t".join(r) + "\n")


if __name__ == "__main__":
    main()
