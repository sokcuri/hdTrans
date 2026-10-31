#!/usr/bin/env python3
"""N5 grid generator.

Enumerates the cells of dims.tsv over the controlled vocabulary, drops the cells valid.tsv rejects, writes the
Japanese of each cell from the conjugation tables (conj-ja.tsv), and asks oracle.py for the expected Korean.

  grid.py [--hd8gen BIN] [--out poc08/tests/grid/n5.tsv] [--cells FILE]
  grid.py --analyzer BIN [--fail-out poc08/tests/grid/n5.analyzer-fail.tsv]   (also run the round-trip check)
  grid.py --check poc08/tests/grid/n5.tsv     (regenerate in memory; fail if the file differs)

Every row also carries its expected Japanese tokens (lemma and, for predicate stems, the UniDic cForm); the
round-trip check compares them with the analyzer's tokens.
"""
import argparse
import collections
import itertools
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)

import oracle  # noqa: E402


def read_tsv(name):
    path = os.path.join(HERE, name)
    rows, header = [], None
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            cols = line.split('\t')
            if header is None:
                header = cols
                continue
            rows.append(dict(zip(header, cols)))
    return rows


# ---------------------------------------------------------------- vocabulary

VERB_CLS = {'godan-u', 'godan-ku', 'godan-ku-iku', 'godan-gu', 'godan-su', 'godan-tsu', 'godan-nu', 'godan-bu',
            'godan-mu', 'godan-ru', 'ichidan', 'suru', 'kuru', 'sahen', 'naru'}
KIND = {**{c: 'verb' for c in VERB_CLS}, 'i-adj': 'adj', 'hoshii': 'adj', 'na-adj': 'nominal',
        'noun-cop': 'nominal', 'aru': 'exist', 'iru': 'exist'}
CTYPE = {'godan-u': '五段-ワア行', 'godan-ku': '五段-カ行', 'godan-ku-iku': '五段-カ行', 'godan-gu': '五段-ガ行',
         'godan-su': '五段-サ行', 'godan-tsu': '五段-タ行', 'godan-nu': '五段-ナ行', 'godan-bu': '五段-バ行',
         'godan-mu': '五段-マ行', 'godan-ru': '五段-ラ行', 'ichidan': '一段', 'suru': 'サ行変格', 'kuru': 'カ行変格',
         'naru': '五段-ラ行', 'aru': '五段-ラ行', 'iru': '一段', 'i-adj': '形容詞', 'hoshii': '形容詞'}


class Vocab:
    def __init__(self):
        self.rows = read_tsv('vocab.tsv')
        self.by_ja = collections.defaultdict(list)
        for r in self.rows:
            r['feats'] = dict(a.split('=', 1) for a in r['feat'].split() if '=' in a)
            r['flags'] = set(a for a in r['gflags'].split() if a != '-')
            for a in list(r['flags']):
                if '=' in a:
                    k, v = a.split('=', 1)
                    r['feats'].setdefault(k, v)
            self.by_ja[r['ja']].append(r)

    def get(self, ja):
        """The grid's row of a lemma: its first row (sense order of the file)."""
        if ja not in self.by_ja:
            raise KeyError('not in vocab.tsv: ' + ja)
        return self.by_ja[ja][0]

    def pred_class(self, ja):
        """The pred dimension value of a predicate lemma (noun-cop for a noun predicate)."""
        r = self.get(ja)
        return 'noun-cop' if r['gcls'] in ('noun', 'noun-cop', 'pron', 'qword') else r['gcls']

    def of_class(self, gcls):
        return [r for r in self.rows if r['gcls'] == gcls and r['sense'] in ('1', '2')
                and not (r['gcls'] == 'i-adj' and r['ja'] == '早い')]


# ---------------------------------------------------------------- Japanese morphology

class M:
    """One expected Japanese token: surface, UniDic lemma, cForm ('' = not checked). A loose token (a noun or a
    number) may also come as several short units that tile its span; alt is a second accepted lemma (the katakana
    reading, the UniDic lemma of proper nouns)."""
    __slots__ = ('surf', 'lemma', 'cform', 'voiced', 'loose', 'alt')

    def __init__(self, surf, lemma, cform='', voiced=False, loose=False, alt=''):
        self.surf, self.lemma, self.cform, self.voiced, self.loose, self.alt = surf, lemma, cform, voiced, loose, alt

    def __repr__(self):
        return '%s/%s%s' % (self.surf, self.lemma, '/' + self.cform if self.cform else '')


