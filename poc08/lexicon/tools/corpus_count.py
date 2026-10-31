#!/usr/bin/env python3
"""Count lemma ids over development corpora, keep example lines and mine collocations for review.

usage: corpus_count.py --out DIR --corpus NAME=GLOB[:REFGLOB] ... [--exclude-tsv GLOB] [--heldout NAME=TSVGLOB]

--exclude-tsv names TSV files with a `ja` column; their lines are left out of every development count.
--heldout counts lemma ids of TSV files with `ja` and `scored` columns (aggregate counts only).
Collocation counts are reviewer evidence only; they are never runtime data.
"""
import argparse
import collections
import glob
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexlib  # noqa: E402

CASE = {'が': 'subj', 'を': 'obj', 'に': 'dat', 'で': 'loc', 'から': 'from', 'と': 'with', 'へ': 'dat', 'まで': 'to',
        'より': 'from'}
STOP = {'補助記号', '記号'}


def read_tsv(path):
    with open(path, encoding='utf-8') as f:
        hdr = f.readline().rstrip('\n').split('\t')
        for line in f:
            p = line.rstrip('\n').split('\t')
            yield dict(zip(hdr, p + [''] * (len(hdr) - len(p))))


def h(s):
    return int(hashlib.md5(s.encode('utf-8')).hexdigest()[:12], 16)


class Keep:
    """Deterministic sample: the k lines with the smallest hash."""

    def __init__(self, k):
        self.k = k
        self.items = []

    def add(self, key, item):
        if len(self.items) < self.k:
            self.items.append((key, item))
            return
        mx = max(range(len(self.items)), key=lambda i: self.items[i][0])
        if key < self.items[mx][0]:
            self.items[mx] = (key, item)


