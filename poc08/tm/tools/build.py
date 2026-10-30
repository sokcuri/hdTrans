#!/usr/bin/env python3
"""TM channel, step 4: dictionary rows from the agreed silver fragments, checked on the engine.

  build.py rows   [--silver WORK/silver.json] [--batch b1]     -> WORK/rows.json, WORK/p2-*.txt
  build.py final  --out DIR [--batch b1]                       -> DIR/zz-tm-<batch>.tsv, DIR/tm-<batch>.prov.tsv,
                                                                   TMDIR/cases/tm-<batch>.cases.tsv (outside the repo)

rows: one row per candidate in 0.7's column format. The head row is the dictionary row the engine chose for the
span's last token; its pos, sem, gram and dict codes are copied. `add`: a new longer key (the span up to the
head's stem) with the silver Korean; `replace`: the span already is one key, so every row of that key's class is
copied and the chosen row gets the silver Korean (the way the tail audit writes rows). Predicate Korean is
encoded with a marker family of the same final stem, part of speech and class. Rows the engine rejects and rows
that change no example line are dropped. The changed example lines go to a blind A/B judge (p2 packets).
final: keeps rows whose changed lines the judge prefers (no line worse), writes the layer, the provenance table
and the fix/keep cases.
"""
import argparse
import collections
import concurrent.futures as cf
import json
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

sys.path.insert(0, C.LEXTOOLS)
import lexlib  # noqa: E402
import audit_layer  # noqa: E402

sys.path.insert(0, C.EVALTOOLS)
import api  # noqa: E402

COLS = ['key', 'ko', 'pos', 'sem', 'gram', 'dict', 'rule', 'ctx', 'note', 'flags', 'prio', 'reading', 'id']
BAD_KO = re.compile(r'[぀-ヿ一-鿿()（）/\[\]{}<>@&$%,~…]')
ERR = re.compile(r'(zz-tm-[\w-]+\.tsv):(\d+): (.*)')
TMDIR = C.V08 + '/tm'
SILVER_TAG = 'silver:claude'


def head_text(batch):
    return f'#! layer=base\n# TM fragments ({batch}): lemma sequences with Korean from agreed silver translations.\n' + \
        '\t'.join(COLS) + '\n'


def layer_text(rows, batch):
    out = [head_text(batch)]
    for r in rows:
        out.append('\t'.join(r.get(c, '') for c in COLS) + '\n')
    return ''.join(out)


def write_layer(rows, batch, d):
    os.makedirs(d, exist_ok=True)
    for n in os.listdir(d):
        if n.endswith('.tsv'):
            os.remove(os.path.join(d, n))
    open(os.path.join(d, f'zz-tm-{batch}.tsv'), 'w', encoding='utf-8').write(layer_text(rows, batch))


def family_index(rows):
    """(marker, stem, V/A, class, uses) per Japanese POS digit of the rows that use the marker: a family is only
    offered for heads of a POS digit whose rows already use it (na-adjective families do not fit i-adjectives)."""
    uses = collections.defaultdict(collections.Counter)
    for r in rows:
        if r['userfmt'] or not lexlib.ENGINE_POS.match(r['pos']):
            continue
        for m in lexlib.MARK.findall(r.get('ko', '')):
            uses[r['pos'][1]][m] += 1
    base = audit_layer.family_index([])
    return {d: [(m, st, kp, cl, uses[d][m]) for m, st, kp, cl, _ in base] for d in uses}


def make_rows(x, by_key, fams, grammar_keys):
    """(rows, why_not) for one agreed candidate."""
    h = x['head']
    ko = x['ko']
    if BAD_KO.search(ko) or not re.search(r'[가-힣]', ko):
        return None, 'bad-ko'
    cand = [r for r in by_key.get(h['surface'], []) if not r['userfmt'] and lexlib.ENGINE_POS.match(r['pos'])]
    target = [r for r in cand if r['ko'] == h['text']]
    if not target:
        return None, 'no-head-row'
    t = target[0]
    d = t['pos'][1]
    pred = d in ('4', '7', '8')
    if d in ('2', '5', '6'):
        return None, 'grammar-head'
    if pred != ko.endswith('다'):
        return None, 'part-of-speech'
    if pred:
        kpos = 'V' if d == '4' else 'A'
        if (x['kind'] in ('vp', 'seq') and any(e[3] > 0 and e[2][e[3] - 1] == 'の' for e in x['ex'])
                and h['mode'] == 'add'):
            return None, 'no-before-verb-row'
        raw = audit_layer.encode(ko, kpos, fams.get(d, []))
        if not raw:
            return None, 'no-marker-family'
    else:
        raw = ko
    mode = h['mode']
    if mode == 'add':
        key = x['ekey']
        if key in by_key:
            return None, 'key-exists'
        r = {c: '' for c in COLS}
        r.update(key=key, ko=raw, pos=t['pos'], sem=t.get('sem', ''), gram=t.get('gram', ''), dict=t.get('dict', 'GN'))
        return [r], None
    key = h['surface']
    if key in grammar_keys:
        return None, 'grammar-key'
    same = [r for r in cand if r['pos'][1] == d]
    if any(r['userfmt'] for r in by_key.get(key, [])):
        return None, 'mixed-formats'
    out = []
    for r in same:
        c = {k: r.get(k, '') for k in COLS}
        if r is t:
            c['ko'] = raw
            c['flags'] = ' '.join(f for f in c['flags'].split() if f != 'llm')
            c['tm'] = True
        out.append(c)
    return out, None


