#!/usr/bin/env python3
"""TM channel, step 2: keep candidates of a useful shape and find their 0.7 dictionary key on the example lines.

  align.py [--cands WORK/cands2.json] [--out WORK/aligned.json]

Shape filter (PMI over the span's tokens): vp, nn, av with PMI >= 1; noun compounds and set phrases ending in a
predicate (after a case particle, or a verb + verb compound) with PMI >= 3.
Key: the engine (0.7.x, --json) is run on up to 4 example lines. The span must start at an engine token and end
inside its last engine token's stem (a predicate's ending may follow) or at its end (a noun). The key is the line
text from the span start to the end of that stem; the head row is the dictionary row the engine chose for the
last token (key = its surface, Korean = its choice). A candidate keeps the key found on most of its examples.
"""
import argparse
import collections
import json
import unicodedata
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

PRED = ('/v1', '/v5', '/vs', '/vk', '/adj')
CASEP = ('を', 'が', 'に', 'で', 'と', 'から', 'へ')


def keep(x):
    p = x['key'].split(' ')
    if x['pmi'] < 1:
        return False
    if x['kind'] in ('vp', 'nn', 'av'):
        return True
    if x['kind'] == 'cmp':
        return x['pmi'] >= 3
    if not p[-1].endswith(PRED):
        return False
    if len(p) >= 3 and p[-2] in CASEP:
        return x['pmi'] >= 3
    if len(p) == 2 and p[0].endswith(PRED):
        return x['pmi'] >= 3
    return False


def _n1(c):
    n = unicodedata.normalize('NFKC', c)
    return n if len(n) == 1 else c


def place(line, toks):
    """Character offsets (start, stem end, ending end) of the engine's choices, found in order in the line
    (full-width forms compared NFKC). A token whose surface is not in the line (the engine rewrote it) is None."""
    norm = ''.join(_n1(c) for c in line)
    out, at = [], 0
    for t in toks:
        surf = unicodedata.normalize('NFKC', t['surface'])
        s = norm.find(surf, at)
        if s < 0 or s - at > 8:
            out.append(None)
            continue
        e = s + len(surf)
        prev = next((i for i in range(len(out) - 1, -1, -1) if out[i] is not None), None)
        if prev is not None:
            out[prev] = (out[prev][0], out[prev][1], s)
        out.append((s, e, e))
        at = e
    prev = next((i for i in range(len(out) - 1, -1, -1) if out[i] is not None), None)
    if prev is not None:
        out[prev] = (out[prev][0], out[prev][1], len(line))
    return out


def align(line, a, b, toks):
    """toks: engine choices of the line. Returns (key, head choice) or None."""
    pl = place(line, toks)
    first = last = None
    gap = False
    for t, p in zip(toks, pl):
        if p is None:
            gap = gap or first is not None
            continue
        s, e, ee = p
        if s < a < e:
            return None          # the span starts inside a token
        if s == a:
            first = t
        if first is not None and s < b:
            if gap:
                return None      # a rewritten token inside the span
            last = (t, s, e, ee)
    if first is None or last is None:
        return None
    t, s, e, ee = last
    if e > b or not (e == b or ee >= b):
        return None              # the last token runs past the span, or the span ends inside its ending
    # one engine row already covering the span: the TM row replaces its Korean
    mode = 'replace' if s == a else 'add'
    return line[a:e], {'mode': mode, 'surface': t['surface'], 'text': t['choice']['text'],
                       'layer': t['choice']['layer'], 'ending': (t.get('ending') or {}).get('surface', '')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cands', default=C.WORK + '/cands2.json')
    ap.add_argument('--out', default=C.WORK + '/aligned.json')
    a = ap.parse_args()
    cands = [x for x in json.load(open(a.cands, encoding='utf-8')) if keep(x)]
    lines = sorted({e[2] for x in cands for e in x['ex'][:4]})
    print('candidates', len(cands), 'lines', len(lines), file=sys.stderr)
    choices = collections.defaultdict(list)
    base = {}
    step = 20000
    for i in range(0, len(lines), step):
        part = lines[i:i + step]
        d, err = C.run_engine(part, json_out=True)
        for c in d['choices']:
            choices[part[c['line']]].append(c)
        for s, o in zip(part, d['text'].split('\n')):
            base[s] = o
        print('engine', i + len(part), file=sys.stderr)
    out = []
    why = collections.Counter()
    for x in cands:
        got = collections.Counter()
        heads = {}
        for e in x['ex'][:4]:
            r = align(e[2], e[3], e[4], choices.get(e[2], []))
            if r:
                got[r[0]] += 1
                heads.setdefault(r[0], r[1])
        if not got:
            why['no-key'] += 1
            continue
        key = got.most_common(1)[0][0]
        x['ekey'] = key
        x['head'] = heads[key]
        x['base'] = {e[2]: base.get(e[2], '') for e in x['ex']}
        out.append(x)
    json.dump(out, open(a.out, 'w', encoding='utf-8'), ensure_ascii=False)
    print('aligned', len(out), dict(why), collections.Counter(x['kind'] for x in out), file=sys.stderr)


if __name__ == '__main__':
    main()
