#!/usr/bin/env python3
"""Measurement sets for the TM channel, drawn before any mining so that mining can leave them out.

  prep.py            writes MEAS/{afaults.tsv,a600.ja,nr-faults.tsv,fresh300.tsv,exclude.txt}

- A's LEX faults (153, from the E3 measurement) and the 600 A sample lines.
- Novel-review faults of layer row or idiom: a stratified sample of 400 (layer x frequency band).
- 300 fresh lines: 150 game rows outside gc and 150 caption lines, outside the held-out sets, the study pool,
  the E3 fresh lines and the A sample.
exclude.txt lists every measurement line plus every fiction-review fault line; mining skips them.
"""
import collections
import glob
import random
import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

sys.path.insert(0, C.LEXTOOLS)
import lexlib  # noqa: E402

KANA = re.compile(r'[぀-ヿ]')
E3M = C.V08 + '/lexicon/e3/meas'


def ranks():
    r = {}
    for i, row in enumerate(C.read_tsv(C.V08 + '/lexicon/work/corpus/lemmas.tsv')):
        if row['pos'] in lexlib.CONTENT and row['pos'] != 'num':
            r.setdefault(row['lid'], len(r) + 1)
    return r


def band(rank):
    if rank is None or rank > 20000:
        return 'rare>20k'
    if rank < 500:
        return 'head<500'
    return '500-2k' if rank <= 2000 else '2k-20k'


def span_rank(ja, span, rk):
    toks = lexlib.analyze(ja)
    pos = ja.find(span)
    out, at = [], 0
    for t in toks:
        s = ja.find(t.surf, at)
        if s < 0:
            continue
        at = s + len(t.surf)
        if pos >= 0 and (s >= pos + len(span) or at <= pos):
            continue
        if t.pos in lexlib.CONTENT and t.pos not in ('num', 'pn'):
            out.append(rk.get(t.lid, 10 ** 6))
    if pos < 0 and not out:
        out = [rk.get(t.lid, 10 ** 6) for t in lexlib.analyze(span) if t.pos in lexlib.CONTENT]
    return max(out) if out else None


def main():
    os.makedirs(C.MEAS, exist_ok=True)
    rk = ranks()
    excl = set()
    # A sample
    a600 = open(E3M + '/a600.ja', encoding='utf-8').read().rstrip('\n').split('\n')
    excl |= {s.strip() for s in a600}
    open(C.MEAS + '/a600.ja', 'w', encoding='utf-8').write('\n'.join(a600) + '\n')
    open(C.MEAS + '/afaults.tsv', 'w', encoding='utf-8').write(open(E3M + '/afaults.tsv', encoding='utf-8').read())
    # fiction-review faults
    rows = []
    for f in sorted(glob.glob(C.NOVEL_REVIEW + '/r*/judged.tsv')):
        for r in C.read_tsv(f):
            if not r.get('ja'):
                continue
            excl.add(r['ja'].strip())
            lay = r['layer']
            if lay in ('row', 'idiom', 'row+idiom', 'idiom+row'):
                r['layer'] = 'idiom' if 'idiom' in lay else 'row'
                rows.append(r)
    rnd = random.Random(1031)
    rnd.shuffle(rows)
    seen, strata, picked = set(), collections.defaultdict(list), []
    for r in rows:
        if r['ja'] in seen:
            continue
        seen.add(r['ja'])
        span = r['span'].split(' ')[0] if r['span'] else ''
        r['jspan'] = span
        r['band'] = band(span_rank(r['ja'].strip(), span, rk))
        strata[(r['layer'], r['band'])].append(r)
    quota = {'row': 240, 'idiom': 160}
    for lay, q in quota.items():
        keys = [k for k in strata if k[0] == lay]
        tot = sum(len(strata[k]) for k in keys)
        for k in sorted(keys):
            n = max(1, round(q * len(strata[k]) / tot))
            picked += strata[k][:n]
    with open(C.MEAS + '/nr-faults.tsv', 'w', encoding='utf-8') as o:
        o.write('fid\tfile\tline\tlayer\tband\tja\tspan\texpected\tfix\n')
        for i, r in enumerate(picked, 1):
            o.write('\t'.join([f'N{i:03d}', r['file'], r['line'], r['layer'], r['band'], r['ja'].strip(),
                               r['span'], r['expected'], r['fix']]).replace('\r', '') + '\n')
    print('nr faults', len(picked), collections.Counter((r['layer'], r['band']) for r in picked))
    # fresh lines
    ev = C.eval_lines()
    pool = C.study_pool()
    e3f = set(open(E3M + '/fresh300.ja', encoding='utf-8').read().split('\n'))
    avoid = ev | pool | e3f | excl
    fresh = []
    for name, path in (('game', C.CORPORA['game2']), ('caption', C.CORPORA['caption'])):
        cand = sorted({s.strip() for s in open(path, encoding='utf-8', errors='replace')
                       if 8 <= len(s.strip()) <= 80 and KANA.search(s) and s.strip() not in avoid})
        r2 = random.Random(1031 + len(name))
        r2.shuffle(cand)
        fresh += [(name, s) for s in cand[:150]]
    with open(C.MEAS + '/fresh300.tsv', 'w', encoding='utf-8') as o:
        o.write('id\tset\tja\n')
        for i, (n, s) in enumerate(fresh, 1):
            o.write(f'f{i:03d}\t{n}\t{s}\n')
    excl |= {s for _, s in fresh}
    open(C.MEAS + '/exclude.txt', 'w', encoding='utf-8').write('\n'.join(sorted(excl)) + '\n')
    print('fresh', len(fresh), 'exclude', len(excl), 'eval lines', len(ev))


if __name__ == '__main__':
    main()