class Conj:
    def __init__(self):
        self.t = {}
        for r in read_tsv('conj-ja.tsv'):
            self.t[(r['ctype'], r['form'])] = r

    def form(self, lemma, write, ctype, form, check=True):
        r = self.t.get((lemma, form)) or self.t.get((ctype, form))
        if r is None:
            raise KeyError('no form %s of %s (%s)' % (form, lemma, ctype))
        strip = r['strip']
        stem = '' if strip == '-' else (write[:-len(strip)] if strip and write.endswith(strip) else write)
        return M(stem + r['ending'], lemma, r['cform'] if check else '', r['voiced'] == '1')

    def aux(self, lemma, form):
        r = self.t[('aux:' + lemma, form)]
        return M(r['ending'], lemma, '')


class Unit:
    """The conjugating word at the end of the predicate so far."""

    def __init__(self, lemma, write, ctype, kind, check=True):
        self.lemma, self.write, self.ctype, self.kind, self.check = lemma, write, ctype, kind, check


class J:
    """Japanese builder: a list of expected tokens; た/て/たり follow the voicing of the token before."""

    def __init__(self, conj):
        self.c, self.ms = conj, []

    def add(self, *ms):
        self.ms.extend(ms)

    def word(self, u, form):
        if u.kind == 'aux-adj':
            self.ms.append(self.c.aux(u.lemma, form))
        else:
            self.ms.append(self.c.form(u.lemma, u.write, u.ctype, form, u.check))

    def voiced(self):
        return bool(self.ms) and self.ms[-1].voiced

    def ta(self, form='dict'):
        self.ms.append(M('だ' if self.voiced() else 'た', 'た'))

    def te(self):
        self.ms.append(M('で' if self.voiced() else 'て', 'て'))

    def tari(self):
        self.ms.append(M('だり' if self.voiced() else 'たり', 'たり'))

    def p(self, surf, lemma=None):
        self.ms.append(M(surf, lemma or surf))

    def noun(self, r):
        """A vocab noun; compound nouns may come as short units (日本/語)."""
        loose = r['gcls'] in ('noun', 'noun-cop')
        self.ms.append(M(r['write'], r['ja'], loose=loose, alt=katakana(r['yomi'])))

    def text(self):
        return ''.join(m.surf for m in self.ms)


def katakana(s):
    return ''.join(chr(ord(c) + 0x60) if 'ぁ' <= c <= 'ゖ' else c for c in s)


def unit_of(v, ja):
    r = v.get(ja)
    g = r['gcls']
    if g == 'sahen':
        return Unit('為る', 'する', 'サ行変格', 'verb', check=True)
    if g in ('na-adj', 'noun', 'noun-cop', 'pron', 'qword'):
        return Unit(r['ja'], r['write'], '-', 'nominal')
    kind = 'adj' if g in ('i-adj', 'hoshii') else 'verb'
    return Unit(r['ja'], r['write'], CTYPE[g], kind)


def j_plain(j, u, tense, pol, adn=False, drop_da=False, narration=False):
    """Plain (non-polite) form of the unit u with tense and polarity."""
    dform = 'adn' if adn else 'dict'
    if u.kind == 'verb':
        if pol == 'pos':
            if tense == 'nonpast':
                j.word(u, dform)
            else:
                j.word(u, 'ta')
                j.ta(dform)
        else:
            if u.lemma == '有る':
                nai = Unit('無い', 'ない', 'aux', 'aux-adj')
            else:
                j.word(u, 'neg')
                nai = Unit('ない', 'ない', 'aux', 'aux-adj')
            if tense == 'nonpast':
                j.word(nai, dform)
            else:
                j.word(nai, 'ta')
                j.ta(dform)
    elif u.kind in ('adj', 'aux-adj'):
        if pol == 'pos':
            if tense == 'nonpast':
                j.word(u, dform)
            else:
                j.word(u, 'ta')
                j.ta(dform)
        else:
            j.word(u, 'cont')
            nai = Unit('無い', 'ない', 'aux', 'aux-adj')
            if tense == 'nonpast':
                j.word(nai, dform)
            else:
                j.word(nai, 'ta')
                j.ta(dform)
    else:  # nominal: the stem is already written
        da = Unit('だ', 'だ', 'aux', 'aux-adj')
        if pol == 'pos':
            if tense == 'nonpast':
                if adn:
                    j.word(da, 'adn') if u.lemma != '積り' else None
                elif not drop_da:
                    j.word(da, 'dict')
            else:
                j.word(da, 'ta')
                j.ta(dform)
        else:
            if narration:
                j.word(da, 'cont')
                j.p('は')
            else:
                j.word(da, 'fused')
            nai = Unit('無い', 'ない', 'aux', 'aux-adj')
            if tense == 'nonpast':
                j.word(nai, dform)
            else:
                j.word(nai, 'ta')
                j.ta(dform)


