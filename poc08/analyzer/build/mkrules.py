#!/usr/bin/env python3
"""Build the analyzer tables (data/model/) from the UniDic lexicon and the rule files.

usage: mkrules.py --lex LEX_CSV --unidic DICDIR --data DATA_DIR --dicts DICTS_DIR --lexicon LEXICON_DIR

  LEX_CSV  UniDic lex.csv (cwj 3.1.x); every lexicon row we write is a row of it. Its trained columns
           (left/right context ids, word cost) are not read.
  DICDIR   UniDic MeCab dictionary folder (char.def, unk.def: character categories and unknown-word POS)
  DATA_DIR the analyzer data folder: reads ja/{conn,prefs,classes,lexfilter,lexadd}.tsv, writes model/*.tsv,
           udpos.tsv, conj.tsv
  DICTS_DIR    the 0.7.x dictionaries (repo dicts/): their keys are the feature `key`
  LEXICON_DIR  the translation lexicon (poc08/lexicon/lex/lexicon of the lexicon workstream): feature `known`

No number here is fitted. A path's cost is the vector of preference counts of ja/prefs.tsv, compared rank by
rank (the first rank that differs decides). It is stored as one integer, each rank a base-1000 digit, so the
order is exact while a line has fewer than 1,000 counts in one rank. A connection that ja/conn.tsv forbids
costs more than any vector; it is taken only when no other path exists.
"""
import argparse, collections, csv, os, re, sys

RANK_BASE = 1000
FORBID = 10 ** 17  # above every preference vector (up to six ranks); leaves room for 90 violations in an int64


def ud_name(p):
    return '-'.join(x for x in p if x != '*')


def lexicalized(p1, p2):
    # function words keep their lemma in the class, so rules can name them (の, だ, いる as auxiliary ...)
    return p1 in ('助詞', '助動詞') or p2 == '非自立可能'


def is_hira(c):
    return 0x3041 <= ord(c) <= 0x309F or c == 'ー'


def is_kata(c):
    return 0x30A0 <= ord(c) <= 0x30FF or 0x31F0 <= ord(c) <= 0x31FF or 0xFF66 <= ord(c) <= 0xFF9F


def is_kanji(c):
    o = ord(c)
    return 0x4E00 <= o <= 0x9FFF or 0x3400 <= o <= 0x4DBF or 0xF900 <= o <= 0xFAFF or 0x20000 <= o <= 0x2FFFF or c in '々〆ヶ'


# ---- selectors -------------------------------------------------------------------------------------
# A selector is atoms joined by '&', or several such conjunctions separated by ' / ' (any of them). An atom is
# key=a|b (any alternative) or a bare flag; '!' negates.
# class keys: ud (prefix of pos1-pos4), ctype (prefix), cform (prefix), lemma (exact; function words only), bos
# arc keys: the class keys plus surf (exact), len (surface length), hira, kata, kanji (contains), kana (all kana),
#           kanaspell (all-kana surface of a lemma written with a kanji), goshu, pos1, lemmaany (lemma of any word)
CLASSES = {}  # ja/classes.tsv: @name → lemmas
# paradigms whose forms legitimately change their first letter (だ/な/で, た/だ, 来る, する, ず/ぬ/ん)
IRREGULAR = {'助動詞-ダ', '助動詞-タ', '助動詞-デス', '助動詞-ジャ', '助動詞-ヌ', 'カ行変格', 'サ行変格'}
VOICED = dict(zip('カキクケコサシスセソタチツテトハヒフヘホ', 'ガギグゲゴザジズゼゾダヂヅデドバビブベボ'))
KEYS = set()  # surface keys of the 0.7.x dictionaries
KNOWN = set()  # (lemma, hiragana reading) of the translation lexicon


def hira_of(kata):
    return ''.join(chr(ord(c) - 0x60) if 0x30A1 <= ord(c) <= 0x30F6 else c for c in kata)


