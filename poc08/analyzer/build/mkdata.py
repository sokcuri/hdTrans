#!/usr/bin/env python3
"""Build the analyzer model TSVs (data/) from the UniDic lexicon and silver tags.

usage: mkdata.py --lex LEX_CSV --unidic DICDIR --silver SILVER --out DATA_DIR [--policy P] [--lexmin N]

  LEX_CSV  UniDic lex.csv (cwj 3.1.x); every lexicon row we write is a row of it
  DICDIR   UniDic MeCab dictionary folder (char.def, unk.def)
  SILVER   output of tag.py; used only for aggregate counts (class bigrams, group frequencies, selection)
  policy   which UniDic groups that never occur in the silver data are kept:
           seen | closed (seen + every non-noun group) | common (closed + common nouns) | all

Costs are integers in units of 0.01 nat (cost = -100 ln p).
"""
import argparse, collections, csv, math, os, sys

FUNC = {'助詞', '助動詞'}
SCALE = 100.0


def ud_name(p1, p2, p3, p4):
    return '-'.join(x for x in (p1, p2, p3, p4) if x != '*')


def lexicalizable(p1, p2):
    return p1 in FUNC or p2 == '非自立可能' or p1 == '接尾辞'


def katakana(s):
    return ''.join(chr(ord(c) + 0x60) if 0x3041 <= ord(c) <= 0x3096 else c for c in s)


def cost_of(p):
    return int(round(-SCALE * math.log(max(p, 1e-30))))


def read_silver(path):
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            toks = []
            for t in line.rstrip('\n').split('\x1f'):
                f = t.split('\x1e')
                if len(f) >= 12:
                    toks.append(f)
            yield toks