def j_polite(j, u, tense, pol):
    masu = Unit('ます', 'ます', 'aux', 'aux-adj')
    if u.kind == 'verb':
        j.word(u, 'cont')
        if pol == 'pos':
            j.word(masu, 'dict' if tense == 'nonpast' else 'cont')
            if tense == 'past':
                j.ta()
        else:
            j.word(masu, 'neg')
            j.add(j.c.aux('ず', 'dict'))
            if tense == 'past':
                j.add(j.c.aux('です', 'cont'))
                j.ta()
    elif u.kind in ('adj', 'aux-adj'):
        j_plain(j, u, tense, pol)
        j.add(j.c.aux('です', 'dict'))
    else:
        if pol == 'pos':
            j.add(j.c.aux('です', 'dict' if tense == 'nonpast' else 'cont'))
            if tense == 'past':
                j.ta()
        else:
            j.word(Unit('だ', 'だ', 'aux', 'aux-adj'), 'fused')
            j.word(Unit('有る', 'ある', '五段-ラ行', 'verb', check=False), 'cont')
            j.word(masu, 'neg')
            j.add(j.c.aux('ず', 'dict'))
            if tense == 'past':
                j.add(j.c.aux('です', 'cont'))
                j.ta()


def j_stem(j, v, ja):
    """Write the part of a predicate before its conjugating unit; return the unit."""
    r = v.get(ja)
    g = r['gcls']
    if g in ('sahen', 'noun', 'noun-cop'):
        j.noun(r)
    elif g in ('na-adj', 'pron', 'qword'):
        j.p(r['write'], r['ja'])
    return unit_of(v, ja)


def j_pred(j, v, pred, d):
    """The predicate of a final clause with the cell's dims d."""
    tense, pol, polite, stype = d['tense'], d['pol'], d['polite'], d['stype']
    mod, aspect, sfp = d['mod'], d['aspect'], d['sfp']
    narration = d['ctx'] == 'narration'
    if isinstance(pred, tuple):  # (kind, ...) composite predicates of points.tsv
        u = pred_composite(j, v, pred)
    else:
        u = j_stem(j, v, pred)
    if aspect == 'teiru':
        j.word(u, 'ta')
        j.te()
        u = Unit('居る', 'いる', '一段', 'verb', check=False)
    if stype == 'request':
        j.word(u, 'ta')
        j.te()
        j.add(j.c.aux('下さる', 'imp'))
        return
    if stype == 'prohibition':
        j.word(u, 'neg')
        j.word(Unit('ない', 'ない', 'aux', 'aux-adj'), 'dict')
        j.p('で', 'て')
        j.add(j.c.aux('下さる', 'imp'))
        return
    if stype.startswith('invite'):
        j.word(u, 'cont')
        if stype == 'invite-masenka':
            j.add(j.c.aux('ます', 'neg'), j.c.aux('ず', 'dict'))
        else:
            j.add(j.c.aux('ます', 'vol'))
        if stype != 'invite-mashou':
            j.p('か')
        return
    if mod == 'tai':
        j.word(u, 'cont')
        u = Unit('たい', 'たい', 'aux', 'aux-adj')
    elif mod == 'darou':
        j_plain(j, u, tense, pol, drop_da=True, narration=narration)
        j.add(j.c.aux('です', 'vol') if polite == 'masu' else j.c.aux('だ', 'vol'))
        return
    elif mod == 'tsumori':
        if pol == 'pos':
            j.word(u, 'adn')
        else:
            j.word(u, 'neg')
            j.word(Unit('ない', 'ない', 'aux', 'aux-adj'), 'adn')
        j.p('つもり', '積り')
        u, pol = Unit('積り', 'つもり', '-', 'nominal'), 'pos'
    elif mod == 'hougaii':
        if pol == 'pos':
            j.word(u, 'ta')
            j.ta('adn')
        else:
            j.word(u, 'neg')
            j.word(Unit('ない', 'ない', 'aux', 'aux-adj'), 'adn')
        j.p('方')
        j.p('が')
        u, pol = Unit('良い', 'いい', '形容詞', 'adj', check=False), 'pos'
    elif mod == 'temoii':
        j.word(u, 'ta')
        j.te()
        j.p('も')
        u = Unit('良い', 'いい', '形容詞', 'adj', check=False)
    elif mod == 'tewaikenai':
        j.word(u, 'ta')
        j.te()
        j.p('は')
        u, pol = Unit('行ける', 'いける', '一段', 'verb', check=False), 'neg'
    elif mod == 'takoto':
        j.word(u, 'ta')
        j.ta('adn')
        j.p('こと', '事')
        j.p('が')
        u = Unit('有る', 'ある', '五段-ラ行', 'verb', check=False)
    if polite == 'masu':
        j_polite(j, u, tense, pol)
    else:
        j_plain(j, u, tense, pol, drop_da=(stype == 'question' and sfp == 'none'), narration=narration)
    if stype == 'question' and polite == 'masu':
        j.p('か')
    if sfp == 'ne':
        j.p('ね')
    elif sfp == 'yo':
        j.p('よ')


