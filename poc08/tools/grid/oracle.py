#!/usr/bin/env python3
"""N5 grid oracle: the expected Korean of a grid cell.

The Korean is composed from the spec tables of this folder (ko-*.tsv: one table per dimension, Japanese value →
Korean value), the fixed gloss of each controlled lemma (vocab.tsv), the Korean paradigm of the generator
(`hd8gen batch`, one predicate word per question) and the generator's realization tables for the spoken forms
(ko/spoken.tsv fusions, ko/particles.tsv person particles; STYLE §7: the generator tables are the house style).
Each lemma's Korean, its Korean class and its ている realization (te) come from the N5 lexicon (levels/n5.tsv and
the sense rows of lexicon/curriculum.tsv), the sense picked by the lexicon's own reference rule (tools/senses.py)
from the grid's context; disputes.tsv keeps the house form where the grid and the lexicon disagree. It reads
nothing of the engine's transfer data.

A cell spec (made by grid.py) holds the block, the predicate lemma, the argument slots and the dimension values d.
"""
import os
import subprocess
import sys
import types

FRAME_KO = {'을': '을/를', '를': '을/를', '이': '이/가', '가': '이/가'}
KASE = {'이/가': 'subj', '은/는': 'top', '을/를': 'obj', '(으)로': 'instr'}
# Japanese particle → the lexicon's argument slot (senses.py ARG_SLOTS)
ARG_OF = {'が': 'subj', 'は': 'subj', 'を': 'obj', 'に': 'dat', 'で': 'loc', 'と': 'with', 'から': 'from', 'へ': 'to',
          'まで': 'to'}
ROLE_ARG = {'subj': 'subj', 'top': 'subj', 'obj': 'obj', 'dat': 'dat', 'loc': 'loc', 'goal': 'to', 'path': 'obj',
            'com': 'with', 'src': 'from'}
TE_OF_ASP = {'activity': 'go', 'change': 'eo', 'change-past': 'past', 'state': 'simple'}
VERB_GCLS = {'godan-u', 'godan-ku', 'godan-ku-iku', 'godan-gu', 'godan-su', 'godan-tsu', 'godan-nu', 'godan-bu',
             'godan-mu', 'godan-ru', 'ichidan', 'suru', 'kuru', 'sahen', 'naru'}


def read_tsv(here, name):
    rows, header = [], None
    with open(os.path.join(here, name), encoding='utf-8') as f:
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


def has_final(word, rieul_as_vowel=False):
    """Whether the last Hangul syllable of word has a final consonant."""
    for ch in reversed(word):
        c = ord(ch)
        if 0xAC00 <= c <= 0xD7A3:
            jong = (c - 0xAC00) % 28
            if rieul_as_vowel and jong == 8:
                return False
            return jong != 0
        if ch.isdigit():
            # digits are read in Sino-Korean: 0 영, 1 일, 3 삼, 6 육, 7 칠, 8 팔, 10 십 end in a consonant
            return ch in '013678'
    return False


def attach(word, particle):
    """Attach a particle pair A/B (after a consonant / after a vowel); (으)로 counts ㄹ as a vowel."""
    if particle in ('-', ''):
        return word
    if particle == '(으)로':
        return word + ('으로' if has_final(word, rieul_as_vowel=True) else '로')
    if '/' in particle:
        a, b = particle.split('/')
        return word + (a if has_final(word) else b)
    return word + particle


class PW:
    """A predicate word: a lemma conjugated by hd8gen with the endings gathered in forms."""

    def __init__(self, lemma, pos, host='', split='', neg=''):
        self.lemma, self.pos, self.host, self.split, self.neg = lemma, pos, host, split, neg
        self.forms = []


