#!/usr/bin/env python3
"""TM channel, step 1: count lemma sequences over the development corpora and keep example lines.

  mine.py [--min 3] [--out WORK/cands.json]

A candidate is a contiguous span of 2-4 UniDic tokens that starts and ends with a content word; tokens
inside may be content words or the joining particles (case particles, の, と). Kinds:
  vp  noun + case particle + predicate (照射を浴びる, 戸をたたく; light verbs included)
  nn  noun + の + noun (パジャマのパンツ)
  cmp noun + noun (目玉商品)
  av  adverb + predicate (ピンと来る)
  seq anything else of that shape (set phrases)
Lines of the held-out sets and of the measurement sets (MEAS/exclude.txt) are skipped. Lines are counted
once per corpus (captions repeat). For each candidate: line count, document count, PMI over its tokens,
per-corpus counts and up to 8 example lines from different documents.
"""
import argparse
import collections
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

sys.path.insert(0, C.LEXTOOLS)
import lexlib  # noqa: E402

CASE = {'を', 'が', 'に', 'で', 'と', 'へ', 'から', 'より', 'まで'}
JOIN = CASE | {'の'}
CONTENT = {'n', 'v1', 'v5', 'vs', 'vk', 'adj', 'na', 'adv'}
PRED = {'v1', 'v5', 'vs', 'vk', 'adj'}
DOC_BLOCK = {'numreg': 300, 'gc': 300, 'caption': 400, 'game2': 400}
# frequent function-like content lemmas that make no collocation by themselves
WEAK = {'為る', '有る', '居る', '成る', '事', '物', '其れ', '此れ', '彼', '所', '様', '時', '方', '中', '上', '人',
        '言う', '思う', '来る', '行く', '見る', '出来る', '良い', '無い', '其の', '此の', '今', '一', '何', '私', '自分'}


def sources(excl):
    for f in C.fiction_files():
        doc = 'fic:' + os.path.basename(f)
        for s in open(f, encoding='utf-8', errors='replace'):
            s = s.strip()
            if s and s not in excl:
                yield 'fiction', doc, s
    for name, path in C.CORPORA.items():
        for i, s in enumerate(open(path, encoding='utf-8', errors='replace')):
            s = s.strip()
            if s and s not in excl:
                yield name, f'{name}:{i // DOC_BLOCK[name]}', s


def tok_class(t):
    if t.pos in CONTENT and t.pos != 'num':
        return 'C'
    if t.pos1 == '助詞' and t.surf in JOIN:
        return 'J'
    return None


def spans(toks):
    """(start, end, kind) of candidate spans over the token list."""
    cls = [tok_class(t) for t in toks]
    n = len(toks)
    for i in range(n):
        if cls[i] != 'C' or toks[i].pos == 'pn':
            continue
        for j in range(i + 1, min(n, i + 4)):
            if cls[j] is None or toks[j].pos == 'pn':
                break
            if cls[j] != 'C':
                continue
            seg = toks[i:j + 1]
            lem = [t.lemma for t in seg if tok_class(t) == 'C']
            if all(x in WEAK for x in lem):
                continue
            a, b = seg[0], seg[-1]
            if len(seg) == 3 and seg[1].surf in CASE and a.pos == 'n' and b.pos in PRED:
                kind = 'vp'
            elif len(seg) == 3 and seg[1].surf == 'の' and a.pos == 'n' and b.pos == 'n':
                kind = 'nn'
            elif len(seg) == 2 and a.pos == 'n' and b.pos == 'n':
                kind = 'cmp'
            elif len(seg) == 2 and a.pos == 'adv' and b.pos in PRED:
                kind = 'av'
            else:
                kind = 'seq'
            yield i, j + 1, kind


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--min', type=int, default=3)
    ap.add_argument('--out', default=C.WORK + '/cands.json')
    a = ap.parse_args()
    excl = set(open(C.MEAS + '/exclude.txt', encoding='utf-8').read().split('\n')) | C.eval_lines()
    uni = collections.Counter()
    ntok = 0
    count = collections.Counter()
    docs = collections.defaultdict(set)
    corp = collections.defaultdict(collections.Counter)
    ex = collections.defaultdict(list)
    kinds = {}
    seen = set()
    nlines = 0
    for name, doc, s in sources(excl):
        if (name, s) in seen:
            continue
        seen.add((name, s))
        nlines += 1
        toks = lexlib.analyze(s)
        # character offsets of tokens
        offs, at = [], 0
        for t in toks:
            p = s.find(t.surf, at)
            if p < 0:
                p = at
            offs.append(p)
            at = p + len(t.surf)
        for t in toks:
            if tok_class(t) == 'C':
                uni[t.lid] += 1
                ntok += 1
        for i, j, kind in spans(toks):
            key = ' '.join(t.lid if tok_class(t) == 'C' else t.surf for t in toks[i:j])
            count[key] += 1
            corp[key][name] += 1
            kinds[key] = kind
            d = docs[key]
            if len(d) < 50:
                d.add(doc)
            e = ex[key]
            if len(e) < 8 and doc not in {x[1] for x in e} and len(s) <= 120:
                e.append((name, doc, s, offs[i], offs[j - 1] + len(toks[j - 1].surf)))
    print('lines', nlines, 'spans', len(count), file=sys.stderr)
    out = []
    for key, c in count.items():
        if c < a.min or len(docs[key]) < 2:
            continue
        parts = key.split(' ')
        lp = sum(math.log(uni[p] / ntok) for p in parts if p in uni)
        pmi = math.log(c / ntok) - lp
        out.append({'key': key, 'kind': kinds[key], 'n': c, 'docs': len(docs[key]), 'pmi': round(pmi, 2),
                    'corp': dict(corp[key]), 'ex': ex[key]})
    out.sort(key=lambda x: -x['n'])
    json.dump(out, open(a.out, 'w', encoding='utf-8'), ensure_ascii=False)
    print('candidates', len(out), collections.Counter(x['kind'] for x in out), file=sys.stderr)


if __name__ == '__main__':
    main()