def pred_composite(j, v, pred):
    kind, arg = pred
    if kind == 'cop':
        j_np(j, v, arg)
        return Unit('-', '-', '-', 'nominal')
    if kind == 'naru':
        r = v.get(arg)
        if r['gcls'] == 'i-adj':
            j.word(unit_of(v, arg), 'cont')
        elif r['gcls'] == 'na-adj':
            j.p(r['write'], r['ja'])
            j.word(Unit('だ', 'だ', 'aux', 'aux-adj'), 'ni')
        else:
            j.p(r['write'], r['ja'])
            j.p('に')
        return Unit('成る', 'なる', '五段-ラ行', 'verb')
    raise ValueError(pred)


def parse_np(s):
    """NP mini-language of points.tsv / links.tsv (see the points.tsv header)."""
    if s.startswith('{'):
        verb, part, noun = s[1:-1].split('/')
        return ('clause', verb, part, noun)
    if '~' in s:
        a, b = s.split('~', 1)
        return ('adj', a, b)
    if '+' in s:
        a, mid, *rest = s.split('+')
        if rest:
            return ('list' if mid != 'の' else 'poss', a, mid, rest[0])
        return ('adnom' if a in ('この', 'その', 'あの', 'どの') else 'num', a, mid)
    return ('n', s)


ADNOM = {'この': '此の', 'その': '其の', 'あの': '彼の', 'どの': '何の'}
NUM_LEMMA = {'一': '一', '二': '二', '三': '三', '十': '十', '二十': '二十', '百': '百'}


def j_np(j, v, np):
    k = np[0]
    if k == 'n':
        r = v.get(np[1])
        if r['gcls'] in ('noun', 'noun-cop'):
            j.noun(r)
        else:
            j.p(r['write'], r['ja'])
    elif k in ('list', 'poss'):
        j_np(j, v, ('n', np[1]))
        j.p(np[2])
        j_np(j, v, ('n', np[3]))
    elif k == 'adnom':
        j.p(np[1], ADNOM[np[1]])
        j_np(j, v, ('n', np[2]))
    elif k == 'num':
        # a number may be one token or one per digit (二十, 二/十); the counter is its own token
        j.add(M(np[1], np[1], loose=True))
        j.p(np[2], np[2])
    elif k == 'adj':
        r = v.get(np[1])
        if r['gcls'] == 'na-adj':
            j.p(r['write'], r['ja'])
            j.word(Unit('だ', 'だ', 'aux', 'aux-adj'), 'adn')
        else:
            j.word(unit_of(v, np[1]), 'adn')
        j_np(j, v, ('n', np[2]))
    elif k == 'clause':
        _, verb, part, noun = np
        j_np(j, v, ('n', noun))
        j.p(part)
        j.word(unit_of(v, verb), 'adn')
        j.p('の')
    else:
        raise ValueError(np)


