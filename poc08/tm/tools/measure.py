#!/usr/bin/env python3
"""TM channel, step 5: measure a layer against the kill criterion, with two blind judges.

  measure.py --layer DIR --out OUTDIR [--classify]

Sets (all development data, drawn before mining and left out of it):
- A: the 153 LEX faults of the anatomy sample (600 lines);
- N: 401 fiction-review faults of layer row or idiom (stratified by layer and frequency band);
- F: 300 fresh lines (150 game, 150 captions).
For each set the engine runs with and without the layer. Faults: the judges see the line, the error span, the
reviewer's suggestion and the OLD (without) and NEW (with) translations of changed lines: fixed? worse elsewhere?
For N, both judges first say whether the error is still present in OLD (faults already fixed in 0.7.6 leave the
denominator). Fault type (collocation, context sense, default gloss, other) is labelled once by judge 1 (--classify).
Pairs: every changed line of A's 600 lines and of F, blind A/B (order seeded), both judges.
Judge 1 = claude-sonnet-5-5 (medium), judge 2 = claude-opus-5-5 (low). Verdicts count when both judges agree.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import reach  # noqa: E402

sys.path.insert(0, C.EVALTOOLS)
import api  # noqa: E402

J1 = ('claude-sonnet-5-5', 'medium')
J2 = ('claude-opus-5-5', 'low')
CACHE = api.Cache(C.V08 + '/tm/judge-cache.jsonl')

FAULT = """# Was a known translation error fixed? (blind)

