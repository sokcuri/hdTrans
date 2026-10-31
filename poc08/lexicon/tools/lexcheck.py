#!/usr/bin/env python3
"""Validate the lexicon sources under lex/.

usage: lexcheck.py [--lex DIR] [-j N] [--list KIND] [--no-analysis]

Checks: schema and closed sets; duplicate senses and sense numbering; Korean lemmas the generator cannot
conjugate (not a dictionary form, unknown or non-unique class, homograph without its tag, nonstandard
spelling, junk); surface keys (a lexicon ja that is not one analyzer lemma and is not marked `lu`);
cue syntax, slots and classes; multi-sense entries whose later senses have no cue; phrase slots; names.
Errors on st=r/m rows and schema errors fail the run; on st=u rows they are counted per check.
Class rule (strict on st=r, counted on st=m/u): an alternative list in a cue names at most two literal lemmas, and
two literal lemmas that share a specific class (a leaf, or a class at depth 3 or more; through the cls feature of
their entries) must be written as the class.
Lemma features: vol, asp, te, neg (suppletive negative), name (+: the spelling is also a given or family name), cls, anim, ni, num, lu (closed value sets on st=r rows).
"""
import argparse
import collections
import glob
import multiprocessing
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexlib  # noqa: E402
from lexconv import KIND_CARRIERS, CARRIER, LEXCOLS, read_tsv, resolve_strings, resolves  # noqa: E402

POS = {'n', 'pn', 'num', 'suf', 'pref', 'v1', 'v5', 'vs', 'vk', 'vz', 'adj', 'na', 'adv', 'adn', 'conj', 'intj'}
SLOTS = {'subj', 'obj', 'dat', 'loc', 'with', 'from', 'to', 'arg', 'head', 'mod', 'pred', 'after', 'next', 'form',
         'domain', 'register', 'main.vol', 'ctx', 'subj-of', 'obj-of', 'dat-of', 'verb'}
FORMS = {'te', 'ta', 'imp', 'neg', 'vol', 'attr', 'no-attr', 'cop', 'te-request', 'cond', 'adv', 'renyou', 'past',
         'polite', 'q', '-'}
ST = {'r', 'm', 'u'}
KCLS = {'REG', 'HA', 'D', 'B', 'BWA', 'S', 'H', 'REU', 'REO', 'U', 'EU', 'L', 'COP', 'EXIST'}
NAME_TYPES = {'family', 'given', 'person', 'place', 'org', 'title', 'other', 'ship', 'work'}
NONSTANDARD = {'개이다': '개다', '바래다': '바라다', '설레이다': '설레다', '되뇌이다': '되뇌다', '헤매이다': '헤매다',
               '메이다': '메다', '날으다': '날다', '삼가하다': '삼가다', '치루다': '치르다', '들리다:visit': '들르다',
               '목메이다': '목메다', '꺼려하다': '꺼리다', '애닯다': '애달프다', '서툴다': '서투르다'}
JUNK = re.compile(r'[぀-ヿ一-鿿()（）/\[\]{}<>@&$%]|^[0-9]+$')


class Report:
    def __init__(self):
        self.err = collections.Counter()
        self.warn = collections.Counter()
        self.samples = collections.defaultdict(list)

    def add(self, check, where, strict):
        (self.err if strict else self.warn)[check] += 1
        if len(self.samples[check]) < 400:
            self.samples[check].append(where)


def parse_cue(cue):
    """[(slot, [values])] or raises ValueError."""
    out = []
    for atom in cue.split():
        if '=' not in atom:
            raise ValueError(atom)
        slot, val = atom.split('=', 1)
        if not slot or not val:
            raise ValueError(atom)
        out.append((slot, val.split('|')))
    return out


def _an_chunk(chunk):
    return [(s, [(t.surf, t.lemma, t.yomi, t.pos, t.pos1, t.pos2, t.ctype, t.cform, t.kana, t.unk, t.orth)
                 for t in lexlib.analyze(s)]) for s in chunk]


