#!/usr/bin/env python3
"""Upper bound of reach: which measured fault spans contain a mined candidate span (no LLM, no engine).

  reach.py [--cands WORK/cands.json]
"""
import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import mine  # noqa: E402

sys.path.insert(0, C.LEXTOOLS)
import lexlib  # noqa: E402


def fault_keys(ja, span):
    toks = lexlib.analyze(ja)
    offs, at = [], 0
    for t in toks:
        p = ja.find(t.surf, at)
        p = at if p < 0 else p
        offs.append(p)
        at = p + len(t.surf)
    sp = ja.find(span)
    keys = []
    for i, j, kind in mine.spans(toks):
        a, b = offs[i], offs[j - 1] + len(toks[j - 1].surf)
        if sp < 0 or (a < sp + len(span) and b > sp):
            keys.append(' '.join(t.lid if mine.tok_class(t) == 'C' else t.surf for t in toks[i:j]))
    return keys


def load_faults():
    a600 = open(C.MEAS + '/a600.ja', encoding='utf-8').read().rstrip('\n').split('\n')
    ids = [r['id'] for r in C.read_tsv(C.V08 + '/A/data/sample.tsv')]
    pos = {x: i for i, x in enumerate(ids)}
    out = []
    for f in C.read_tsv(C.MEAS + '/afaults.tsv'):
        out.append({'fid': f['fid'], 'src': 'A', 'band': f['band'], 'ja': a600[pos[f['id']]], 'span': f['span'],
                    'ko': f['ko'], 'fix': f['fix'], 'layer': 'lex'})
    for f in C.read_tsv(C.MEAS + '/nr-faults.tsv'):
        out.append({'fid': f['fid'], 'src': 'N', 'band': f['band'], 'ja': f['ja'], 'span': f['span'].split(' ')[0],
                    'ko': ' '.join(f['span'].split(' ')[1:]), 'fix': f['expected'] or f['fix'], 'layer': f['layer']})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cands', default=C.WORK + '/cands.json')
    a = ap.parse_args()
    cands = {c['key']: c for c in json.load(open(a.cands, encoding='utf-8'))}
    hit = collections.Counter()
    tot = collections.Counter()
    for f in load_faults():
        ks = [k for k in fault_keys(f['ja'], f['span']) if k in cands]
        g = (f['src'], f['band'])
        tot[g] += 1
        hit[g] += bool(ks)
    for g in sorted(tot):
        print(g, hit[g], '/', tot[g])
    print('A', sum(v for k, v in hit.items() if k[0] == 'A'), sum(v for k, v in tot.items() if k[0] == 'A'))
    print('N', sum(v for k, v in hit.items() if k[0] == 'N'), sum(v for k, v in tot.items() if k[0] == 'N'))


if __name__ == '__main__':
    main()