def j_slot(j, v, slot):
    role, part, np = slot
    if role == 'adv':
        r = v.get(np[1])
        j.p(r['write'], r['ja'])
        return
    if role == 'advadj':
        r = v.get(np[1])
        if r['gcls'] == 'na-adj':
            j.p(r['write'], r['ja'])
            j.word(Unit('だ', 'だ', 'aux', 'aux-adj'), 'ni')
        else:
            j.word(Unit(r['ja'], r['write'], '形容詞', 'adj'), 'cont')
        return
    j_np(j, v, np)
    if part != '-':
        j.p(part)


def parse_slots(s):
    if s in ('-', ''):
        return []
    out = []
    for item in s.split('; '):
        role, part, np = item.split(':', 2)
        out.append((role, part, parse_np(np)))
    return out


def np_nouns(np):
    k = np[0]
    if k == 'n':
        return [np[1]]
    if k in ('list', 'poss'):
        return [np[1], np[3]]
    if k == 'adnom':
        return [np[2]]
    if k == 'adj':
        return [np[2]]
    if k == 'clause':
        return [np[3]]
    return []


# ---------------------------------------------------------------- dimensions and validity

def load_dims():
    blocks = collections.OrderedDict()
    for r in read_tsv('dims.tsv'):
        blocks.setdefault(r['block'], collections.OrderedDict())[r['dim']] = r['values'].split()
    return blocks


DEFAULTS = {'aspect': 'simple', 'mod': 'none', 'sfp': 'none', 'pol': 'pos', 'stype': 'statement',
            'link': 'final', 'v1tense': '-', 'tense': 'nonpast'}


class Valid:
    def __init__(self):
        self.rules = []
        for r in read_tsv('valid.tsv'):
            self.rules.append((r['id'], self.parse(r['if']), self.parse(r['then'])))

    @staticmethod
    def parse(s):
        terms = []
        for t in s.split():
            if '!=' in t:
                k, vals = t.split('!=', 1)
                terms.append((k, False, set(vals.split('|'))))
            else:
                k, vals = t.split('=', 1)
                terms.append((k, True, set(vals.split('|'))))
        return terms

    @staticmethod
    def holds(terms, ctx):
        for k, positive, vals in terms:
            have = ctx.get(k, set())
            have = have if isinstance(have, set) else {have}
            hit = bool(have & vals)
            if hit != positive:
                return False
        return True

    def ok(self, ctx, limit=()):
        for _, cond, then in self.rules:
            if self.holds(cond, ctx) and not self.holds(then, ctx):
                return False
        return self.holds(list(limit), ctx)


def pred_ctx(v, ja):
    """Validity keys of a predicate lemma."""
    if isinstance(ja, tuple):
        if ja[0] == 'cop':
            return {'kind': 'nominal', 'flag': set()}
        return {'kind': 'verb', 'vol': '-', 'asp': 'change', 'flag': set()}
    r = v.get(ja)
    g = r['gcls']
    kind = KIND.get(g, 'nominal')
    c = {'kind': kind, 'flag': set(r['flags'])}
    for k in ('vol', 'asp', 'pclass'):
        if k in r['feats']:
            c[k] = r['feats'][k]
    c.setdefault('vol', '-')
    return c


def time_flags(v, nouns):
    out = set()
    for n in nouns:
        if n in v.by_ja:
            t = v.get(n)['feats'].get('time')
            if t:
                out.add(t)
    return out


# ---------------------------------------------------------------- rows

class Row:
    def __init__(self, rid, block, dims, spec, ms):
        self.id, self.block, self.dims, self.spec, self.ms = rid, block, dims, spec, ms
        self.ja = ''.join(m.surf for m in ms)
        self.ko = None

    def dims_text(self):
        return ';'.join('%s=%s' % (k, self.dims[k]) for k in self.dims)


def split_cp(cp):
    ctx, polite = cp.split('.')
    return ctx, polite


def finish(j, d):
    """End mark and dialogue quotes."""
    if d['stype'] == 'question' and d['polite'] == 'plain':
        j.p('？')
    else:
        j.p('。')
    if d['ctx'] == 'dialogue':
        j.ms.insert(0, M('「', '「'))
        j.p('」')


def frame_slots(v, pred, fill, stype, mod):
    r = v.get(pred)
    out = []
    for f in FRAMES.get(r['gframe'], []):
        # no subject in requests and invitations, nor with the speaker's wish or plan (たい, つもり)
        if f['role'] == 'subj' and (stype not in ('statement', 'question') or mod in ('tai', 'tsumori')):
            continue
        out.append((f['role'], f['ja' + fill], ('n', f['fill' + fill])))
    return out