def analyze(strings, jobs):
    strings = sorted(set(strings))
    n = max(1, len(strings) // (jobs * 8))
    out = {}
    with multiprocessing.get_context('fork').Pool(jobs) as p:
        for part in p.imap_unordered(_an_chunk, [strings[i:i + n] for i in range(0, len(strings), n)]):
            out.update(part)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lex', default=lexlib.LEX)
    ap.add_argument('-j', type=int, default=min(16, os.cpu_count() or 1))
    ap.add_argument('--list', default='')
    ap.add_argument('--no-analysis', action='store_true')
    a = ap.parse_args()
    R = Report()

    classes = {}
    for r in read_tsv(os.path.join(a.lex, 'classes.tsv')):
        classes[r['class']] = r['parent']
    for c, p in classes.items():
        if p and p not in classes:
            R.add('class-parent', c, True)
    prov_codes = set()
    for r in read_tsv(os.path.join(a.lex, 'provenance.tsv')):
        prov_codes.add(r['code'])
    stems = {}
    for r in read_tsv(os.path.join(a.lex, 'stems.tsv')):
        stems[r['lemma']] = r
    attested = set()
    if os.path.isfile(os.path.join(a.lex, 'attested.tsv')):
        attested = {(r['ja'], r['yomi'], r['pos']) for r in read_tsv(os.path.join(a.lex, 'attested.tsv'))}

    def check_class(v, where, strict):
        if v.startswith('@') and v not in classes:
            R.add('unknown-class', where, strict)

    def check_prov(p, where, strict):
        for c in p.split('+'):
            if c.rstrip('?') not in prov_codes:
                R.add('unknown-prov', where, strict)

    lex_rows = []
    files = sorted(glob.glob(os.path.join(a.lex, 'lexicon', '*.tsv'))) + sorted(glob.glob(os.path.join(a.lex, 'by-sa', 'lexicon*.tsv')))
    owner = {}
    for f in files:
        with open(f, encoding='utf-8') as fh:
            if fh.readline().strip() != '#! lexicon':
                R.add('schema-header', f, True)
        hdr_ok = None
        for i, r in enumerate(read_tsv(f)):
            if hdr_ok is None:
                hdr_ok = list(r.keys())[:len(LEXCOLS)] == LEXCOLS
                if not hdr_ok:
                    R.add('schema-columns', f, True)
            r['_file'] = os.path.basename(f)
            lex_rows.append(r)
    # class membership from the cls feature of each entry's first row (with ancestors)
    def ancestors(c):
        out = []
        while c and c in classes and c not in out:
            out.append(c)
            c = classes[c]
        return out
    member = collections.defaultdict(set)
    for r in lex_rows:
        if r['sense'] == '1':
            for f in r['feat'].split():
                if f.startswith('cls='):
                    for v in f[4:].split('|'):
                        member[r['ja']].update(c for c in ancestors(v) if classes.get(c))
    parents_of = set(classes.values())
    FEATS = {'vol': {'+', '-'}, 'asp': {'state', 'activity', 'change', 'instant', 'type4'},
             'te': {'go', 'eo', 'past', 'simple'}, 'name': {'+'}, 'anim': {'+', '-'}, 'ni': {'-'}, 'num': {'native', 'sino'}}

    def check_list(slot, vals, where, st):
        lits = [v for v in vals if not v.startswith('@') and v != '-']
        if slot in ('form', 'register', 'main.vol', 'domain') or len(lits) < 2:
            return
        if len(lits) >= 3:
            R.add('cue-surface-list', where, st == 'r')
        common = set.intersection(*[member.get(v, set()) for v in lits])
        if common:
            # strict when the shared class is specific: a leaf, or at depth 3 or more
            specific = [c for c in common if c not in parents_of or len(ancestors(c)) >= 3]
            R.add('cue-shared-class', where + ' → ' + '|'.join(sorted(common)), st == 'r' and bool(specific))

    entries = collections.OrderedDict()
    for r in lex_rows:
        lid = (r['ja'], r['yomi'], r['pos'])
        strict = r['st'] in ('r', 'm')
        where = f'{r["_file"]}: {"/".join(lid)}#{r["sense"]} {r["ko"]}'
        if owner.setdefault(lid, r['_file']) != r['_file']:
            R.add('lemma-in-two-files', where, True)
        if r['pos'] not in POS:
            R.add('schema-pos', where, True)
        if r['st'] not in ST:
            R.add('schema-st', where, True)
        if not r['sense'].isdigit():
            R.add('schema-sense', where, True)
        if not r['ja'] or not r['ko']:
            R.add('schema-empty', where, True)
        check_prov(r['prov'], where, True)
        entries.setdefault(lid, []).append(r)
        # Korean side
        ko = r['ko']
        if JUNK.search(ko.replace(':ㄷ', '').replace(':ㅂ', '').replace(':러', '').replace(':르', '').replace(':ㅅ', '')):
            R.add('ko-junk', where, strict)
        lw = lexlib.last_word(ko)
        base = lw.split(':')[0]
        if base in NONSTANDARD:
            R.add('ko-nonstandard', where + ' → ' + NONSTANDARD[base], strict)
        is_pred = r['pos'] in lexlib.PRED or (r['pos'] == 'na' and base.endswith('다'))
        if is_pred and re.search(r'form=[^ ]*(adv|renyou)', r['cue']):
            is_pred = False
        if is_pred:
            if not base.endswith('다'):
                if r['pos'] in ('v1', 'v5', 'vs', 'vk', 'adj'):
                    R.add('ko-not-predicate', where, strict)
            else:
                st = stems.get(base)
                cls = st['class'] if st else None
                if not cls:
                    cls, sure = lexlib.guess_class(lw, 'A' if r['pos'] in ('adj', 'na') else 'V')
                    if not sure and ':' not in lw:
                        R.add('ko-homograph-untagged', where, strict)
                if cls not in KCLS:
                    R.add('ko-class-unknown', where, strict)
                elif st and st['flags'].startswith('conflict') and ':' not in lw:
                    R.add('ko-class-not-unique', where + ' ' + st['flags'], strict)
                elif st and 'homograph' in st['flags'] and ':' not in lw:
                    R.add('ko-homograph-untagged', where, strict)
        # cues
        for col in ('cue',):
            if r[col]:
                try:
                    for slot, vals in parse_cue(r[col]):
                        if slot not in SLOTS:
                            R.add('cue-slot', where + ' ' + r[col], strict)
                        check_list(slot, vals, where + ' ' + r[col], r['st'])
                        for v in vals:
                            check_class(v, where + ' ' + r[col], strict)
                            if slot == 'form' and v not in FORMS:
                                R.add('cue-form', where + ' ' + r[col], strict)
                except ValueError:
                    R.add('cue-syntax', where + ' ' + r[col], strict)
        for f in r['feat'].split():
            k, _, v = f.partition('=')
            if k == 'neg':
                if not v or (r['pos'] != 'adv' and not v.endswith('다')):
                    R.add('feat-value', where + ' ' + f, r['st'] == 'r')
            elif r['st'] == 'r' and k != 'lu' and (k not in FEATS and k != 'cls' or k in FEATS and v not in FEATS[k]):
                R.add('feat-value', where + ' ' + f, True)
            if f.startswith('cls='):
                for v in f[4:].split('|'):
                    check_class(v, where, strict)
    # per entry
    for lid, rs in entries.items():
        nums = [int(r['sense']) for r in rs if r['sense'].isdigit()]
        strict = any(r['st'] in ('r', 'm') for r in rs)
        where = '/'.join(lid)
        if sorted(nums) != list(range(1, len(nums) + 1)):
            R.add('sense-numbering', where, True)
        seen = set()
        for r in rs:
            k = (r['ko'], r['cue'])
            if k in seen:
                R.add('duplicate-sense', where + ' ' + r['ko'], strict)
            seen.add(k)
        kos = {r['ko'] for r in rs}
        if len(kos) > 1:
            for r in rs:
                if r['sense'] != '1' and not r['cue']:
                    R.add('missing-cue', where + '#' + r['sense'] + ' ' + r['ko'], strict)
        if len({r['st'] for r in rs}) > 1:
            R.add('mixed-status', where, True)

    # surface keys
    if not a.no_analysis:
        strings = []
        for (ja, yomi, pos), rs in entries.items():
            if 'lu' in rs[0]['feat'].split():
                continue
            strings += resolve_strings(ja, yomi, pos)
        an = analyze(strings, a.j)
        for (ja, yomi, pos), rs in entries.items():
            if 'lu' in rs[0]['feat'].split():
                continue
            if (ja, yomi, pos) not in attested and not resolves(an, ja, yomi, pos):
                R.add('surface-key', '/'.join((ja, yomi, pos)), any(r['st'] in ('r', 'm') for r in rs) or True)

    # phrases
    for f in sorted(glob.glob(os.path.join(a.lex, 'phrases*.tsv'))) + sorted(glob.glob(os.path.join(a.lex, 'by-sa', 'phrases*.tsv'))):
        ids = set()
        for r in read_tsv(f):
            strict = r['st'] in ('r', 'm')
            where = f'{os.path.basename(f)}: {r["id"]}'
            if r['id'] in ids:
                R.add('phrase-id-dup', where, True)
            ids.add(r['id'])
            if not r['pattern'] or not r['ko']:
                R.add('schema-empty', where, True)
            check_prov(r['prov'], where, True)
            slots = re.findall(r'\{([A-Z][A-Za-z0-9]*)(?::([^}]+))?\}', r['pattern'])
            for name, typ in slots:
                if typ:
                    for v in re.split(r'[.|]', typ):
                        if v.startswith('@'):
                            check_class(v, where, strict)
                if '{' + name + '}' not in r['ko']:
                    R.add('phrase-slot-unused', where, strict)
            if JUNK.search(re.sub(r'\((이|으|을|를|과|와|은|는|이었|였)\)', '', re.sub(r'\{[A-Z][A-Za-z0-9]*\}', '', r['ko']))):
                R.add('ko-junk', where + ' ' + r['ko'], strict)
            if r['cue']:
                try:
                    for slot, vals in parse_cue(r['cue']):
                        if slot not in SLOTS:
                            R.add('cue-slot', where, strict)
                        for v in vals:
                            check_class(v, where, strict)
                except ValueError:
                    R.add('cue-syntax', where, strict)
    # names and titles
    for f in sorted(glob.glob(os.path.join(a.lex, 'names', '*.tsv'))) + [os.path.join(a.lex, 'titles.tsv')] + \
            sorted(glob.glob(os.path.join(a.lex, 'by-sa', 'names-*.tsv'))):
        for r in read_tsv(f):
            where = f'{os.path.basename(f)}: {r["ja"]}'
            if r['type'] not in NAME_TYPES:
                R.add('name-type', where, True)
            if r['conf'] not in ('high', 'med', 'low'):
                R.add('name-conf', where, True)
            check_prov(r['prov'], where, True)
            if r['prov'].rstrip('?') in ('nn', 'np', 'nu', 'ps'):
                R.add('name-provenance', where, True)

    print('check\terrors\twarnings(st=u)')
    for k in sorted(set(R.err) | set(R.warn)):
        print(f'{k}\t{R.err[k]}\t{R.warn[k]}')
    print(f'lexicon rows {len(lex_rows)}, entries {len(entries)}')
    if a.list:
        for w in R.samples.get(a.list, []):
            print('  ', w)
    sys.exit(1 if R.err else 0)


if __name__ == '__main__':
    main()
