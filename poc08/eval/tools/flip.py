#!/usr/bin/env python3
"""Critical meaning-flip suite: pass/fail in seconds.

  flip.py run   [--suite TSV] (--cmd 'ENGINE ARGS' | --out KO.txt) [--fails N] [--save KO.txt]
  flip.py check [--suite TSV]          every item's ref passes its own want/reject, ids unique
  flip.py --self-test

The suite (default: ../flip/suite.tsv) is a TSV with a header:
  id  cat  sub  kind  ja  want  reject  ref  note
  cat   neg | tense | force | bene | voice | num | name
  sub   finer class, e.g. neg.rhetorical, neg.prohib, voice.potential
  kind  real (a real excerpt) | pair (minimal-pair partner of a real item) | inv (invented shape)
  want  Python regex that must match the Korean output
  reject  Python regex that must not match it (empty = none)
  ref   a correct Korean rendering; `check` requires it to pass
Patterns are matched against the output with all whitespace removed, so write them without spaces.

`run --cmd` feeds every ja line on stdin, one per line, and reads the same number of lines back;
give a line-by-line engine (e.g. hdtrans --each). `run --out` checks a ready output file aligned
with the suite rows. It prints pass rates per cat and sub and the first failures; exit 0 always
(the caller sets the gate). `--save` writes the outputs.
"""
import argparse
import collections
import csv
import os
import re
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, "..", "flip", "suite.tsv")
COLS = ["id", "cat", "sub", "kind", "ja", "want", "reject", "ref", "note"]
CATS = {"neg", "tense", "force", "bene", "voice", "num", "name"}
KINDS = {"real", "pair", "inv"}
WS = re.compile(r"\s+")


def load(path):
    with open(path, encoding="utf-8", newline="") as f:
        rows = list(csv.reader(f, delimiter="\t", quoting=csv.QUOTE_NONE))
    if not rows or rows[0][:len(COLS)] != COLS:
        sys.exit(f"{path}: header must be {' '.join(COLS)}")
    items = []
    for n, r in enumerate(rows[1:], 2):
        if not r or not r[0] or r[0].startswith("#"):
            continue
        r = (r + [""] * len(COLS))[:len(COLS)]
        items.append(dict(zip(COLS, r), line=n))
    return items


def verdict(item, ko):
    s = WS.sub("", ko)
    if not re.search(item["want"], s):
        return False, "want"
    if item["reject"] and re.search(item["reject"], s):
        return False, "reject"
    return True, ""


def check(items):
    errs, seen = [], set()
    for it in items:
        where = f"line {it['line']} {it['id']}"
        if it["id"] in seen:
            errs.append(f"{where}: duplicate id")
        seen.add(it["id"])
        if it["cat"] not in CATS:
            errs.append(f"{where}: cat {it['cat']!r}")
        if it["kind"] not in KINDS:
            errs.append(f"{where}: kind {it['kind']!r}")
        if not it["sub"].startswith(it["cat"] + "."):
            errs.append(f"{where}: sub {it['sub']!r} must start with {it['cat']}.")
        if "\n" in it["ja"] or not it["ja"].strip():
            errs.append(f"{where}: empty or multi-line ja")
        try:
            re.compile(it["want"])
            re.compile(it["reject"] or "x")
        except re.error as e:
            errs.append(f"{where}: bad regex {e}")
            continue
        if not it["want"]:
            errs.append(f"{where}: empty want")
            continue
        ok, why = verdict(it, it["ref"])
        if not ok:
            errs.append(f"{where}: ref fails {why}: {it['ref']}")
    return errs


def translate(cmd, lines):
    p = subprocess.run(shlex.split(cmd), input=("\n".join(lines) + "\n").encode(), capture_output=True,
                       env=dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR="off"))
    out = p.stdout.decode("utf-8").split("\n")
    if out and out[-1] == "":
        out.pop()
    if len(out) != len(lines):
        sys.exit(f"engine returned {len(out)} lines for {len(lines)} (rc {p.returncode}): "
                 f"{p.stderr.decode()[-500:]}")
    return out


def report(items, outs, fails):
    by_cat, by_sub, by_kind = (collections.defaultdict(lambda: [0, 0]) for _ in range(3))
    bad = []
    for it, ko in zip(items, outs):
        ok, why = verdict(it, ko)
        for d, k in ((by_cat, it["cat"]), (by_sub, it["sub"]), (by_kind, it["kind"])):
            d[k][0] += ok
            d[k][1] += 1
        if not ok:
            bad.append((it, ko, why))
    total = sum(v[0] for v in by_cat.values())
    print(f"flip suite: {total}/{len(items)} pass ({100 * total / max(1, len(items)):.1f}%)")
    for name, d in (("cat", by_cat), ("kind", by_kind), ("sub", by_sub)):
        print(f"  by {name}: " + ", ".join(f"{k} {v[0]}/{v[1]}" for k, v in sorted(d.items())))
    for it, ko, why in bad[:fails]:
        print(f"  FAIL {it['id']} [{it['sub']}] {why}: {it['ja']} => {ko}")
    return total


def self_test():
    it = {"id": "t1", "cat": "neg", "sub": "neg.plain", "kind": "inv", "ja": "行かない。",
          "want": "안가|가지않", "reject": "", "ref": "안 가.", "note": "", "line": 2}
    assert verdict(it, "안 가.") == (True, "")
    assert verdict(it, "가지 않는다.") == (True, "")
    assert verdict(it, "간다.") == (False, "want")
    it2 = dict(it, id="t2", want="바보", reject="바보가아니")
    assert verdict(it2, "그냥 바보가 아니다") == (False, "reject")
    assert check([it, dict(it)]) == ["line 2 t1: duplicate id"]
    assert check([dict(it, sub="tense.past")])[0].endswith("must start with neg.")
    print("flip self-test OK")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["run", "check"])
    ap.add_argument("--suite", default=DEFAULT)
    ap.add_argument("--cmd")
    ap.add_argument("--out")
    ap.add_argument("--save")
    ap.add_argument("--fails", type=int, default=30)
    a = ap.parse_args()
    items = load(a.suite)
    if a.mode == "check":
        errs = check(items)
        for e in errs:
            print(e)
        cats = collections.Counter(i["cat"] for i in items)
        kinds = collections.Counter(i["kind"] for i in items)
        print(f"{len(items)} items, {len(errs)} problems; {dict(cats)} {dict(kinds)}")
        return 1 if errs else 0
    if bool(a.cmd) == bool(a.out):
        sys.exit("give --cmd or --out")
    if a.cmd:
        outs = translate(a.cmd, [i["ja"] for i in items])
    else:
        with open(a.out, encoding="utf-8") as f:
            outs = f.read().rstrip("\n").split("\n")
        if len(outs) != len(items):
            sys.exit(f"{a.out}: {len(outs)} lines for {len(items)} items")
    if a.save:
        with open(a.save, "w", encoding="utf-8") as f:
            f.write("\n".join(outs) + "\n")
    report(items, outs, a.fails)
    return 0


if __name__ == "__main__":
    sys.exit(main())