FRAMES = collections.defaultdict(list)
DUPLICATES = []  # (kept, dropped) rows with the same Japanese


def gen_vfinal(v, conj, valid, dims, block, rows):
    D = dims[block]
    for gcls in D['pred']:
        for rank, r in enumerate(v.of_class(gcls), 1):
            pc = dict(pred_ctx(v, r['ja']), rank=str(rank))
            keys = [k for k in D if k != 'pred']
            for combo in itertools.product(*(D[k] for k in keys)):
                d = dict(DEFAULTS)
                d.update(zip(keys, combo))
                d['ctx'], d['polite'] = split_cp(d['ctx.polite'])
                slots = frame_slots(v, r['ja'], d['fill'], d['stype'], d['mod'])
                ctx = dict(pc, block=block, pred=gcls, **d)
                ctx['time'] = time_flags(v, [n for s in slots for n in np_nouns(s[2])])
                if not valid.ok(ctx):
                    continue
                # fill 2 adds a topic when the frame has no subject (statements and questions)
                topic = None
                if d['fill'] == '2' and d['stype'] in ('statement', 'question') and \
                        not any(s[0] == 'subj' for s in slots) and pc['kind'] == 'verb':
                    topic = '私' if d['mod'] in ('tai', 'tsumori') else '友達'
                    if topic == '友達' and any('友達' in np_nouns(s[2]) for s in slots):
                        topic = '母'
                    slots = [('top', 'は', ('n', topic))] + slots
                j = J(conj)
                for s in slots:
                    j_slot(j, v, s)
                j_pred(j, v, r['ja'], d)
                finish(j, d)
                key = collections.OrderedDict([('block', block), ('pred', gcls)])
                for k in keys:
                    if k not in ('fill',):
                        key[k] = d[k]
                spec = {'block': block, 'pred': r['ja'], 'slots': slots, 'd': d}
                rows.append(Row(None, block, key, spec, j.ms))


def gen_links(v, conj, valid, dims, rows):
    D = dims['link']
    pairs = read_tsv('links.tsv')
    keys = list(D)
    for p in pairs:
        s1, s2 = parse_slots(p['slots1']), parse_slots(p['slots2'])
        limit = Valid.parse(p['limit']) if p['limit'] != '-' else []
        pc = pred_ctx(v, p['pred2'])
        for combo in itertools.product(*(D[k] for k in keys)):
            d = dict(DEFAULTS)
            d.update(zip(keys, combo))
            d['pol'] = p['pol']
            d['ctx'], d['polite'] = split_cp(d['ctx.polite'])
            ctx = dict(pc, block='link', group=p['group'], **d)
            ctx['time'] = time_flags(v, [n for s in s1 + s2 for n in np_nouns(s[2])])
            if not valid.ok(ctx, limit):
                continue
            j = J(conj)
            for s in s1:
                j_slot(j, v, s)
            u = j_stem(j, v, p['pred1'])
            link = d['link']
            narr = d['ctx'] == 'narration'
            if link == 'te':
                j.word(u, 'ta')
                j.te()
            elif link == 'tekara':
                j.word(u, 'ta')
                j.te()
                j.p('から')
            elif link == 'mae':
                j.word(u, 'adn')
                j.p('前')
                j.p('に')
            elif link == 'ato':
                j.word(u, 'ta')
                j.ta('adn')
                j.p('後')
                j.p('で')
            elif link == 'toki':
                j_plain(j, u, d['v1tense'], 'pos', adn=True)
                j.p('とき', '時')
            elif link == 'tari':
                j.word(u, 'ta')
                j.tari()
            elif link == 'nagara':
                j.word(u, 'cont')
                j.p('ながら')
            else:  # kara, ga, kedo
                j_plain(j, u, d['v1tense'], 'pos', narration=narr)
                j.p({'kara': 'から', 'ga': 'が', 'kedo': 'けど'}[link], 'けれど' if link == 'kedo' else None)
                j.p('、')
            for s in s2:
                j_slot(j, v, s)
            if link == 'tari':
                u2 = j_stem(j, v, p['pred2'])
                j.word(u2, 'ta')
                j.tari()
                pred2 = '為る'
                d2 = dict(d)
                j_pred(j, v, pred2, d2)
            else:
                j_pred(j, v, p['pred2'], d)
            finish(j, d)
            key = collections.OrderedDict([('block', 'link'), ('group', p['group'])])
            for k in keys:
                key[k] = d[k]
            spec = {'block': 'link', 'pair': p, 'slots1': s1, 'slots2': s2, 'd': d}
            rows.append(Row(None, 'link', key, spec, j.ms))


