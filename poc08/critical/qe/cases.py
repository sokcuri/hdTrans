#!/usr/bin/env python3
"""Runs the fix and keep cases of data/check.cases.tsv: the Japanese lines through hdtrans08-ir (W1, W2 clauses),
the Korean through qe/ko.py (Kiwi), then the rows of data/check.tsv. Exit 1 on any failing case, or on a row
without a fix and a keep case.

  cases.py --ir PATH/hdtrans08-ir --dicts POC08_DIR [--verbose]
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import checks  # noqa: E402
import gate  # noqa: E402
import ja  # noqa: E402
import ko  # noqa: E402
import names  # noqa: E402

CASES = os.path.join(checks.HERE, '..', 'data', 'check.cases.tsv')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ir', required=True)
    ap.add_argument('--dicts', required=True)
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    rows = checks.load()
    cases, cols = [], None
    for line in open(CASES, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        c = line.split('\t')
        if cols is None:
            cols = c
            continue
        cases.append(dict(zip(cols, c)))
    srcs = sorted({c['ja'] for c in cases})
    with tempfile.TemporaryDirectory() as d:
        env = dict(os.environ, HOME=d, HDTRANS_CACHE_DIR='off')
        subprocess.run([a.ir, '--dicts', a.dicts, '--each', '--stages', d], input='\n'.join(srcs) + '\n', text=True,
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        recs = ja.load_ir(os.path.join(d, 'ir.jsonl'))
    exp = {s: ja.expectations(r) for s, r in zip(srcs, recs)}
    ids = {r.id for r in rows}
    seen = {i: set() for i in ids}
    bad = 0
    for c in cases:
        if c['id'] not in ids:
            print(f"case for unknown row {c['id']}")
            bad += 1
            continue
        seen[c['id']].add(c['kind'])
        v = checks.check(rows, exp[c['ja']], ko.analyse(c['ko']), gate.units, names.present)
        fired = any(rid == c['id'] for _, _, rid in v)
        ok = fired if c['kind'] == 'fix' else not fired
        if not ok or a.verbose:
            print(f"{'ok  ' if ok else 'FAIL'} {c['id']} {c['kind']}: {c['ja']} | {c['ko']} | {[x[2] for x in v]}")
        bad += not ok
    for i in sorted(ids):
        if seen[i] != {'fix', 'keep'}:
            print(f'row {i} needs a fix and a keep case')
            bad += 1
    print(f'check rows {len(rows)}, cases {len(cases)}, failing {bad}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