class Atom:
    def __init__(self, text):
        t = text.strip()
        self.neg = t.startswith('!')
        if self.neg:
            t = t[1:]
        if '=' in t:
            k, v = t.split('=', 1)
            self.key, self.vals = k.strip(), []
            for x in v.strip().split('|'):
                if x.startswith('@'):
                    if x not in CLASSES:
                        sys.exit(f'unknown class {x}')
                    self.vals.extend(CLASSES[x])
                else:
                    self.vals.append(x)
        else:
            self.key, self.vals = t, None

    def test(self, f):
        k = self.key
        if k == '*':
            r = True
        elif self.vals is None:
            r = bool(f.get(k))
        elif k in ('ud', 'ctype', 'cform'):
            x = f.get(k, '')
            r = any(x.startswith(v) for v in self.vals)
        elif k == 'len':
            r = str(f.get('len', 0)) in self.vals
        else:
            r = f.get(k, None) in self.vals
        return r != self.neg


class Sel:
    # alternatives separated by ' / ', each a conjunction of atoms joined by '&'
    def __init__(self, text):
        self.text = text
        self.alts = [] if text in ('*', '-', '') else [[Atom(a) for a in alt.split('&')] for alt in text.split(' / ')]
        self.atoms = [a for alt in self.alts for a in alt]

    def test(self, f):
        return not self.alts or any(all(a.test(f) for a in alt) for alt in self.alts)


def read_rules(path, need):
    rows = []
    with open(path, encoding='utf-8') as fh:
        header = None
        for line in fh:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            f = line.split('\t')
            if header is None:
                header = f
                continue
            r = dict(zip(header, f))
            for k in need:
                if not r.get(k):
                    sys.exit(f'{path}: row {f[0]} lacks {k}')
            rows.append(r)
    ids = [r['id'] for r in rows]
    dup = [i for i, n in collections.Counter(ids).items() if n > 1]
    if dup:
        sys.exit(f'{path}: duplicate ids {dup}')
    return rows


def out_lemma(g):
    # the lemma the analyzer reports: UniDic's lexeme, unless the written base form uses kanji the lexeme does
    # not (撃つ under 打つ, 撮る under 取る, 帰る under 返る, 兄妹 under 兄弟): then the written form, so that the
    # word the source chose stays visible to the lexicon
    lemma, ob = g[0], g[7]
    if any(is_kanji(c) and c not in lemma for c in ob):
        return ob
    return lemma


