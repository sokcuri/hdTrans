#!/usr/bin/env python3
"""Decompose the 0.7 ending rows (base.tsv, POS class 5) into UniDic morphemes and check them against aux.tsv.

usage: endings.py BASE_TSV POS_SCHEMA AUX_TSV [--dump OUT.tsv] [--inventory] [--strict]

--strict picks the host without looking at aux.tsv (family first, then the fewest morphemes).

Each kana key is attached to host words (verb, adjective, noun forms); the analysis that keeps the host whole
and folds best is taken. A key folds when every morpheme after the host matches an aux.tsv row, and the host's
inflected form (when the key starts inside the inflection) matches a form row. Keys V/I/D are bare stem forms
of the 0.7 dictionary: they fold through the form rows.
"""
import collections
import csv
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import auxmatch  # noqa: E402

HOSTS = {
    'S': [('書か', '書く'), ('書き', '書く'), ('書く', '書く'), ('書け', '書く'), ('書こ', '書く'), ('書い', '書く'),
          ('書いて', '書く'), ('書', '書く'), ('言', '言う'), ('食べ', '食べる'), ('食べる', '食べる'), ('食べて', '食べる'),
          ('読ん', '読む'), ('読んで', '読む'), ('読', '読む')],
    'T': [('高', '高い'), ('高い', '高い'), ('高く', '高い'), ('静か', '静か')],
    'U': [('学生', '学生'), ('静か', '静か'), ('高い', '高い')],
}
ALL = [h for v in HOSTS.values() for h in v]


def load_rows(base, schema):
    cls = {r['code']: r['class'] for r in csv.DictReader(open(schema, encoding='utf-8'), delimiter='\t')}
    out = []
    with open(base, encoding='utf-8') as fh:
        hdr = None
        for line in fh:
            if line.startswith('#'):
                continue
            p = line.rstrip('\n').split('\t')
            if hdr is None:
                hdr = p
                continue
            if cls.get(p[2]) == '5':
                out.append(p)
    return out


def analyse(tagger, host, lemma, key):
    s = host + key
    toks = []
    for w in tagger(s):
        f = w.feature
        ud = '-'.join(x for x in (f.pos1, f.pos2, f.pos3, f.pos4) if x and x != '*')
        toks.append((w.surface, f.lemma if f.lemma and f.lemma != '*' else w.surface, ud,
                     f.cForm if f.cForm not in (None, '*') else '', f.cType if f.cType not in (None, '*') else ''))
    # locate the host token
    n = len(host)
    acc = 0
    for i, t in enumerate(toks):
        start = acc
        acc += len(t[0])
        if acc >= n:
            break
    else:
        return None
    host_tok = toks[i]
    if host_tok[1] != auxmatch.LEMMA_OF.get(lemma, lemma) and not (start < n and host_tok[1] == lemma):
        return None
    if start >= n:
        return None
    inner = acc > n          # the key starts inside the host token's inflection
    morphs = list(toks[i + 1:])
    return {'host': host, 'inner': inner, 'hostform': host_tok[3], 'hosttok': host_tok, 'morphs': morphs, 'all': toks}


def main():
    import fugashi
    import unidic
    base, schema, auxf = sys.argv[1:4]
    dump = sys.argv[sys.argv.index('--dump') + 1] if '--dump' in sys.argv else None
    tagger = fugashi.Tagger('-d ' + unidic.DICDIR + ' -r ' + unidic.DICDIR + '/mecabrc')
    table = auxmatch.load(auxf)
    rows = load_rows(base, schema)
    keys = collections.OrderedDict()
    for r in rows:
        keys.setdefault((r[0], r[2][0]), []).append(r)
    res = {}
    inv = collections.Counter()
    unk = collections.Counter()
    for (key, fam), rs in keys.items():
        if key.isascii():
            ok = auxmatch.bare_form_folds(table, key)
            res[(key, fam)] = ('bare', ok, [], None)
            continue
        best = None
        for host, lemma in HOSTS.get(fam, []) + [h for h in ALL if h not in HOSTS.get(fam, [])]:
            a = analyse(tagger, host, lemma, key)
            if a is None or not a['morphs'] and not a['inner']:
                continue
            rs = auxmatch.roles(table, a['all'])
            off = len(a['all']) - len(a['morphs'])
            miss = [m for k, m in enumerate(a['morphs']) if rs[off + k] is None]
            formok = (not a['inner']) or auxmatch.form_folds(table, a['hostform'])
            fam_pref = 0 if (host, lemma) in HOSTS.get(fam, []) else 1
            # a host whose own word changes class with the key (書け+る → 書ける) is a misreading
            if a['hosttok'][4] and lemma.endswith(('く', 'む')) and a['hosttok'][4].startswith('下一段'):
                fam_pref += 2
            score = (len(miss) + (0 if formok else 1), fam_pref, len(a['morphs']))
            if '--strict' in sys.argv:
                score = (fam_pref, len(a['morphs']), len(miss))
            if best is None or score < best[0]:
                best = (score, a, miss, formok)
        if best is None:
            res[(key, fam)] = ('nohost', False, [], None)
            continue
        score, a, miss, formok = best
        ok = not miss and formok
        res[(key, fam)] = ('ok' if ok else 'miss', ok, miss, a)
        for m in a['morphs']:
            inv[(m[1], m[2].split('-')[0], m[3].split('-')[0] if m[2].startswith(('助動詞', '動詞', '形容詞')) else '')] += len(rs)
        for m in miss:
            unk[(m[1], m[2], m[0])] += len(rs)
    nrows = sum(len(v) for v in keys.values())
    okrows = sum(len(keys[k]) for k, v in res.items() if v[1])
    okkeys = sum(1 for v in res.values() if v[1])
    print(f'ending rows {nrows}, keys {len(keys)}; folded rows {okrows} ({okrows / nrows:.1%}), '
          f'keys {okkeys} ({okkeys / len(keys):.1%})')
    st = collections.Counter(v[0] for v in res.values())
    print('key status', dict(st))
    print('distinct morphemes after hosts', len(inv))
    if '--inventory' in sys.argv:
        for k, c in inv.most_common():
            print('INV', c, *k, sep='\t')
    print('top unmatched morphemes:')
    for k, c in unk.most_common(60):
        print('  ', c, *k, sep='\t')
    if dump:
        with open(dump, 'w', encoding='utf-8') as fh:
            fh.write('key\tfam\trows\tstatus\thost\tmorphs\tmissing\n')
            for (key, fam), v in res.items():
                a = v[3]
                ms = '‖'.join(f'{m[0]}¦{m[1]}¦{m[2]}¦{m[3]}' for m in a['morphs']) if a else ''
                if a and a['inner']:
                    ms = f'[{a["hostform"]}]‖' + ms if ms else f'[{a["hostform"]}]'
                fh.write(f'{key}\t{fam}\t{len(keys[(key, fam)])}\t{v[0]}\t{a["host"] if a else ""}\t{ms}\t'
                         f'{"‖".join(m[0] for m in v[2])}\n')


if __name__ == '__main__':
    main()