def gen_points(v, conj, valid, dims, rows):
    D = dims['point']
    keys = list(D)
    for p in read_tsv('points.tsv'):
        slots = parse_slots(p['slots'])
        limit = Valid.parse(p['limit']) if p['limit'] != '-' else []
        pred = p['pred']
        if ':' in pred:
            kind, arg = pred.split(':', 1)
            pred = (kind, parse_np(arg) if kind == 'cop' else arg)
        pc = pred_ctx(v, pred)
        for combo in itertools.product(*(D[k] for k in keys)):
            d = dict(DEFAULTS)
            d.update(zip(keys, combo))
            d.update(stype=p['stype'], pol=p['pol'], aspect=p['aspect'])
            d['ctx'], d['polite'] = split_cp(d['ctx.polite'])
            ctx = dict(pc, block='point', **d)
            nouns = [n for s in slots for n in np_nouns(s[2])]
            ctx['time'] = time_flags(v, nouns)
            if not valid.ok(ctx, limit):
                continue
            j = J(conj)
            for s in slots:
                j_slot(j, v, s)
            j_pred(j, v, pred, d)
            finish(j, d)
            key = collections.OrderedDict([('block', 'point'), ('point', p['point']), ('row', p['id'])])
            for k in ('ctx.polite', 'stype', 'tense', 'pol', 'aspect'):
                key[k] = d[k]
            spec = {'block': 'point', 'pred': pred, 'slots': slots, 'd': d}
            rows.append(Row(None, 'point', key, spec, j.ms))


def generate(v):
    conj, valid, dims = Conj(), Valid(), load_dims()
    for f in read_tsv('frames.tsv'):
        FRAMES[f['gframe']].append(f)
    rows = []
    for block in ('vfinal', 'vmod', 'pfinal', 'sfp'):
        gen_vfinal(v, conj, valid, dims, block, rows)
    gen_links(v, conj, valid, dims, rows)
    gen_points(v, conj, valid, dims, rows)
    seen, out = {}, []
    for r in rows:
        if r.ja in seen:
            DUPLICATES.append((seen[r.ja], r))
            continue
        seen[r.ja] = r
        out.append(r)
    for i, r in enumerate(out, 1):
        r.id = 'n5-%s-%04d' % (r.block, i)
    return out


# ---------------------------------------------------------------- analyzer round trip

def utf8len(s):
    return len(s.encode('utf-8'))


def norm_lemma(l):
    # loanword lemmas carry their source spelling (パン-pan); compare the Japanese part
    i = l.find('-')
    return l[:i] if i > 0 else l