def arc_feats_raw(g, cf, s, goshu, row=None):
    lemma = g[0]
    ud = ud_name(g[2:6])
    f = {'ud': ud, 'ctype': '' if g[6] == '*' else g[6], 'cform': '' if cf == '*' else cf,
         'lemma': lemma, 'bos': False}
    f['surf'] = s
    f['len'] = len(s)
    f['hira'] = all(is_hira(c) for c in s)
    f['kata'] = all(is_kata(c) for c in s)
    f['kana'] = all(is_hira(c) or is_kata(c) for c in s)
    f['kanji'] = any(is_kanji(c) for c in s)
    f['alnum'] = all(c.isascii() and c.isalnum() or 0xFF10 <= ord(c) <= 0xFF19 or 0xFF21 <= ord(c) <= 0xFF3A
                     or 0xFF41 <= ord(c) <= 0xFF5A for c in s)
    f['kanaspell'] = f['kana'] and any(is_kanji(c) for c in lemma)
    # a hiragana form of a word whose lemma is not written in hiragana (下 as した, a name as ことは)
    f['hiraspell'] = f['hira'] and not all(is_hira(c) or c in '-' for c in lemma)
    # the form's kanji are not the lemma's (当に for 正に)
    f['variant'] = any(is_kanji(c) for c in lemma) and any(is_kanji(c) and c not in lemma for c in s)
    f['key'] = s in KEYS or (g[6] != '*' and g[7] in KEYS)  # inflected words by their written base form
    f['basekey'] = bool(row) and (row[14] in KEYS or lemma.split('-')[0] in KEYS)
    f['goshu'] = goshu
    f['pos1'] = g[2]
    # UniDic word-form fields: a word-initial voiced (rendaku) form, which only ends a compound (ごめ for 込む);
    # a short or altered word form of the lemma (して for そして)
    # a clipped spelling of a longer lemma: okurigana omitted (持 for 持ち) or a part of it (彼 for 彼処)
    f['okuri'] = g[6] == '*' and f['kanji'] and lemma != s and lemma.startswith(s)
    if row:
        # a function word spelled other than its lemma's form (が for か, てい for たい, だって for たって)
        # a voiced spelling of a particle (が for か, だって for たって) or an altered conclusive form (てい for たい)
        if g[2] in ('助詞', '助動詞'):
            if row[8] == '*':
                a, b = row[27], row[10]
                f['fvariant'] = len(a) == len(b) and a[1:] == b[1:] and a[0] != b[0] and VOICED.get(b[0]) == a[0] \
                    and row[0] not in ('で', 'だ')
            elif row[9] == '終止形-一般':
                f['fvariant'] = row[26] != row[27]
            # a particle spelled shorter than its lemma (よ for より): a tie-breaker only (けど, ん are common)
            f['pshort'] = row[8] == '*' and g[2] == '助詞' and len(row[27]) < len(row[10])
            # a particle written with other letters than its lemma, not by voicing or clipping (て for と, てえ for て)
            a, b = row[27], row[10]
            f['pvariant'] = row[8] == '*' and g[2] == '助詞' and a != b and not f['pshort'] and not f.get('fvariant') \
                and not (len(a) == len(b) and VOICED.get(b[0]) == a[0] and a[1:] == b[1:])
            # ... and of those, one whose first letter changes other than by a contraction (ん/じゃ/ちゃ/っ)
            f['pletter'] = f['pvariant'] and a[:1] != b[:1] and a[:1] not in 'ンジチッ'
            # a lengthened non-final particle (てえ for て): lengthening belongs to final particles
            f['pelong'] = row[8] == '*' and g[3] != '終助詞' and len(a) > len(b) and a.startswith(b) \
                and all(c in 'アイウエオァィゥェォー' for c in a[len(b):])
        # an inflected auxiliary form that does not start like its base (て for たい, ね for ない)
        if g[2] in ('助動詞', '形容詞', '動詞') and g[6] != '*' and g[6] not in IRREGULAR and not g[6].startswith('文語'):
            stem = row[25][:-1]
            # the spoken ai/oi → ee change (たい → てえ, ない → ねえ, すごい → すげえ) is regular, not a variant
            spoken = row[25].endswith('イ') and row[24][-1:] in ('エ', 'ェ', 'ー')
            f['ivariant'] = bool(stem) and not row[24].startswith(stem) and 'ウ音便' not in row[9] and not spoken \
                and not (row[18] != '*' and row[18] != '基本形')
        # an inflected word in a contracted word form of its lemma (なる as a form of なさる)
        f['wvariant'] = g[6] != '*' and g[6] not in IRREGULAR and not g[6].startswith('文語') and len(row[27]) < len(row[10])
        y = hira_of(row[10])
        f['known'] = (lemma.split('-')[0], y) in KNOWN or (row[14], y) in KNOWN or (y, y) in KNOWN and f['hira']
    f['rendaku'] = bool(row) and row[18] == '濁音形'
    f['shortform'] = bool(row) and row[27] not in ('*', row[10])
    return f


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
    ap.add_argument('--data', required=True)
    ap.add_argument('--known', help='lemma list (ja, yomi) of the translation lexicon: feature known')
    ap.add_argument('--lexicon', help='the translation lexicon folder (lex/lexicon/*.tsv of poc08/lexicon): feature known')
    ap.add_argument('--keys', help='surface keys of the 0.7.x dictionaries and ending tables, one per line: feature key')
    ap.add_argument('--dicts', help='the 0.7.x dictionaries folder (dicts/*.tsv): feature key')
    a = ap.parse_args()
    global KNOWN
    if a.keys:
        KEYS.update(l.rstrip('\n') for l in open(a.keys, encoding='utf-8'))
    if a.dicts:
        import glob, re
        for path in glob.glob(os.path.join(a.dicts, '*.tsv')):
            for line in open(path, encoding='utf-8'):
                k = line.split('\t', 1)[0]
                if not line.startswith('#') and k != 'key' and re.fullmatch(r'[ぁ-ゟ゠-ヿ一-鿿々ー〜～]+', k):
                    KEYS.add(k)
    if a.lexicon:
        import glob
        for path in glob.glob(os.path.join(a.lexicon, '*.tsv')):
            for line in open(path, encoding='utf-8'):
                f = line.rstrip('\n').split('\t')
                if not line.startswith('#') and len(f) >= 9 and f[0] != 'ja':
                    KNOWN.add((f[0], f[1]))
    if a.known:
        for line in open(a.known, encoding='utf-8'):
            f = line.rstrip('\n').split('\t')
            if len(f) >= 2:
                KNOWN.add((f[0], f[1]))
    out = os.path.join(a.data, 'model')
    os.makedirs(out, exist_ok=True)

    for r in read_rules(os.path.join(a.data, 'ja', 'classes.tsv'), ['id', 'members']):
        CLASSES['@' + r['id']] = r['members'].split('|')
    conn_rules = read_rules(os.path.join(a.data, 'ja', 'conn.tsv'), ['id', 'spec', 'left', 'right'])
    lexfilter = read_rules(os.path.join(a.data, 'ja', 'lexfilter.tsv'), ['id', 'spec', 'select'])
    for r in lexfilter:
        r['S'] = Sel(r['select'])
    prefs = read_rules(os.path.join(a.data, 'ja', 'prefs.tsv'), ['id', 'spec', 'rank', 'kind', 'select'])
    for r in conn_rules:
        r['L'], r['R'] = Sel(r['left']), Sel(r['right'])
    nrank = max(int(p['rank']) for p in prefs)
    weight = {k: RANK_BASE ** (nrank - k) for k in range(1, nrank + 1)}
    for p in prefs:
        p['w'] = weight[int(p['rank'])] * int(p.get('weight') or 1)
        p['S'] = Sel(p['select'])
        p['N'] = Sel(p.get('next') or '*')
    kinds = collections.defaultdict(list)
    for p in prefs:
        kinds[p['kind']].append(p)
    # lemmas a class-level selector names are kept in the class (connection rules can only see classes)
    named = set()
    for sel in [r['L'] for r in conn_rules] + [r['R'] for r in conn_rules] + \
            [p['S'] for p in kinds['pair']] + [p['N'] for p in kinds['pair']]:
        for at in sel.atoms:
            if at.key == 'lemma' and at.vals:
                named.update(at.vals)
    unknown = set(kinds) - {'arc', 'pair', 'unk', 'derived', 'join', 'cut'}
    if unknown:
        sys.exit(f'prefs.tsv: unknown kinds {unknown}')

    # --- UniDic lexicon into paradigm groups ---------------------------------------------------------
    # group key: lemma, lForm, pos1-4, cType, orthBase; forms: (cForm, surface, kana), first reading kept
    groups = collections.OrderedDict()
    meta = {}
    dropped = collections.Counter()
    with open(a.lex, encoding='utf-8') as fh:
        for r in csv.reader(fh):
            if r[0] != r[12]:  # NFKC duplicate keys; the normalizer folds width instead
                continue
            if r[4] == '補助記号' and r[5] == 'ＡＡ':
                continue
            g = (r[11], r[10], r[4], r[5], r[6], r[7], r[8], r[14])
            if lexfilter:
                f = arc_feats_raw(g, r[9], r[0], r[16], r)
                hit = next((x['id'] for x in lexfilter if x['S'].test(f)), None)
                if hit:
                    dropped[hit] += 1
                    continue
            forms = groups.setdefault(g, [])
            if not any(f[0] == r[9] and f[1] == r[0] for f in forms):
                forms.append((r[9], r[0], r[24]))
            if g not in meta:
                meta[g] = {'goshu': r[16]}
            meta[(g, r[9], r[0])] = r
    # ja/lexadd.tsv: own rows in UniDic's shape, for words UniDic lacks (the nickname suffix すけ)
    for r in read_rules(os.path.join(a.data, 'ja', 'lexadd.tsv'), ['id', 'surf', 'ud', 'lemma', 'reading']):
        pos = (r['ud'].split('-') + ['*', '*', '*'])[:4]
        ct, cf = r.get('ctype') or '*', r.get('cform') or '*'
        g = (r['lemma'], r['reading'], pos[0], pos[1], pos[2], pos[3], ct, r['surf'])
        groups.setdefault(g, []).append((cf, r['surf'], r['reading']))
        row = [r['surf'], '', '', '', pos[0], pos[1], pos[2], pos[3], ct, cf, r['reading'], r['lemma'], r['surf'], r['reading'],
               r['surf'], r['reading'], r.get('goshu') or '和', '*', '*', '*', '*', '*', '*', '*', r['reading'], r['reading'],
               r['reading'], r['reading']]
        meta[g] = {'goshu': row[16]}
        meta[(g, cf, r['surf'])] = row
        KNOWN.add((r['lemma'], hira_of(r['reading'])))
    print('groups', len(groups), 'dropped by ja/lexfilter.tsv', dict(dropped), file=sys.stderr)

    # --- classes -------------------------------------------------------------------------------------
    classes = {('BOS/EOS', '*', '*', '*'): 0}

    def cid(key):
        i = classes.get(key)
        if i is None:
            i = classes[key] = len(classes)
        return i

    def cls_key(g, cf):
        ud = ud_name(g[2:6])
        return (ud, g[6], cf, g[0] if lexicalized(g[2], g[3]) or g[0] in named else '*')

    for g, forms in groups.items():
        for cf, s, k in forms:
            cid(cls_key(g, cf))
    cats, ranges = char_categories(a.unidic)
    unkdef = []
    seen_cat = set()
    with open(os.path.join(a.unidic, 'unk.def'), encoding='utf-8') as fh:
        for r in csv.reader(fh):
            if not r or r[0] in seen_cat:
                continue  # one POS per category: the first unk.def lists (common noun, symbol, numeral)
            seen_cat.add(r[0])
            key = (ud_name(r[4:8]), r[8], r[9], '*')
            unkdef.append((r[0], cid(key)))

    def class_feats(key, i):
        ud, ct, cf, lx = key
        return {'ud': ud, 'ctype': '' if ct == '*' else ct, 'cform': '' if cf == '*' else cf,
                'lemma': None if lx == '*' else lx, 'bos': i == 0}

    cfeat = [None] * len(classes)
    for key, i in classes.items():
        cfeat[i] = class_feats(key, i)

    # --- connection: forbidden pairs and pair preferences, compiled by the C++ loader -------------------
    def sel_text(sel):
        alts = []
        for alt in sel.alts:
            atoms = []
            for at in alt:
                t = ('!' if at.neg else '') + at.key
                if at.vals is not None:
                    t += '=' + '|'.join(at.vals)
                atoms.append(t)
            alts.append(' & '.join(atoms))
        return ' / '.join(alts) if alts else '*'
    connrules = [(sel_text(r['L']), sel_text(r['R']), FORBID, r['id']) for r in conn_rules]
    connrules += [(sel_text(p['S']), sel_text(p['N']), p['w'], p['id']) for p in kinds['pair']]
    for l, r, c, i in connrules:
        for sel in (l, r):
            for alt in sel.split(' / '):
                for at in alt.split(' & '):
                    k = at.lstrip('!').split('=')[0].strip()
                    if k not in ('ud', 'ctype', 'cform', 'lemma', 'bos', '*'):
                        sys.exit(f'{i}: key {k} cannot be used in a connection selector')
    n = len(classes)
    with open(os.path.join(out, 'connrules.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-connrules\n# compiled from ja/conn.tsv (forbidden pairs) and the pair rows of ja/prefs.tsv\n')
        o.write('left\tright\tcost\tid\n')
        for l, r, c, i in connrules:
            o.write(f'{l}\t{r}\t{c}\t{i}\n')
    with open(os.path.join(out, 'classes.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-classes\nid\tud\tctype\tcform\tlemma\tcount\n')
        for key, i in sorted(classes.items(), key=lambda kv: kv[1]):
            o.write(f'{i}\t{key[0]}\t{key[1]}\t{key[2]}\t{key[3]}\t0\n')
    print('classes', n, 'connection rules', len(connrules), file=sys.stderr)

    # --- lexicon rows: arc preference counts per form ---------------------------------------------------
    def arc_feats(g, cf, s):
        return arc_feats_raw(g, cf, s, meta[g]['goshu'], meta.get((g, cf, s)))

    arc_prefs = kinds['arc']
    templates = {(): 0}
    rows = []
    for g, forms in groups.items():
        # cost per form, then one lexicon row per (first character split, cost)
        costed = []
        for cf, s, k in forms:
            f = arc_feats(g, cf, s)
            c = sum(p['w'] for p in arc_prefs if p['S'].test(f))
            costed.append((cf, s, k, c))
        parts = collections.OrderedDict()
        common = os.path.commonprefix([s for _, s, _, _ in costed])
        for x in costed:
            parts.setdefault(('' if common else x[1][:1], x[3]), []).append(x)
        for (_, c), fs in parts.items():
            stem = os.path.commonprefix([s for _, s, _, _ in fs])
            rstem = os.path.commonprefix([k for _, _, k, _ in fs])
            if g[6] == '*' and len(fs) == 1 and fs[0][1] == stem:
                t = ()
            else:
                t = tuple(sorted((cf, s[len(stem):], k[len(rstem):]) for cf, s, k, _ in fs))
            if not stem:  # a form split off alone cannot have an empty stem
                continue
            tid = templates.setdefault(t, len(templates))
            lx = lexicalized(g[2], g[3]) or g[0] in named
            rows.append((stem, rstem, g[0] if lx else out_lemma(g), ud_name(g[2:6]), g[6], tid, c))
    rows.sort()
    with open(os.path.join(out, 'templates.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-templates\n# forms: cform,suffix,reading-suffix separated by spaces; 0 = uninflected\n')
        o.write('id\tforms\n')
        for t, i in sorted(templates.items(), key=lambda kv: kv[1]):
            o.write(f'{i}\t' + ' '.join(f'{cf},{s},{k}' for cf, s, k in t) + '\n')
    with open(os.path.join(out, 'lexicon.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-lexicon\n# cost: preference counts of ja/prefs.tsv (arc rows), one base-1000 digit per rank\n')
        o.write('stem\treading\tlemma\tud\tctype\ttemplate\tcost\n')
        for stem, rstem, lemma, ud, ct, tid, c in rows:
            o.write(f'{stem}\t{rstem}\t{lemma}\t{ud}\t{ct}\t{tid}\t{c}\n')
    print('lexicon rows', len(rows), 'templates', len(templates), file=sys.stderr)

    # --- characters and unknown words -------------------------------------------------------------------
    with open(os.path.join(out, 'chardef.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-chardef\n# category rows: name invoke group length; range rows: first last categories\n')
        for name, inv, grp, ln in cats:
            o.write(f'cat\t{name}\t{inv}\t{grp}\t{ln}\n')
        for lo, hi, c, more in ranges:
            o.write(f'range\t{lo:04X}\t{hi:04X}\t{" ".join([c] + more)}\n')
    with open(os.path.join(out, 'unk.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-unk\ncategory\tclass\tcost\tperchar\n')
        for cat, c in unkdef:
            f = {'cat': cat, 'ud': cfeat[c]['ud']}
            u = sum(p['w'] for p in kinds['unk'] if p['S'].test(f))
            u += sum(p['w'] for p in arc_prefs if p['select'] == '*')  # the unit count holds for every arc
            o.write(f'{cat}\t{c}\t{u}\t0\n')

    # --- id tables (IR.md §3) and parameters --------------------------------------------------------------
    uds = sorted({k[0] for k in classes} - {'BOS/EOS'})
    with open(os.path.join(a.data, 'udpos.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! udpos\n# UniDic POS (pos1-pos4, * dropped); id 0 is the sentence boundary\nid\tud\n0\tBOS/EOS\n')
        for i, u in enumerate(uds, 1):
            o.write(f'{i}\t{u}\n')
    cts = sorted({k[1] for k in classes} - {'*'})
    cfs = sorted({k[2] for k in classes} - {'*'})
    with open(os.path.join(a.data, 'conj.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! conj\n# UniDic cType and cForm names; id 0 = none\nkind\tid\tname\n')
        for i, c in enumerate(cts, 1):
            o.write(f'ctype\t{i}\t{c}\n')
        for i, c in enumerate(cfs, 1):
            o.write(f'cform\t{i}\t{c}\n')
    derived = sum(p['w'] for p in kinds['derived'])
    join = sum(p['w'] for p in kinds['join'])
    with open(os.path.join(out, 'params.tsv'), 'w', encoding='utf-8') as o:
        o.write('#! analyzer-params\n# compiled from ja/prefs.tsv\nkey\tvalue\n')
        unit = next(p['w'] for p in arc_prefs if p['select'] == '*')
        o.write(f'join_cost\t{join}\nderive_cost\t{derived}\nforbid_cost\t{FORBID}\nunit_cost\t{unit}\n')
        for p in kinds['cut']:
            o.write(f"cut\t{p['select']} {p['next']} {p['w']}\n")
        o.write('stutter_ud\t感動詞-フィラー\n')


if __name__ == '__main__':
    main()