Each item: a Japanese line, an error a reviewer found in an earlier Korean translation (the Japanese span, the
earlier Korean for it, and the reviewer's suggestion), then translation OLD and translation NEW of the same line.
For each item answer:
- `old_err`: `Y` if OLD still has this error at that span (its rendering of the span is wrong or clearly
  unnatural in the way the reviewer describes), `N` if OLD already renders the span acceptably.
- `fixed`: `Y` if NEW renders the span correctly and naturally (the reviewer's wording or any other good
  rendering), `N` otherwise.
- `worse`: `Y` if NEW is worse than OLD anywhere else in the line, `N` otherwise.
Output one object per item, in order: {"n": 3, "old_err": "Y", "fixed": "Y", "worse": "N"}.

"""
FSCHEMA = {'type': 'object', 'properties': {'items': {'type': 'array', 'items': {
    'type': 'object', 'properties': {'n': {'type': 'integer'}, 'old_err': {'type': 'string', 'enum': ['Y', 'N']},
                                     'fixed': {'type': 'string', 'enum': ['Y', 'N']},
                                     'worse': {'type': 'string', 'enum': ['Y', 'N']}},
    'required': ['n', 'old_err', 'fixed', 'worse'], 'additionalProperties': False}}},
    'required': ['items'], 'additionalProperties': False}
PAIR = """# Which Korean translation is better? (blind)

Each item: a Japanese line and two Korean machine translations, A and B. They differ in a few words.
Decide which is better: meaning first, then natural modern Korean.
- `A` or `B`: that one is better.
- `same`: no real difference in quality.
Output one object per item, in order: {"k": 4, "v": "B"}.

"""
PSCHEMA = {'type': 'object', 'properties': {'items': {'type': 'array', 'items': {
    'type': 'object', 'properties': {'k': {'type': 'integer'}, 'v': {'type': 'string', 'enum': ['A', 'B', 'same']}},
    'required': ['k', 'v'], 'additionalProperties': False}}}, 'required': ['items'], 'additionalProperties': False}
TYPE = """# What kind of word-choice error is this?

Each item: a Japanese line, a span a reviewer marked in a machine translation into Korean, the machine's Korean for
the span and the reviewer's suggestion. Classify the error:
- `colloc`: the span is a multi-word unit (idiom, set phrase, verb + argument, noun + の + noun, compound) and the
  machine translated it word by word or picked the wrong word because of the combination;
- `context`: one word whose right meaning or wording depends on the context (the machine's word is right
  elsewhere);
- `gloss`: one word whose default Korean is simply wrong, archaic or a transliteration;
- `other`: grammar, ending, particle, name, segmentation or anything else.
Output one object per item, in order: {"n": 3, "t": "colloc"}.

"""
TSCHEMA = {'type': 'object', 'properties': {'items': {'type': 'array', 'items': {
    'type': 'object', 'properties': {'n': {'type': 'integer'},
                                     't': {'type': 'string', 'enum': ['colloc', 'context', 'gloss', 'other']}},
    'required': ['n', 't'], 'additionalProperties': False}}}, 'required': ['items'], 'additionalProperties': False}


def ask(judge, rubric, schema, items, fmt, idkey, size=20, jobs=12):
    """items: list of dicts; fmt(j, item) -> text block. Returns list of answers (dict) per item."""
    model, effort = judge
    res = [None] * len(items)
    todo = []
    for i, it in enumerate(items):
        k = api.key(model, rubric[:40], json.dumps(it, ensure_ascii=False, sort_keys=True))
        v = CACHE.get(k)
        if v is not None:
            res[i] = v
        else:
            todo.append((i, k))

    def one(chunk):
        txt = rubric + '\n'.join(fmt(j, items[i]) for j, (i, _) in enumerate(chunk, 1))
        want = set(range(1, len(chunk) + 1))

        def val(o):
            if {x[idkey] for x in o['items']} != want:
                raise ValueError('items')
        o = api.call(model, txt, schema, effort=effort, max_tokens=16000, validate=val)
        got = {x[idkey]: x for x in o['items']}
        for j, (i, k) in enumerate(chunk, 1):
            CACHE.put(k, got[j])
            res[i] = got[j]

    chunks = [todo[s:s + size] for s in range(0, len(todo), size)]
    with cf.ThreadPoolExecutor(jobs) as ex:
        for f in cf.as_completed([ex.submit(one, c) for c in chunks]):
            f.result()
    return res


def ffmt(j, it):
    return (f'### {j}\nJA: {it["ja"]}\nERROR SPAN: {it["span"]}  (earlier Korean for it: {it["ko"]}; '
            f'the reviewer suggested: {it["fix"]})\nOLD: {it["old"]}\nNEW: {it["new"]}\n')


def tfmt(j, it):
    return f'### {j}\nJA: {it["ja"]}\nSPAN: {it["span"]}  (machine: {it["ko"]}; reviewer: {it["fix"]})\n'


def pairs(lines, base, new, seed):
    rnd = random.Random(seed)
    items, key = [], []
    for i, (s, b, n) in enumerate(zip(lines, base, new)):
        if b.strip() == n.strip():
            continue
        flip = rnd.random() < 0.5
        A, B = (n, b) if flip else (b, n)
        items.append({'ja': s, 'A': A, 'B': B})
        key.append((i, 'A' if flip else 'B'))
    out = {}
    for name, j in (('j1', J1), ('j2', J2)):
        r = ask(j, PAIR, PSCHEMA, items, lambda k, it: f'### {k}\nJA: {it["ja"]}\nA: {it["A"]}\nB: {it["B"]}\n', 'k')
        out[name] = ['same' if x['v'] == 'same' else ('new' if x['v'] == ns else 'old') for x, (_, ns) in zip(r, key)]
    res = []
    for k, (i, _) in enumerate(key):
        v1, v2 = out['j1'][k], out['j2'][k]
        res.append({'line': i, 'ja': lines[i], 'old': base[i], 'new': new[i], 'j1': v1, 'j2': v2})
    return res


def summarize_pairs(res):
    c = collections.Counter()
    for r in res:
        v1, v2 = r['j1'], r['j2']
        c['changed'] += 1
        c['j1:' + v1] += 1
        c['j2:' + v2] += 1
        if v1 == v2 == 'new':
            c['better-both'] += 1
        elif v1 == v2 == 'old':
            c['worse-both'] += 1
        elif v1 == v2:
            c['same-both'] += 1
        else:
            c['split'] += 1
    return dict(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--layer', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    faults = reach.load_faults()
    a600 = open(C.MEAS + '/a600.ja', encoding='utf-8').read().rstrip('\n').split('\n')
    fresh = list(C.read_tsv(C.MEAS + '/fresh300.tsv'))
    nlines = sorted({f['ja'] for f in faults if f['src'] == 'N'})
    allj = a600 + nlines + [r['ja'] for r in fresh]
    base, _ = C.run_engine(allj)
    new, _ = C.run_engine(allj, layer_dir=a.layer)
    B = dict(zip(allj, base))
    N = dict(zip(allj, new))
    summary = {}
    # fault types (judge 1 once; independent of the layer)
    types = ask(J1, TYPE, TSCHEMA, [{'ja': f['ja'], 'span': f['span'], 'ko': f['ko'], 'fix': f['fix']} for f in faults],
                tfmt, 'n')
    for f, t in zip(faults, types):
        f['type'] = t['t']
        f['old'], f['new'] = B[f['ja']], N[f['ja']]
        f['changed'] = f['old'].strip() != f['new'].strip()
    # fault judgments on every fault (old_err needed for the denominator); unchanged lines are not fixed
    items = [{'ja': f['ja'], 'span': f['span'], 'ko': f['ko'], 'fix': f['fix'], 'old': f['old'], 'new': f['new']}
             for f in faults]
    r1 = ask(J1, FAULT, FSCHEMA, items, ffmt, 'n')
    r2 = ask(J2, FAULT, FSCHEMA, items, ffmt, 'n')
    rows = []
    for f, x1, x2 in zip(faults, r1, r2):
        f['j1'], f['j2'] = x1, x2
        present = f['src'] == 'A' or (x1['old_err'] == 'Y' and x2['old_err'] == 'Y')
        fixed = f['changed'] and x1['fixed'] == 'Y' and x2['fixed'] == 'Y'
        worse = f['changed'] and x1['worse'] == 'Y' and x2['worse'] == 'Y'
        f.update(present=present, fixed=fixed, worse=worse)
        rows.append(f)
    json.dump(rows, open(os.path.join(a.out, 'faults.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    tab = collections.defaultdict(collections.Counter)
    for f in rows:
        if not f['present']:
            tab[(f['src'], 'gone-in-base')]['n'] += 1
            continue
        for g in [(f['src'], 'all'), (f['src'], 'band:' + f['band']), (f['src'], 'type:' + f['type']),
                  (f['src'], 'cc' if f['type'] in ('colloc', 'context') else 'non-cc'), ('pool', 'all'),
                  ('pool', 'cc' if f['type'] in ('colloc', 'context') else 'non-cc')]:
            tab[g]['n'] += 1
            tab[g]['changed'] += f['changed']
            tab[g]['fixed'] += f['fixed']
            tab[g]['worse'] += f['worse']
    summary['faults'] = {f'{k[0]}:{k[1]}': dict(v) for k, v in sorted(tab.items())}
    # pairwise on A's 600 lines and on the fresh 300
    pa = pairs(a600, [B[s] for s in a600], [N[s] for s in a600], 31)
    fl = [r['ja'] for r in fresh]
    pf = pairs(fl, [B[s] for s in fl], [N[s] for s in fl], 32)
    for r in pf:
        r['set'] = fresh[r['line']]['set']
    json.dump({'a600': pa, 'fresh': pf}, open(os.path.join(a.out, 'pairs.json'), 'w', encoding='utf-8'),
              ensure_ascii=False, indent=0)
    summary['pairs-a600'] = summarize_pairs(pa)
    summary['pairs-fresh'] = summarize_pairs(pf)
    for s in ('game', 'caption'):
        summary[f'pairs-fresh-{s}'] = summarize_pairs([r for r in pf if r['set'] == s])
    # judge agreement on faults (changed lines)
    ch = [f for f in rows if f['changed']]
    summary['agree-fixed'] = f"{sum(f['j1']['fixed'] == f['j2']['fixed'] for f in ch)}/{len(ch)}"
    summary['agree-old_err'] = f"{sum(f['j1']['old_err'] == f['j2']['old_err'] for f in rows)}/{len(rows)}"
    json.dump(summary, open(os.path.join(a.out, 'summary.json'), 'w', encoding='utf-8'), indent=1)
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