def colloc(toks, i, out):
    """Collocation facts for the token at i: arguments of predicates, heads of modifiers, verbs of nouns."""
    t = toks[i]
    p = t.pos
    if p in lexlib.PRED or (p == 'n' and t.pos2 == '普通名詞' and i + 1 < len(toks) and toks[i + 1].lemma in ('する', '為る')):
        j = i - 1
        seen = 0
        while j >= 1 and seen < 4:
            a, b = toks[j - 1], toks[j]
            if b.pos1 in STOP or (b.pos in lexlib.PRED and b.pos1 == '動詞' and b.pos2 != '非自立可能'):
                break
            if b.pos1 == '助詞' and b.pos2 in ('格助詞', '係助詞') and a.pos in ('n', 'pn', 'num', 'suf'):
                role = CASE.get(b.surf, 'top' if b.surf in ('は', 'も') else None)
                if role:
                    out[f'{role}={a.lemma}'] += 1
                    seen += 1
                j -= 2
                continue
            j -= 1
    if p in ('adj', 'na', 'adn') or (p in lexlib.PRED and t.cform.startswith('連体形')):
        k = i + 1
        if p == 'na' and k < len(toks) and toks[k].surf in ('な', 'の'):
            k += 1
        if k < len(toks) and toks[k].pos in ('n', 'pn') and (p != 'adj' or t.cform.startswith('連体形') or p in ('adn', 'na')):
            out[f'head={toks[k].lemma}'] += 1
    if p == 'n':
        if i + 2 < len(toks) and toks[i + 1].pos1 == '助詞' and toks[i + 2].pos in lexlib.PRED:
            role = CASE.get(toks[i + 1].surf, 'top' if toks[i + 1].surf in ('は', 'も') else None)
            if role:
                out[f'{role}-of={toks[i + 2].lemma}'] += 1
        if i >= 2 and toks[i - 1].surf == 'の' and toks[i - 2].pos in ('n', 'pn'):
            out[f'mod={toks[i - 2].lemma}の'] += 1
        elif i >= 1 and (toks[i - 1].pos in ('adj', 'adn', 'na') or toks[i - 1].cform.startswith('連体形')):
            pv = toks[i - 1]
            out[f'mod={pv.lemma}' if pv.pos else f'mod=(clause {pv.surf})'] += 1
    if p == 'adv':
        for k in range(i + 1, min(i + 4, len(toks))):
            if toks[k].pos in lexlib.PRED:
                out[f'pred={toks[k].lemma}'] += 1
                break
    if p == 'suf' and i >= 1:
        out[f'after={toks[i - 1].lemma}'] += 1
    if i + 1 < len(toks):
        nx = toks[i + 1]
        out[f'next={nx.surf if nx.pos is None else nx.lemma}'] += 1
    if i >= 1:
        pv = toks[i - 1]
        out[f'prev={pv.surf if pv.pos is None else pv.lemma}'] += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--corpus', action='append', default=[])
    ap.add_argument('--exclude-tsv', action='append', default=[])
    ap.add_argument('--heldout', action='append', default=[])
    ap.add_argument('--examples', type=int, default=12)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    excl = set()
    for g in a.exclude_tsv:
        for f in glob.glob(g):
            for r in read_tsv(f):
                if r.get('ja', '').strip():
                    excl.add(r['ja'].strip())
    print('excluded lines', len(excl), file=sys.stderr)

    cnt = collections.Counter()
    per = collections.defaultdict(collections.Counter)
    forms = collections.defaultdict(collections.Counter)
    meta = {}
    coll = collections.defaultdict(collections.Counter)
    ex = collections.defaultdict(lambda: Keep(a.examples))
    totals = collections.Counter()
    nlines = collections.Counter()
    dropped = 0
    for spec in a.corpus:
        name, rest = spec.split('=', 1)
        jg, _, rg = rest.partition(':')
        for f in sorted(glob.glob(jg)):
            refs = None
            if rg:
                rf = sorted(glob.glob(rg))
                refs = open(rf[0], encoding='utf-8').read().split('\n') if rf else None
            for ln, line in enumerate(open(f, encoding='utf-8', errors='replace').read().split('\n')):
                line = line.strip()
                if not line:
                    continue
                if line in excl:
                    dropped += 1
                    continue
                nlines[name] += 1
                toks = lexlib.analyze(line)
                seen = set()
                for i, t in enumerate(toks):
                    if t.pos1 in STOP:
                        continue
                    lid = t.lid
                    cnt[lid] += 1
                    per[lid][name] += 1
                    forms[lid][t.surf] += 1
                    totals[(name, t.pos or 'gram')] += 1
                    if lid not in meta:
                        meta[lid] = (t.lemma, t.yomi, t.pos or '-', t.pos1, t.pos2, t.ctype)
                    if t.pos:
                        colloc(toks, i, coll[lid])
                    if lid not in seen and 6 <= len(line) <= 80 and t.pos:
                        seen.add(lid)
                        ref = refs[ln].strip() if refs and ln < len(refs) else ''
                        ex[lid].add(h(lid + line), (name, line, ref))
            print(name, f, nlines[name], file=sys.stderr, flush=True)
    print('dropped (held-out) lines', dropped, file=sys.stderr)

    with open(os.path.join(a.out, 'lemmas.tsv'), 'w', encoding='utf-8') as o:
        names = [s.split('=', 1)[0] for s in a.corpus]
        o.write('lid\tja\tyomi\tpos\tpos1\tpos2\tctype\ttotal\t' + '\t'.join(names) + '\tforms\n')
        for lid, n in cnt.most_common():
            m = meta[lid]
            fs = ' '.join(f'{k}:{v}' for k, v in forms[lid].most_common(6))
            o.write('\t'.join([lid, *m, str(n)] + [str(per[lid][x]) for x in names] + [fs]) + '\n')
    with open(os.path.join(a.out, 'totals.tsv'), 'w', encoding='utf-8') as o:
        o.write('corpus\tpos\ttokens\n')
        for (c, p), n in sorted(totals.items()):
            o.write(f'{c}\t{p}\t{n}\n')
        for c, n in sorted(nlines.items()):
            o.write(f'{c}\tlines\t{n}\n')
    keep = {lid for lid, n in cnt.items() if n >= 5 and meta[lid][2] != '-'}
    json.dump({lid: [x for _, x in sorted(ex[lid].items)] for lid in keep if lid in ex},
              open(os.path.join(a.out, 'examples.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    json.dump({lid: coll[lid].most_common(40) for lid in keep if lid in coll},
              open(os.path.join(a.out, 'colloc.json'), 'w', encoding='utf-8'), ensure_ascii=False)

    # held-out: aggregate counts only
    for spec in a.heldout:
        name, g = spec.split('=', 1)
        hc = collections.Counter()
        for f in sorted(glob.glob(g)):
            for r in read_tsv(f):
                line = r.get('ja', '').strip()
                if not line:
                    continue
                scope = 'scored' if r.get('scored') == '1' else 'context'
                for t in lexlib.analyze(line):
                    if t.pos1 in STOP:
                        continue
                    hc[(scope, t.lid)] += 1
        with open(os.path.join(a.out, f'heldout-{name}.tsv'), 'w', encoding='utf-8') as o:
            o.write('scope\tlid\tcount\n')
            for (s, lid), n in sorted(hc.items(), key=lambda x: -x[1]):
                o.write(f'{s}\t{lid}\t{n}\n')


if __name__ == '__main__':
    main()
