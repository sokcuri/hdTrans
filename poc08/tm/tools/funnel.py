#!/usr/bin/env python3
"""Where the measured collocation/context faults leave the TM pipeline: mined, shaped, keyed, agreed, row.

  funnel.py --faults MEASDIR/faults.json [--batch b1]
"""
import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import align  # noqa: E402
import reach  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--faults', required=True)
    ap.add_argument('--batch', default='b1')
    a = ap.parse_args()
    mined = {x['key']: x for x in json.load(open(C.WORK + '/cands2.json', encoding='utf-8'))}
    keyed = {x['key'] for x in json.load(open(C.WORK + '/aligned.json', encoding='utf-8'))}
    agreed = {x['key'] for x in json.load(open(C.WORK + '/silver.json', encoding='utf-8'))}
    rows = {ch['cand']['key'] for ch in json.load(open(C.WORK + f'/rows-{a.batch}.json', encoding='utf-8'))}
    F = json.load(open(a.faults, encoding='utf-8'))
    st = collections.defaultdict(collections.Counter)
    for f in F:
        if not f['present']:
            continue
        g = 'cc' if f['type'] in ('colloc', 'context') else 'non-cc'
        ks = reach.fault_keys(f['ja'], f['span'])
        stages = [('any span', ks), ('mined n>=2', [k for k in ks if k in mined])]
        stages.append(('shape', [k for k in stages[-1][1] if align.keep(mined[k])]))
        stages.append(('keyed', [k for k in stages[-1][1] if k in keyed]))
        stages.append(('agreed', [k for k in stages[-1][1] if k in agreed]))
        stages.append(('row', [k for k in stages[-1][1] if k in rows]))
        for name, ks2 in stages:
            st[g][name] += bool(ks2)
        st[g]['n'] += 1
        st[g]['fixed'] += f['fixed']
    for g, c in st.items():
        print(g, dict(c))


if __name__ == '__main__':
    main()