def char_categories(dicdir):
    cats, ranges = [], []
    with open(os.path.join(dicdir, 'char.def'), encoding='utf-8') as fh:
        for line in fh:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            p = line.split()
            if p[0].startswith('0x'):
                a, _, b = p[0].partition('..')
                ranges.append((int(a, 16), int(b or a, 16), p[1], p[2:]))
            else:
                cats.append((p[0], int(p[1]), int(p[2]), int(p[3])))
    return cats, ranges


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lex', required=True)
    ap.add_argument('--unidic', required=True)
    ap.add_argument('--silver', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--policy', default='closed')
    ap.add_argument('--lexmin', type=int, default=30, help='min count to keep a lexicalized class')
    a = ap.parse_args()
    top = a.out
    a.out = os.path.join(top, 'model')
    os.makedirs(a.out, exist_ok=True)

    # --- UniDic lexicon into paradigm groups ---------------------------------------------------
    # group key: lemma, lForm, pos1-4, cType, orthBase; forms: (cForm, surface, kana)
    groups = collections.OrderedDict()
    rowkey = {}
    with open(a.lex, encoding='utf-8') as fh:
        for r in csv.reader(fh):
            if r[0] != r[12]:  # NFKC duplicate keys; the normalizer folds width instead
                continue
            g = (r[11], r[10], r[4], r[5], r[6], r[7], r[8], r[14])
            forms = groups.setdefault(g, [])
            form = (r[9], r[0], r[24])
            if form not in forms:
                forms.append(form)
            rowkey[(r[0], r[4], r[5], r[6], r[7], r[8], r[9], r[11], r[24])] = g
    print('groups', len(groups), file=sys.stderr)

    # --- silver counts ---------------------------------------------------------------------------
    gcount = collections.Counter()
    fcount = collections.Counter()  # (group, cForm, surface, kana)
    extra = 0
    lexcand = collections.Counter()
    sents = []
    for toks in read_silver(a.silver):
        seq = []
        for f in toks:
            ws, s, p1, p2, p3, p4, ct, cf, lf, lemma, kana, unk = f[:12]
            if p1 == '空白':
                continue
            if unk:
                seq.append(('U', s, p1, p2, p3, p4, ct, cf))
                continue
            k = (s, p1, p2, p3, p4, ct, cf, lemma, kana)
            g = rowkey.get(k)
            if g is None:  # teacher row missing in the lexicon release: add it as its own group
                g = (lemma, lf, p1, p2, p3, p4, ct, s if ct == '*' else s)
                forms = groups.setdefault(g, [])
                if (cf, s, kana) not in forms:
                    forms.append((cf, s, kana))
                    extra += 1
                rowkey[k] = g
            gcount[g] += 1
            fcount[(g, cf, s, kana)] += 1
            if lexicalizable(p1, p2):
                lexcand[(ud_name(p1, p2, p3, p4), ct, lemma)] += 1
            seq.append(('W', g, cf))
        sents.append(seq)
    print('silver sentences', len(sents), 'added rows', extra, file=sys.stderr)

    lexkeep = {k for k, n in lexcand.items() if n >= a.lexmin}

    def cls_key(ud, ct, cf, lemma, p1, p2):
        lx = lemma if lexicalizable(p1, p2) and (ud, ct, lemma) in lexkeep else '*'
        return (ud, ct, cf, lx)

    # --- selection -----------------------------------------------------------------------------------
    def keep(g):
        if gcount[g] > 0:
            return True
        p1, p2 = g[2], g[3]
        if a.policy == 'seen':
            return False
        if a.policy == 'all':
            return True
        if p1 == '補助記号' and p2 == 'ＡＡ':
            return False
        if p1 == '名詞' and p2 == '固有名詞':
            return False
        if a.policy == 'closed':
            return p1 != '名詞'
        return True  # common

    sel = [g for g in groups if keep(g)]
    print('selected groups', len(sel), file=sys.stderr)

    # --- classes and transitions --------------------------------------------------------------------
    classes = {('BOS/EOS', '*', '*', '*'): 0}
    ccount = collections.Counter()

    def cid(key):
        i = classes.get(key)
        if i is None:
            i = classes[key] = len(classes)
        return i

    bigram = collections.Counter()
    unkcount = collections.Counter()
    for seq in sents:
        prev = 0
        for t in seq:
            if t[0] == 'U':
                _, s, p1, p2, p3, p4, ct, cf = t
                c = cid(cls_key(ud_name(p1, p2, p3, p4), ct, cf, '*', p1, p2))
                unkcount[c] += 1
            else:
                _, g, cf = t
                c = cid(cls_key(ud_name(*g[2:6]), g[6], cf, g[0], g[2], g[3]))
            ccount[c] += 1
            bigram[(prev, c)] += 1
            prev = c
        bigram[(prev, 0)] += 1
        ccount[0] += 1
    for g in sel:
        ud = ud_name(*g[2:6])
        for cf, s, kana in groups[g]:
            cid(cls_key(ud, g[6], cf, g[0], g[2], g[3]))

    # unknown-word classes: every class UniDic's unk.def can emit
    cats, ranges = char_categories(a.unidic)
    unkdef = []
    with open(os.path.join(a.unidic, 'unk.def'), encoding='utf-8') as fh:
        for r in csv.reader(fh):
            if not r:
                continue
            cat, p1, p2, p3, p4, ct, cf = r[0], r[4], r[5], r[6], r[7], r[8], r[9]
            c = cid(cls_key(ud_name(p1, p2, p3, p4), ct, cf, '*', p1, p2))
            unkdef.append((cat, c))

    ntok = sum(v for k, v in ccount.items() if k != 0)
    nclass = len(classes)
    left = collections.Counter()
    for (x, y), n in bigram.items():
        left[x] += n
    K = 50.0
    uni = [0.0] * nclass
    for c in range(nclass):
        uni[c] = (ccount[c] + 1.0) / (ntok + ccount[0] + nclass)
    with open(os.path.join(a.out, 'classes.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-classes\nid\tud\tctype\tcform\tlemma\tcount\n')
        for key, i in sorted(classes.items(), key=lambda kv: kv[1]):
            o.write(f'{i}\t{key[0]}\t{key[1]}\t{key[2]}\t{key[3]}\t{ccount[i]}\n')
    with open(os.path.join(a.out, 'backoff.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-backoff\nclass\tleft\tright\n')
        for c in range(nclass):
            o.write(f'{c}\t{cost_of(K / (left[c] + K))}\t{cost_of(uni[c])}\n')
    with open(os.path.join(a.out, 'conn.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-conn\nleft\tright\tcost\n')
        for (x, y), n in sorted(bigram.items()):
            p = (n + K * uni[y]) / (left[x] + K)
            o.write(f'{x}\t{y}\t{cost_of(p)}\n')

    # --- templates and lexicon ------------------------------------------------------------------------
    pcount = collections.Counter()  # tokens per (ud, ctype, lexlemma) for emission estimates
    pgroups = collections.Counter()

    def pkey(g):
        ud = ud_name(*g[2:6])
        lx = cls_key(ud, g[6], '*', g[0], g[2], g[3])[3]
        return (ud, g[6], lx)

    for g in sel:
        pcount[pkey(g)] += gcount[g]
        pgroups[pkey(g)] += 1
    templates = {(): 0}
    rows = []
    for g in sel:
        # one reading per (cForm, surface): the most frequent in the silver data, else the first listed
        best = collections.OrderedDict()
        for cf, s, kana in groups[g]:
            k = (cf, s)
            n = fcount[(g, cf, s, kana)]
            if k not in best or n > best[k][1]:
                best[k] = (kana, n)
        forms = [(cf, s, kana) for (cf, s), (kana, n) in best.items()]
        # split by first character when the forms share no prefix (来る: き/く/こ)
        parts = collections.OrderedDict()
        common = os.path.commonprefix([s for _, s, _ in forms])
        if common:
            parts[''] = forms
        else:
            for f in forms:
                parts.setdefault(f[1][:1], []).append(f)
        for _, fs in parts.items():
            stem = os.path.commonprefix([s for _, s, _ in fs])
            rstem = os.path.commonprefix([k for _, _, k in fs])
            if g[6] == '*' and len(fs) == 1:
                t = ()
            else:
                t = tuple(sorted((cf, s[len(stem):], k[len(rstem):]) for cf, s, k in fs))
            tid = templates.setdefault(t, len(templates))
            pk = pkey(g)
            p = (gcount[g] + 0.5) / (pcount[pk] + 0.5 * pgroups[pk])
            rows.append((stem, rstem, g[0], ud_name(*g[2:6]), g[6], tid, cost_of(p), gcount[g]))
    rows.sort()
    with open(os.path.join(a.out, 'templates.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-templates\n# forms: cform,suffix,reading-suffix separated by spaces; 0 = uninflected\n')
        o.write('id\tforms\n')
        for t, i in sorted(templates.items(), key=lambda kv: kv[1]):
            o.write(f'{i}\t' + ' '.join(f'{cf},{s},{k}' for cf, s, k in t) + '\n')
    with open(os.path.join(a.out, 'lexicon.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-lexicon\nstem\treading\tlemma\tud\tctype\ttemplate\tcost\n')
        for stem, rstem, lemma, ud, ct, tid, c, n in rows:
            o.write(f'{stem}\t{rstem}\t{lemma}\t{ud}\t{ct}\t{tid}\t{c}\n')
    print('lexicon rows', len(rows), 'templates', len(templates), 'classes', nclass,
          'transitions', len(bigram), file=sys.stderr)

    # --- character categories and unknown words ---------------------------------------------------------
    with open(os.path.join(a.out, 'chardef.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-chardef\n# category rows: name invoke group length; range rows: first last categories\n')
        for name, inv, grp, ln in cats:
            o.write(f'cat\t{name}\t{inv}\t{grp}\t{ln}\n')
        for lo, hi, c, more in ranges:
            o.write(f'range\t{lo:04X}\t{hi:04X}\t{" ".join([c] + more)}\n')
    with open(os.path.join(a.out, 'unk.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-unk\ncategory\tclass\tcost\tperchar\n')
        for cat, c in unkdef:
            p = (unkcount[c] + 1.0) / (ccount[c] + 2.0)
            o.write(f'{cat}\t{c}\t{cost_of(p) + 400}\t0\n')

    # --- id tables (IR.md §3) -----------------------------------------------------------------------
    uds = sorted({k[0] for k in classes} - {'BOS/EOS'})
    with open(os.path.join(top, 'udpos.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! udpos\n# UniDic POS (pos1-pos4, * dropped); id 0 is the sentence boundary\nid\tud\n0\tBOS/EOS\n')
        for i, u in enumerate(uds, 1):
            o.write(f'{i}\t{u}\n')
    cts = sorted({k[1] for k in classes} - {'*'})
    cfs = sorted({k[2] for k in classes} - {'*'})
    with open(os.path.join(top, 'conj.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! conj\n# UniDic cType and cForm names; id 0 = none\nkind\tid\tname\n')
        for i, c in enumerate(cts, 1):
            o.write(f'ctype\t{i}\t{c}\n')
        for i, c in enumerate(cfs, 1):
            o.write(f'cform\t{i}\t{c}\n')
    params = os.path.join(a.out, 'params.tsv')
    if not os.path.exists(params):
        with open(params, 'w', encoding='utf-8') as o:
            o.write('#! analyzer-params\nkey\tvalue\njoin_cost\t1.5\nstutter_cost\t6\nstutter_ud\t感動詞-フィラー\n')


if __name__ == '__main__':
    main()