def roundtrip(rows, analyzer, fail_out):
    with tempfile.TemporaryDirectory() as d:
        ja_path = os.path.join(d, 'ja.txt')
        with open(ja_path, 'w', encoding='utf-8') as f:
            for r in rows:
                f.write(r.ja + '\n')
        env = dict(os.environ, HOME=tempfile.mkdtemp(), HDTRANS_CACHE_DIR='off')
        with open(ja_path, encoding='utf-8') as fin:
            subprocess.run([analyzer, '--each', '--stages', d], stdin=fin, stdout=subprocess.DEVNULL, env=env,
                           check=True)
        lines = []
        with open(os.path.join(d, 'ir.jsonl'), encoding='utf-8') as f:
            for l in f:
                o = json.loads(l)
                if 'toks' in o:
                    lines.append(o)
    if len(lines) != len(rows):
        raise SystemExit('analyzer: %d lines for %d rows' % (len(lines), len(rows)))
    fails = []
    for r, o in zip(rows, lines):
        by_start = {t['src'][0]: t for t in o['toks']}
        pos, bad = 0, []
        for m in r.ms:
            n = utf8len(m.surf)
            t = by_start.get(pos)
            exp = m.lemma + ('/' + m.cform if m.cform else '')
            if m.loose and t is not None and t['src'][1] < pos + n:
                # short units tiling the span
                end, q = t['src'][1], pos
                while end < pos + n and end in by_start:
                    end = by_start[end]['src'][1]
                if end == pos + n:
                    pos += n
                    continue
            if t is None or t['src'][1] != pos + n:
                got = t['surf'] if t else '?'
                if t is None:
                    cover = [x for x in o['toks'] if x['src'][0] < pos < x['src'][1]]
                    got = cover[0]['surf'] if cover else '?'
                bad.append((m.surf, exp, 'seg:' + got))
            else:
                gl = norm_lemma(t['lemma'])
                if gl not in (m.lemma, m.alt) or (m.cform and t.get('cform', '') != m.cform):
                    bad.append((m.surf, exp, gl + ('/' + t.get('cform', '') if m.cform else '')))
            pos += n
        if bad:
            fails.append((r, bad))
    with open(fail_out, 'w', encoding='utf-8') as f:
        f.write('id\tja\texpected\tgot\n')
        for r, bad in fails:
            f.write('%s\t%s\t%s\t%s\n' % (r.id, r.ja, ' | '.join('%s=%s' % (s, e) for s, e, _ in bad),
                                          ' | '.join('%s=%s' % (s, g) for s, _, g in bad)))
    return fails


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('--hd8gen', default=os.environ.get('HD8GEN', os.path.join(REPO, 'build/mac/poc08/generator/hd8gen')))
    ap.add_argument('--gen-data', default=os.path.join(REPO, 'poc08/generator/data/ko'),
                    help="the generator's Korean tables (spoken.tsv, particles.tsv)")
    ap.add_argument('--out', default=os.path.join(REPO, 'poc08/tests/grid/n5.tsv'))
    ap.add_argument('--cells', help='write the row count of every cell to this file')
    ap.add_argument('--analyzer', help='hdtrans08-ir binary: run the round-trip check')
    ap.add_argument('--fail-out', default=os.path.join(REPO, 'poc08/tests/grid/n5.analyzer-fail.tsv'))
    ap.add_argument('--check', help='compare the regenerated grid with this file instead of writing it')
    a = ap.parse_args()
    v = Vocab()
    rows = generate(v)
    orc = oracle.Oracle(a.hd8gen, HERE, a.gen_data)
    for r in rows:
        r.ko = orc.ko(r.spec)
    for kept, dropped in DUPLICATES:
        dropped.ko = orc.ko(dropped.spec)
        if dropped.ko != kept.ko:
            print('ambiguous Japanese: %s → %s (%s) / %s (%s)' % (kept.ja, kept.ko, kept.dims_text(), dropped.ko,
                                                              dropped.dims_text()), file=sys.stderr)
    orc.close()
    text = 'id\tdims\tja\tko\n' + ''.join('%s\t%s\t%s\t%s\n' % (r.id, r.dims_text(), r.ja, r.ko) for r in rows)
    if a.check:
        with open(a.check, encoding='utf-8') as f:
            old = f.read()
        if old != text:
            new, prev = text.split('\n'), old.split('\n')
            for i, (x, y) in enumerate(zip(new, prev)):
                if x != y:
                    print('%s:%d differs\n  file  %s\n  regen %s' % (a.check, i + 1, y, x))
                    break
            print('%s is not reproducible (%d lines, regenerated %d); rerun grid.py' % (a.check, len(prev), len(new)))
            return 1
        print('%s reproduced (%d rows)' % (a.check, len(rows)))
        return 0
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write(text)
    blocks = collections.Counter(r.block for r in rows)
    cells = collections.Counter(r.dims_text() for r in rows)
    print('rows %d, cells %d' % (len(rows), len(cells)))
    for b, n in blocks.items():
        print('  block %-7s rows %5d  cells %5d' % (b, n, len({r.dims_text() for r in rows if r.block == b})))
    if a.cells:
        with open(a.cells, 'w', encoding='utf-8') as f:
            f.write('rows\tcell\n')
            for c, n in sorted(cells.items()):
                f.write('%d\t%s\n' % (n, c))
    if a.analyzer:
        fails = roundtrip(rows, a.analyzer, a.fail_out)
        shapes = collections.Counter()
        for _, bad in fails:
            for s, e, g in bad:
                shapes['%s %s → %s' % (s, e, g)] += 1
        print('analyzer round trip: %d of %d rows fail' % (len(fails), len(rows)))
        for s, n in shapes.most_common(15):
            print('  %5d  %s' % (n, s))


if __name__ == '__main__':
    sys.exit(main())