def cmd_rows(a):
    xs = json.load(open(a.silver, encoding='utf-8'))
    rows = lexlib.load_rows(C.DICTS)
    by_key = collections.defaultdict(list)
    for r in rows:
        if r['layer'] in ('base', 'words'):
            by_key[r['key']].append(r)
    fams = family_index(rows)
    grammar_keys = {r['key'] for r in rows if lexlib.row_kind(r) in ('particle', 'ending')}
    why = collections.Counter()
    changes = []
    used = set()
    for x in xs:
        rs, w = make_rows(x, by_key, fams, grammar_keys)
        if w:
            why[w] += 1
            continue
        k = rs[0]['key']
        if k in used:
            why['duplicate-key'] += 1
            continue
        used.add(k)
        changes.append({'cand': x, 'rows': rs})
    # ids
    for n, ch in enumerate(changes, 1):
        for j, r in enumerate(ch['rows']):
            r['id'] = f'tm:{a.batch}:{n}' + (f'.{j}' if len(ch['rows']) > 1 else '')
    # engine validation: drop rows it rejects
    tmp = os.path.join(C.WORK, 'layer-try')
    for _ in range(6):
        flat = [(i, r) for i, ch in enumerate(changes) for r in ch['rows']]
        write_layer([r for _, r in flat], a.batch, tmp)
        _, err = C.run_engine(['テスト'], layer_dir=tmp)
        bad = set()
        for m in ERR.finditer(err):
            ln = int(m.group(2)) - 4
            if 0 <= ln < len(flat):
                bad.add(flat[ln][0])
        if not bad:
            break
        why['engine-rejects'] += len(bad)
        changes = [ch for i, ch in enumerate(changes) if i not in bad]
    # effect on the example lines, each row alone: rows sharing an example line go to different groups
    by_line = collections.defaultdict(list)
    for i, ch in enumerate(changes):
        for e in ch['cand']['ex']:
            by_line[e[2]].append(i)
    color = {}
    for i, ch in enumerate(changes):
        taken = {color[j] for e in ch['cand']['ex'] for j in by_line[e[2]] if j in color}
        c = 0
        while c in taken:
            c += 1
        color[i] = c
    groups = collections.defaultdict(list)
    for i, c in color.items():
        groups[c].append(i)
    lines = sorted({e[2] for ch in changes for e in ch['cand']['ex']})
    base, _ = C.run_engine(lines)
    base = dict(zip(lines, base))
    new = {}
    for c in sorted(groups):
        idx = groups[c]
        write_layer([r for i in idx for r in changes[i]['rows']], a.batch, tmp)
        gl = sorted({e[2] for i in idx for e in changes[i]['cand']['ex']})
        out, _ = C.run_engine(gl, layer_dir=tmp)
        got = dict(zip(gl, out))
        for i in idx:
            for e in changes[i]['cand']['ex']:
                new[(i, e[2])] = got[e[2]]
        print('group', c, len(idx), file=sys.stderr)
    kept = []
    for i, ch in enumerate(changes):
        ex = ch['cand']['ex']
        ch['lines'] = [e[2] for e in ex]
        ch['base'] = [base[s] for s in ch['lines']]
        ch['new'] = [new[(i, s)] for s in ch['lines']]
        if ch['new'] == ch['base']:
            why['no-effect'] += 1
            continue
        kept.append(ch)
    json.dump(kept, open(os.path.join(C.WORK, f'rows-{a.batch}.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print('rows', len(kept), 'add', sum(1 for c in kept if c['cand']['head']['mode'] == 'add'), dict(why))


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


def judge_pairs(pairs, model, cache_path, jobs=16, size=40, seed=5):
    """pairs: list of (ja, old, new). Returns verdicts 'new' | 'old' | 'same' per pair (blind, order shuffled)."""
    cache = api.Cache(cache_path)
    rnd = random.Random(seed)
    flips = [rnd.random() < 0.5 for _ in pairs]
    res = [None] * len(pairs)
    todo = []
    for i, (ja, o, n) in enumerate(pairs):
        A, B = (n, o) if flips[i] else (o, n)
        k = api.key(model, 'pair1', ja, A, B)
        v = cache.get(k)
        if v is not None:
            res[i] = v
        else:
            todo.append((i, ja, A, B, k))

    def one(chunk):
        txt = PAIR + '\n'.join(f'### {j}\nJA: {ja}\nA: {A}\nB: {B}\n' for j, (_, ja, A, B, _) in enumerate(chunk, 1))
        want = set(range(1, len(chunk) + 1))

        def val(o):
            if {x['k'] for x in o['items']} != want:
                raise ValueError('items')
        o = api.call(model, txt, PSCHEMA, effort='low', max_tokens=8000, validate=val)
        got = {x['k']: x['v'] for x in o['items']}
        for j, (i, _, _, _, k) in enumerate(chunk, 1):
            cache.put(k, got[j])
            res[i] = got[j]

    chunks = [todo[s:s + size] for s in range(0, len(todo), size)]
    with cf.ThreadPoolExecutor(jobs) as ex:
        for f in cf.as_completed([ex.submit(one, c) for c in chunks]):
            try:
                f.result()
            except Exception as e:  # noqa: BLE001
                print('judge batch failed', e, file=sys.stderr)
    out = []
    for i, v in enumerate(res):
        if v is None or v == 'same':
            out.append(v)
        else:
            new_side = 'A' if flips[i] else 'B'
            out.append('new' if v == new_side else 'old')
    return out


def cmd_final(a):
    kept = json.load(open(os.path.join(C.WORK, f'rows-{a.batch}.json'), encoding='utf-8'))
    pairs, owner = [], []
    for ci, ch in enumerate(kept):
        n = 0
        for s, b, w in zip(ch['lines'], ch['base'], ch['new']):
            if b != w and n < 3:
                pairs.append((s, b, w))
                owner.append(ci)
                n += 1
    v = judge_pairs(pairs, 'claude-sonnet-5-5', os.path.join(C.WORK, 'judge-p2.jsonl'))
    score = collections.defaultdict(collections.Counter)
    for ci, x in zip(owner, v):
        score[ci][x] += 1
    final = []
    st = collections.Counter()
    for ci, ch in enumerate(kept):
        s = score[ci]
        ok = s['new'] >= 1 and s['old'] == 0
        st['keep' if ok else 'drop'] += 1
        ch['p2'] = dict(s)
        if ok:
            final.append(ch)
    for ch in final:
        for r in ch['rows']:
            if len(ch['rows']) == 1 or r.get('tm'):
                r['note'] = SILVER_TAG     # Korean from a closed LLM: may not ship until regenerated
    rows = [r for ch in final for r in ch['rows']]
    os.makedirs(a.out, exist_ok=True)
    write_layer(rows, a.batch, a.out)
    with open(os.path.join(a.out, f'tm-{a.batch}.prov.tsv'), 'w', encoding='utf-8') as o:
        o.write('id\tkey\tlemmas\tkind\tmode\tko\tsilver\tlines\tdocs\tpmi\tsilver_a\tsilver_b\tp2\n')
        for ch in final:
            x = ch['cand']
            rid = ch['rows'][0]['id'].split('.')[0]
            o.write('\t'.join([rid, ch['rows'][0]['key'], x['key'], x['kind'], x['head']['mode'], x['ko'], SILVER_TAG,
                               str(x['n']),
                               str(x['docs']), str(x['pmi']), x['silver']['A'], x['silver']['B'],
                               ' '.join(f'{k}={v}' for k, v in sorted(ch['p2'].items()))]) + '\n')
    # cases with the final layer only (outside the repo: they quote corpus lines)
    lines = sorted({s for ch in final for s in ch['lines']})
    out, _ = C.run_engine(lines, layer_dir=a.out)
    out = dict(zip(lines, out))
    os.makedirs(os.path.join(TMDIR, 'cases'), exist_ok=True)
    with open(os.path.join(TMDIR, 'cases', f'tm-{a.batch}.cases.tsv'), 'w', encoding='utf-8') as o:
        o.write('id\tkind\tja\tko\n')
        for ch in final:
            rid = ch['rows'][0]['id'].split('.')[0]
            for k, (s, b) in enumerate(zip(ch['lines'], ch['base']), 1):
                kind = 'fix' if out[s] != b else 'keep'
                o.write('\t'.join([f'{rid}#{k}', kind, s.replace('\t', ' '), out[s].replace('\t', ' ')]) + '\n')
    size = os.path.getsize(os.path.join(a.out, f'zz-tm-{a.batch}.tsv'))
    print('final rows', len(final), 'lines', len(rows), 'bytes', size, dict(st))


def cmd_test(a):
    cases = list(C.read_tsv(os.path.join(TMDIR, 'cases', f'tm-{a.batch}.cases.tsv')))
    got, _ = C.run_engine([c['ja'] for c in cases], layer_dir=a.layer)
    bad = [(c, g) for c, g in zip(cases, got) if g.strip() != c['ko'].strip()]
    for c, g in bad[:20]:
        print('FAIL', c['id'], c['ja'], '| want', c['ko'], '| got', g)
    print(f'{len(cases) - len(bad)}/{len(cases)} cases pass')
    sys.exit(1 if bad else 0)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('rows')
    r.add_argument('--silver', default=C.WORK + '/silver.json')
    r.add_argument('--batch', default='b1')
    f = sub.add_parser('final')
    f.add_argument('--out', required=True)
    f.add_argument('--batch', default='b1')
    t = sub.add_parser('test')
    t.add_argument('--layer', required=True)
    t.add_argument('--batch', default='b1')
    a = ap.parse_args()
    {'rows': cmd_rows, 'final': cmd_final, 'test': cmd_test}[a.cmd](a)


if __name__ == '__main__':
    main()