class Oracle:
    def __init__(self, hd8gen, here, gendata, lexdir):
        self.here = here
        sys.path.insert(0, os.path.join(lexdir, '..', 'tools'))
        import senses  # the lexicon's reference sense choice
        self.senses = senses

        def reader(path):
            return read_tsv(os.path.dirname(path), os.path.basename(path))
        self.classes, self.sense_rows = senses.load(lexdir, reader)
        self.lex = {(r['ja'], r['pos']): r for r in read_tsv(os.path.join(lexdir, 'levels'), 'n5.tsv')}
        self.disputes = read_tsv(here, 'disputes.tsv')
        self.cc = None  # the clause being composed: pred, args, forms, register
        self.used = {}  # (ja, ko, source) → count
        self.spoken = read_tsv(gendata, 'spoken.tsv')
        self.gen_particles = read_tsv(gendata, 'particles.tsv')
        self.cur_level = 'haera'
        self.cmd = [hd8gen, 'batch']
        self.proc = None
        self.cache = {}
        self.vocab = {}
        for r in read_tsv(here, 'vocab.tsv'):
            self.vocab.setdefault(r['ja'], []).append(r)
        self.level = {(r['ctx'], r['polite']): r['level'] for r in read_tsv(here, 'ko-level.tsv')}
        self.final = {(r['level'], r['stype'], r['sfp']): r for r in read_tsv(here, 'ko-final.tsv')}
        self.neg = read_tsv(here, 'ko-neg.tsv')
        self.aspect = read_tsv(here, 'ko-aspect.tsv')
        self.mod = read_tsv(here, 'ko-mod.tsv')
        self.link = read_tsv(here, 'ko-link.tsv')
        self.particle = read_tsv(here, 'ko-particle.tsv')
        self.contract = read_tsv(here, 'ko-contract.tsv')
        self.adv = read_tsv(here, 'ko-adv.tsv')
        self.num = {r['ja']: r for r in read_tsv(here, 'ko-num.tsv')}
        self.punct = {r['ja']: r['ko'] for r in read_tsv(here, 'ko-punct.tsv')}

    # ------------------------------------------------------------ paradigm

    def conj(self, lemma, pos, host, forms):
        key = (lemma, pos, host, tuple(forms))
        if key in self.cache:
            return self.cache[key]
        if self.proc is None:
            self.proc = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                                         encoding='utf-8')
        line = '\t'.join([lemma, pos or '-', 'COP' if pos == 'C' else ('NC' if pos == 'N' else '-'), host or '-',
                          ' '.join(forms) if forms else '-'])
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()
        out = self.proc.stdout.readline().rstrip('\n')
        if not forms:
            out = (host or '') + lemma if pos == 'C' else lemma
        self.cache[key] = out
        return out

    def close(self):
        if self.proc:
            self.proc.stdin.close()
            self.proc.wait()
            self.proc = None

    # ------------------------------------------------------------ lexicon

    def row(self, ja, gcls=None):
        rows = self.vocab[ja]
        if gcls:
            for r in rows:
                if r['gcls'] == gcls:
                    return r
        return rows[0]

    def feats(self, r):
        f = dict(a.split('=', 1) for a in r['feat'].split() if '=' in a)
        for a in r['gflags'].split():
            if '=' in a:
                k, v = a.split('=', 1)
                f.setdefault(k, v)
        return f

    def frame_map(self, ja):
        """The lexicon frame of a predicate: Japanese particle → Korean particle (が=을)."""
        if not isinstance(ja, str) or ja not in self.vocab:
            return {}
        out = {}
        r = self.row(ja)
        lr = self.lexrow(r)
        for a in (lr['frame'] if lr else r['frame']).split():
            if '=' in a:
                k, v = a.split('=', 1)
                out[k] = FRAME_KO.get(v, v)
        return out

    # ------------------------------------------------------------ lexicon senses

    def clause(self, pred, slots, forms, d, extra_args=None):
        """Set the clause context the sense choice reads: the predicate, its arguments by slot, its forms."""
        args = {}
        for role, jpart, np in slots:
            if role in ('adv', 'advadj', 'qty'):
                continue
            slot = ARG_OF.get(jpart) or ROLE_ARG.get(role)
            if slot:
                args.setdefault(slot, []).extend(self.np_nouns(np))
        for k, v in (extra_args or {}).items():
            args.setdefault(k, []).extend(v)
        counted = [n for s in ('obj', 'subj') for n in args.get(s, [])]
        self.cc = {'pred': pred if isinstance(pred, str) else None, 'args': args, 'forms': set(forms),
                   'register': 'polite' if d['polite'] == 'masu' else 'casual', 'counted': counted[:1]}

    @staticmethod
    def np_nouns(np):
        k = np[0]
        if k == 'n':
            return [np[1]]
        if k == 'list':
            return [np[1], np[3]]
        if k == 'poss':
            return [np[3]]
        if k in ('adnom', 'adj'):
            return [np[2]]
        return []

    def gloss(self, ja, gcls=None, forms=None, head=None, nexts=(), args=None, preds=None, why=False):
        """The Korean of a lemma: the lexicon sense the context selects (senses.choose), else the vocab gloss."""
        r = self.row(ja, gcls)
        cc = self.cc or {'pred': None, 'args': {}, 'forms': set(), 'register': 'casual'}
        key = (r['ja'], r['yomi'], r['pos'])
        cue = ''
        if key in self.sense_rows:
            ctx = types.SimpleNamespace(
                args=cc['args'] if args is None else args, head=head, mods=[], nexts=list(nexts), afters=[],
                preds=([cc['pred']] if cc['pred'] else []) if preds is None else list(preds),
                forms=cc['forms'] if forms is None else set(forms), domain='', register=cc['register'], main_vol='')
            _, ko, cue = self.senses.choose([(n, k, c) for n, k, c, _ in self.sense_rows[key]], ctx, self.classes)
            ko = ko.split(':')[0]
            src = 'lexicon'
            for dsp in self.disputes:
                if dsp['ja'] == r['ja'] and dsp['field'] == 'ko' and dsp['lexicon'] == ko:
                    ko, src = dsp['house'], 'disputed'
        else:
            ko, src = r['ko'], 'vocab'
        k = (r['ja'], ko, src)
        self.used[k] = self.used.get(k, 0) + 1
        return (ko, cue) if why else ko

    def animate(self, ja):
        r = self.row(ja)
        lr = self.lexrow(r)
        return (lr['anim'] == '+') if lr is not None and lr.get('anim') else 'anim=+' in r['feat']

    def pred_forms(self, pred, d, link=None, v1tense=None):
        """The lexicon form names (lexcheck FORMS) of a predicate in the grid cell."""
        nominal = isinstance(pred, tuple) and pred[0] == 'cop' or \
            isinstance(pred, str) and self.row(pred)['gcls'] in ('noun', 'noun-cop', 'pron', 'qword', 'na-adj')
        f = {'cop'} if nominal else set()
        if link is not None:  # the first predicate of a two-clause line
            if link in ('te', 'tekara'):
                f.add('te')
            elif link == 'nagara':
                f.add('renyou')
            elif link in ('ato', 'tari') or v1tense == 'past':
                f.add('ta')
            return f
        f.add('no-attr')
        if d['tense'] == 'past' or d['mod'] in ('hougaii', 'takoto') and d['pol'] == 'pos':
            f.add('ta')
        if d['pol'] == 'neg':
            f.add('neg')
        if d['aspect'] == 'teiru' or d['mod'] in ('temoii', 'tewaikenai'):
            f.add('te')
        if d['stype'] == 'request':
            f.add('te-request')
        if d['stype'].startswith('invite'):
            f.add('vol')
        return f

    def lexrow(self, r):
        return self.lex.get((r['ja'], r['pos']))

    def te(self, r):
        """How ている realises for a verb (go, eo, past, simple): the lexicon's te, else the vocab asp."""
        lr = self.lexrow(r)
        te = lr['te'] if lr and lr.get('te') else TE_OF_ASP[self.feats(r)['asp']]
        for dsp in self.disputes:
            if dsp['ja'] == r['ja'] and dsp['field'] == 'te' and dsp['lexicon'] in (te, '*'):
                te = dsp['house']
        return te

    # ------------------------------------------------------------ noun phrases

    def contracted(self, ja, level, particle):
        for r in self.contract:
            if r['ja'] == ja and r['particle'] == particle and (r['level'] == '*' or level in r['level'].split('|')):
                return r['ko']
        return None

    def noun(self, ja, level, nexts=(), forms=()):
        c = self.contracted(ja, level, '-')
        return c if c is not None else self.gloss(ja, forms=forms, nexts=nexts, args={})

    def fusion(self, host, nxt, level):
        """The generator's spoken.tsv row for host + next (subj top obj instr COP -), if it applies at the level:
        replace/fuse rows in dialogue only, always/always-fuse rows everywhere."""
        for r in self.spoken:
            if r['host'] == host and r['next'] == nxt:
                if r['mode'].startswith('always') or level != 'haera':
                    return r
        return None

    def with_particle(self, ja, ko, particle, level):
        if ja is not None:
            c = self.contracted(ja, level, particle)
            if c is not None:
                return c
        head, _, last = ko.rpartition(' ')
        r = self.fusion(last, KASE.get(particle, '-' if particle in ('-', '') else None), level)
        if r is not None and r['mode'] in ('replace', 'always'):
            return (head + ' ' if head else '') + r['result']
        return attach(ko, particle)

    def cop_host(self, host, level):
        head, _, last = host.rpartition(' ')
        r = self.fusion(last, 'COP', level)
        if r is not None and r['mode'] in ('fuse', 'always-fuse'):
            return (head + ' ' if head else '') + r['result']
        return host

    def gen_particle(self, kase, anim, level):
        """The generator's particles.tsv form of a case: the first row whose conditions hold (hon is never set in
        the N5 grid; spoken holds in dialogue)."""
        have = {'anim'} if anim else set()
        if level != 'haera':
            have.add('spoken')
        for r in self.gen_particles:
            if r['kase'] != kase:
                continue
            when = set() if r['when'] == '-' else set(r['when'].split(','))
            if when <= have:
                return r['form']
        raise KeyError('no generator particle for ' + kase)

    def ko_particle(self, jpart, role, level, anim=False):
        if jpart == '-':
            return '-'
        best = None
        for r in self.particle:
            if r['ja'] != jpart:
                continue
            if r['role'] not in ('*', role) or r['level'] not in ('*', level):
                continue
            score = (r['role'] != '*') * 2 + (r['level'] != '*')
            if best is None or score > best[0]:
                best = (score, r['ko'])
        if best is None:
            raise KeyError('no Korean particle for %s (%s, %s)' % (jpart, role, level))
        if best[1].startswith('@'):
            return self.gen_particle(best[1][1:], anim, level)
        return best[1]

    def np(self, np, level):
        """Korean of a noun phrase and the lemma whose contracted forms apply (None for a compound phrase)."""
        k = np[0]
        if k == 'n':
            return self.noun(np[1], level), np[1]
        if k == 'list':
            a, _ = self.np(('n', np[1]), level)
            b, bja = self.np(('n', np[3]), level)
            return attach(a, self.ko_particle(np[2], 'list', level)) + ' ' + b, None
        if k == 'poss':
            a = self.contracted(np[1], level, 'poss') or self.noun(np[1], level)
            b, _ = self.np(('n', np[3]), level)
            return a + ' ' + b, None
        if k == 'adnom':
            adn = {'この': '此の', 'その': '其の', 'あの': '彼の', 'どの': '何の'}[np[1]]
            b, _ = self.np(('n', np[2]), level)
            return self.gloss(adn, head=np[2], forms=(), args={}) + ' ' + b, None
        if k == 'adj':
            r = self.row(np[1])
            b, _ = self.np(('n', np[2]), level)
            ko = self.gloss(np[1], forms=('attr',), head=np[2], args={'subj': [np[2]]})
            return self.conj(ko, self.kpos(r), '', ['-는']) + ' ' + b, None
        if k == 'num':
            return self.number(np[1], np[2]), None
        if k == 'clause':
            _, verb, part, noun = np
            n, nja = self.np(('n', noun), level)
            r = self.row(verb)
            v = self.conj(self.kverb(r, self.gloss(verb, forms=('attr',), args={'obj': [noun]}, preds=[])), 'V', '',
                          ['-는'])
            return self.with_particle(nja, n, self.ko_particle(part, 'obj', level), level) + ' ' + v + ' 것', None
        raise ValueError(np)

    def number(self, num, counter):
        c = self.row(counter, 'counter')
        head = self.cc['counted'][0] if self.cc and self.cc['counted'] else None
        cko = self.gloss(counter, 'counter', head=head, forms=(), args={})
        if num.isdigit():
            return num + cko
        n = self.num[num]
        if counter == 'つ':
            return '하나' if n['value'] == '1' else n['native'] + ' ' + cko
        lr = self.lexrow(c)
        kind = lr['num'] if lr and lr.get('num') else self.feats(c).get('num', 'native')
        return (n['native'] if kind == 'native' else n['sino']) + ' ' + cko

    def slot(self, slot, level, frame, clause):
        role, jpart, np = slot
        if role == 'adv':
            return self.adverb(np[1], clause)
        if role == 'advadj':
            r = self.row(np[1])
            ko, cue = self.gloss(np[1], forms=('adv',), args={}, why=True)
            if 'form=' in cue and 'adv' in cue:
                return ko  # a lexicalised adverb sense
            return self.conj(ko, self.kpos(r), '', ['-게'])
        if np[0] == 'n':
            ko, ja = self.noun(np[1], level, nexts=[jpart] if jpart != '-' else []), np[1]
        else:
            ko, ja = self.np(np, level)
        if jpart in frame and role in ('obj', 'dat'):
            part = frame[jpart]
        else:
            anim = np[0] == 'n' and self.animate(np[1])
            part = self.ko_particle(jpart, role, level, anim)
        return self.with_particle(ja, ko, part, level)

    def adverb(self, ja, clause):
        return self.gloss(ja, args={})

    def adverb_then(self, slots, clause):
        for role, _, np in slots:
            if role != 'adv':
                continue
            for r in self.adv:
                if r['ja'] == np[1] and all(r[k] in ('*', clause[k]) for k in ('pol', 'tense', 'aspect')):
                    return r['then']
        return '-'

    # ------------------------------------------------------------ predicates

    @staticmethod
    def kpos(r):
        g = r['gcls']
        if g in ('i-adj', 'hoshii', 'advadj'):
            return 'A'
        if g == 'na-adj':
            return 'V' if r['kpos'] == 'V' else 'A'
        if g in ('aru', 'iru'):
            return 'X'
        return 'V'

    @staticmethod
    def kverb(r, ko):
        return ko + '하다' if r['gcls'] == 'sahen' else ko

    def base(self, pred, level):
        """The predicate before aspect, modality, polarity and tense: a list of literal words and PWs."""
        if isinstance(pred, tuple):
            kind, arg = pred
            if kind == 'cop':
                host, _ = self.np(arg, level)
                return [PW('이다', 'C', host=host)]
            if kind == 'naru':
                r = self.row(arg)
                if r['gcls'] in ('i-adj', 'na-adj'):
                    stem = self.conj(self.gloss(arg, forms=('renyou',)), 'A', '', ['-어'])
                    return [PW(stem + '지다', 'V')]
                return [attach(self.noun(arg, level, nexts=['に']), '이/가'), PW('되다', 'V')]
            raise ValueError(pred)
        r = self.row(pred)
        g = r['gcls']
        neg = self.feats(r).get('neg', '')
        if g in ('noun', 'noun-cop', 'pron', 'qword'):
            return [PW('이다', 'C', host=self.noun(pred, level, forms=('cop',)))]
        if g == 'hoshii':
            *lit, last = self.gloss(pred).split(' ')
            return lit + [PW(last, 'A')]
        if g in ('aru', 'iru'):
            return [PW('있다', 'X', neg=neg)]
        ko = self.kverb(r, self.gloss(pred))
        split = ''
        lr = self.lexrow(r)
        # a verb whose Korean is a noun + 하다 lets 안 in between (공부 안 해, N5-H1); an adjective does not (안 조용해)
        if g in VERB_GCLS and ko.endswith('하다') and len(ko) > 2 and \
                (g == 'sahen' or (lr is not None and lr['kcls'] == 'HA')):
            split = ko[:-2]
        return [PW(ko, self.kpos(r), split=split, neg=neg)]

    @staticmethod
    def last_pw(items):
        for x in reversed(items):
            if isinstance(x, PW):
                return x
        raise ValueError('no predicate word')

    def steps(self, items, steps):
        """Apply a steps string: -ending on the current word, +LEMMA/POS[@HOST] a new word, else a literal."""
        for t in steps.split():
            if t.startswith('-'):
                self.last_pw(items).forms.append(t)
            elif t.startswith('+'):
                lp, _, host = t[1:].partition('@')
                lemma, _, pos = lp.partition('/')
                items.append(PW(lemma, pos, host=host))
            elif t.startswith('='):
                noun, _, kase = t[1:].partition('/')
                part = {v: k for k, v in KASE.items()}[kase]
                items.append(self.with_particle(None, noun, part, self.cur_level))
            else:
                items.append(t)

    def negate(self, items, base, method):
        if base.pos == 'C':
            i = items.index(base)
            items[i:i + 1] = [attach(base.host, '이/가'), PW('아니다', 'N')]
            for f in base.forms:
                items[i + 1].forms.append(f)
            return items[i + 1]
        if method.startswith('supp'):
            word = method.split(':', 1)[1] if ':' in method else base.neg
            base.lemma = word
            if word in ('없다',):
                base.pos = 'X'
            return base
        if base.neg and method != 'long-outer':
            base.lemma = base.neg
            if base.neg == '없다':
                base.pos = 'X'
            return base
        if method == 'short':
            i = items.index(base)
            if base.split:
                items[i:i + 1] = [base.split, '안', base]
                base.lemma = '하다'
            else:
                items.insert(i, '안')
            return base
        # long: -지 않다 on the current last word
        last = self.last_pw(items)
        self.steps(items, '-지 +않다/%s' % ('V' if last.pos == 'V' else 'A'))
        return base

    def neg_method(self, level, pclass):
        best = None
        for r in self.neg:
            if r['level'] in ('*', level) and r['pred'] in ('*', pclass):
                score = (r['pred'] != '*') * 2 + (r['level'] != '*')
                if best is None or score > best[0]:
                    best = (score, r['method'])
        return best[1]

    def pred_class(self, pred):
        if isinstance(pred, tuple):
            return pred[0]
        return self.row(pred)['gcls']

    def predicate(self, pred, d, level, slots, items=None, steps_first=None):
        """The Korean predicate words of a final clause."""
        clause = {'pol': d['pol'], 'tense': d['tense'], 'aspect': d['aspect']}
        then = self.adverb_then(slots, clause)
        tense, aspect = d['tense'], d['aspect']
        if then == 'simple-past':
            tense, aspect = 'past', 'simple'
        items = list(items or []) + self.base(pred, level)
        base = self.last_pw(items)
        if steps_first:
            self.steps(items, steps_first)
        if aspect == 'teiru':
            r = self.row(pred)
            f = self.feats(r)
            te = self.te(r)
            st = next(a['steps'] for a in self.aspect if a['te'] == te
                      and a['pclass'] in ('*', f.get('pclass', '-')) and a['pol'] in ('*', d['pol']))
            if st == 'past':
                tense = 'past'
            elif st != 'simple':
                self.steps(items, st)
        stype, mod = d['stype'], d['mod']
        pclass = self.pred_class(pred)
        method = self.neg_method(level, pclass)
        if mod != 'none':
            m = [r for r in self.mod if r['mod'] == mod and r['level'] in ('*', level)]
            m = sorted(m, key=lambda r: r['level'] == '*')[0]
            if d['pol'] == 'neg' and m['neg'].startswith('inner'):
                self.negate(items, base, 'long' if m['neg'] == 'inner-long' else method)
            if tense == 'past' and m['tense'] == 'inner':
                self.last_pw(items).forms.append('-었-')
            self.steps(items, m['steps'])
            if d['pol'] == 'neg' and m['neg'].startswith('outer'):
                last = self.last_pw(items)
                if m['neg'].startswith('outer-supp'):
                    last.lemma = m['neg'].split(':', 1)[1]
                else:
                    self.negate(items, last, 'long')
            if tense == 'past' and m['tense'] == 'outer':
                self.last_pw(items).forms.append('-었-')
        else:
            if d['pol'] == 'neg':
                self.negate(items, base, method)
            if tense == 'past':
                self.last_pw(items).forms.append('-었-')
        f = self.final[(level, stype, d['sfp'])]
        self.steps(items, f['steps'])
        return items, f['punct']

    def render(self, items):
        out = []
        for x in items:
            if isinstance(x, PW):
                host = self.cop_host(x.host, self.cur_level) if x.pos == 'C' and x.host else x.host
                out.append(self.conj(x.lemma, x.pos, host, x.forms))
            else:
                out.append(x)
        return ' '.join(out)

    # ------------------------------------------------------------ lines

    def ko(self, spec):
        d = spec['d']
        level = self.level[(d['ctx'], d['polite'])]
        self.cur_level = level
        if spec['block'] == 'link':
            text, punct = self.link_line(spec, d, level)
        else:
            pred, slots = spec['pred'], spec['slots']
            self.clause(pred, slots, self.pred_forms(pred, d), d)
            frame = self.frame_map(pred)
            clause = {'pol': d['pol'], 'tense': d['tense'], 'aspect': d['aspect']}
            words = [self.slot(s, level, frame, clause) for s in slots]
            items, punct = self.predicate(pred, d, level, slots)
            text = ' '.join(words + [self.render(items)])
        text += punct
        if d['ctx'] == 'dialogue':
            text = self.punct['「'] + text + self.punct['」']
        return text

    def link_line(self, spec, d, level):
        p = spec['pair']
        link = d['link']
        main = 'statement' if d['stype'] == 'statement' else 'q'
        r1 = self.row(p['pred1'])
        f1 = self.feats(r1)
        cond = {'pclass': f1.get('pclass', '-'), 'shared': p['shared']}
        row = None
        for r in self.link:
            if r['link'] != link or r['v1tense'] not in ('-', d['v1tense']) or r['main'] not in ('*', main):
                continue
            if r['cond'] != '*':
                k, v = r['cond'].split('=')
                if cond.get(k) != v:
                    continue
            row = r
            break
        if row is None:
            raise KeyError('no link row for %s %s %s' % (link, d['v1tense'], main))
        frame1, frame2 = self.frame_map(p['pred1']), self.frame_map(p['pred2'])
        clause = {'pol': 'pos', 'tense': d['tense'], 'aspect': 'simple'}
        self.clause(p['pred1'], spec['slots1'], self.pred_forms(p['pred1'], d, link, d['v1tense']), d)
        w1 = [self.slot(s, level, frame1, clause) for s in spec['slots1']]
        items1 = self.base(p['pred1'], level)
        self.steps(items1, row['steps'])
        first = ' '.join(w1 + [self.render(items1)])
        if link in ('kara', 'ga', 'kedo'):
            first += self.punct['、']
        clause2 = {'pol': d['pol'], 'tense': d['tense'], 'aspect': 'simple'}
        shared = {'obj': self.cc['args'].get('obj', [])} if p['shared'] == '1' else None
        self.clause(p['pred2'], spec['slots2'], self.pred_forms(p['pred2'], d), d, shared)
        w2 = [self.slot(s, level, frame2, clause2) for s in spec['slots2']]
        if link == 'tari':
            items, punct = self.predicate('為る', d, level, [], items=self.base(p['pred2'], level),
                                          steps_first=None)
            # the second verb takes -거나 too; 하다 carries the end
            v2 = items[0]
            v2.forms.insert(0, '-거나')
            items = [v2] + items[1:]
            second = ' '.join(w2 + [self.render(items)])
        else:
            items, punct = self.predicate(p['pred2'], d, level, spec['slots2'])
            second = ' '.join(w2 + [self.render(items)])
        return first + ' ' + second, punct
