#!/usr/bin/env python3
"""Convert dicts/*.tsv into the 0.8 lexicon sources under lex/.

usage: lexconv.py [--dicts DIR] [--trace FILE] [--out LEXDIR] [--build DIR] [-j N]

Writes lexicon/gen-*.tsv (one row per sense, st=u), phrases-gen.tsv, names/*.tsv, titles.tsv, stems.tsv,
longunits.tsv and report.tsv. Every input row is converted or gets a reason in BUILD/unconverted.tsv.
Lemma ids whose rows are in a reviewed file (lexicon/top.tsv) are not generated again.
--trace is a private (file, key, ko, code) table that gives the name and supplement rows their provenance
code; without it those rows get code `nu`/`w`.
"""
import argparse
import collections
import multiprocessing
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kana  # noqa: E402
import lexlib  # noqa: E402
from lexlib import lemma_id  # noqa: E402

LEXCOLS = ['ja', 'yomi', 'pos', 'sense', 'ko', 'cue', 'frame', 'feat', 'st', 'prov', 'note']
PHCOLS = ['id', 'pattern', 'ko', 'cue', 'st', 'prov', 'note']
NAMECOLS = ['ja', 'yomi', 'type', 'ko', 'conf', 'needs', 'prov', 'note']
SHIP_NO = {'nn', 'np', 'nu', 'ps'}


# ---------------------------------------------------------------- analysis (parallel, by unique string)

def _an(s):
    return [(t.surf, t.lemma, t.yomi, t.pos, t.pos1, t.pos2, t.ctype, t.cform, t.kana, t.unk, t.orth) for t in lexlib.analyze(s)]


def _an_chunk(chunk):
    return [(s, _an(s)) for s in chunk]


def analyze_all(strings, jobs):
    strings = sorted(set(strings))
    if jobs <= 1:
        return dict(_an_chunk(strings))
    n = max(1, len(strings) // (jobs * 8))
    chunks = [strings[i:i + n] for i in range(0, len(strings), n)]
    out = {}
    with multiprocessing.get_context('fork').Pool(jobs) as p:
        for part in p.imap_unordered(_an_chunk, chunks):
            out.update(part)
    return out


# ---------------------------------------------------------------- provenance

def row_prov(r, trace):
    f = r['file']
    if f.startswith('names'):
        return trace.get((f, r['key'], r['ko']), 'nu')
    if f in ('words-slang.tsv', 'words-idioms.tsv'):
        c = trace.get((f, r['key'], r['ko']), 'w')
        if c == 'w' and 'llm' in r.get('flags', ''):
            return 'wl'
        return c
    if f.startswith('words'):
        return 'wl' if 'llm' in r.get('flags', '') else 'w'
    if f == 'base.tsv':
        return 'b' if r.get('id', '').startswith('jisjk:') else 'x'
    return 'x'


# ---------------------------------------------------------------- Korean side

JA_CHARS = re.compile(r'[぀-ヿ一-鿿]')


def korean(r, kind):
    """(lemma text, kpos, kcls, junk reason) for a row's Korean."""
    ko = r['ko'].strip()
    if not ko:
        return None, None, None, 'empty-ko'
    tag = None
    if r['userfmt'] and ':' in r['pos']:
        tag = r['pos'].split(':', 1)[1].strip()
    if r['userfmt'] and kind == 'na' and '/' in ko:
        ko = ko.split('/')[0]
    if r['userfmt'] and kind == 'adv' and '/' in ko:
        ko = ko.split('/')[0]
    text, kpos, kcls, ok = lexlib.expand_ko(ko)
    if not ok:
        return None, None, None, 'unknown-marker'
    text = re.sub(r'\s+', ' ', text).strip()
    if JA_CHARS.search(text):
        return None, None, None, 'japanese-in-ko'
    if re.search(r'[()（）]', text) or re.match(r'^[0-9]', text):
        return None, None, None, 'gloss-junk'
    if kpos is None and kind in ('verb', 'adj', 'na') and text.endswith('다'):
        kpos = 'V' if kind == 'verb' else 'A'
    if tag and tag in lexlib.CLASS_TAG:
        kcls = lexlib.CLASS_TAG[tag]
    return text, kpos, kcls, None


# ---------------------------------------------------------------- rules → draft cues

ATOM = re.compile(r'([A-Z])([+-]\d+)\s*(==|!=|-=|=)\s*"([^"]*)"')


def rule_cues(ctx):
    """Draft cue strings from the simple conjunctions of a 0.7 local rule; returns (cues, unmapped)."""
    if not ctx:
        return [], 0
    body = ctx.split('//')[0]
    groups = []
    toks = re.findall(r'\|\|\d+|&&\d+|[A-Z][+-]\d+\s*(?:==|!=|-=|=)\s*"[^"]*"', body)
    i = 0
    while i < len(toks):
        t = toks[i]
        if t.startswith('||'):
            i += 1
            continue
        if t.startswith('&&'):
            k = int(t[2:])
            groups.append(toks[i + 1:i + 1 + k])
            i += 1 + k
        else:
            groups.append([t])
            i += 1
    cues = []
    unmapped = 0
    for g in groups:
        parts = []
        for a in g:
            m = ATOM.match(a)
            if not m:
                parts = None
                break
            fld, off, op, val = m.groups()
            if fld == 'S' and op == '==' and off == '+1':
                parts.append('next=' + val)
            elif fld == 'S' and op == '==' and off == '-1':
                parts.append('after=' + val)
            else:
                parts = None
                break
        if parts:
            cues.append(' '.join(sorted(set(parts))))
        else:
            unmapped += 1
    return cues, unmapped


# ---------------------------------------------------------------- names

SEM = re.compile(r'[A-Z][A-Za-z]{2}')


def name_type(r):
    sem = set(SEM.findall(r.get('sem', '')))
    gram = r.get('gram', '')
    if 'QWK' in sem:
        return 'work'
    if 'QPN' in sem or 'QPH' in sem:
        if 'Wa' in gram:
            return 'family'
        if 'Wb' in gram:
            return 'given'
        return 'person'
    if sem & {'QCT', 'QST', 'QNN', 'GCT', 'QCt'} or r['file'] == 'names-places.tsv':
        return 'place'
    if sem & {'QID', 'KNa', 'QMS', 'VGt'}:
        return 'org'
    if 'VTp' in sem:
        return 'other'
    return 'term' if r['pos'][2:3] in ('1', '2') else 'other'


TITLE_MARK = re.compile(r'[!?！？「」『』【】〜～♪☆★:：/／&＆#＃"“”()（）\[\]]|\s')
KANJI = re.compile(r'[一-鿿々〆]')
_HANJA = None


def hanja():
    global _HANJA
    if _HANJA is None:
        _HANJA = {}
        p = os.path.join(lexlib.LEX, 'hanja.tsv')
        if os.path.isfile(p):
            for r in read_tsv(p):
                _HANJA[r['kanji']] = r['hangul'].split()
    return _HANJA


DUEUM = {'라': '나', '래': '내', '로': '노', '뢰': '뇌', '루': '누', '르': '느', '랑': '낭', '랭': '냉', '량': '양', '려': '여',
         '력': '역', '련': '연', '렬': '열', '렴': '염', '령': '영', '례': '예', '료': '요', '룡': '용', '류': '유', '륙': '육',
         '륜': '윤', '률': '율', '리': '이', '린': '인', '림': '임', '립': '입', '녀': '여', '뇨': '요', '뉴': '유', '니': '이',
         '년': '연', '념': '염', '녕': '영', '락': '낙', '란': '난', '람': '남', '랍': '납', '론': '논', '롱': '농', '록': '녹', '략': '약'}


def sino_match(ko, key):
    """Korean is the Sino-Korean reading of every kanji of the key (spaces ignored, word-initial sound law)."""
    ks = [c for c in key if KANJI.match(c)]
    if not ks or len(ks) != len(key):
        return False
    h = hanja()
    ko = ko.replace(' ', '')
    if len(ko) != len(ks):
        return False
    for i, (c, k) in enumerate(zip(ks, ko)):
        rs = set(h.get(c, []))
        if k in rs or (i == 0 and any(DUEUM.get(r) == k for r in rs)):
            continue
        return False
    return True


def split_name(r, toks):
    """'title', 'common' or 'name' for a name-layer row, from its codes, shape, analysis and Korean."""
    t = name_type(r)
    key, ko = r['key'], r['ko'].strip()
    if t == 'work':
        return 'title'
    if t in ('term', 'other'):
        if TITLE_MARK.search(key) and len(key) >= 4:
            return 'title'
        if any(x[3] is None and x[4] in ('助動詞', '助詞') and x[0] not in ('の', 'ノ') for x in toks) and len(toks) >= 3:
            return 'title'
        if any(x[3] in lexlib.PRED for x in toks) and len(toks) >= 2:
            return 'title'
    if t in ('person', 'family', 'given', 'place', 'org'):
        return 'name'
    if not toks or any(x[9] for x in toks) or any(x[5] == '固有名詞' for x in toks):
        return 'name'
    if not all(x[3] in ('n', 'suf', 'pref', 'na') for x in toks):
        return 'name'
    if sino_match(ko, key):
        return 'common'
    if len(key) == 1:
        return 'name'
    reading = lexlib.hira(r.get('reading', '')) if r.get('reading') else ''.join(x[8] for x in toks)
    katakana = all('ァ' <= c <= 'ヺ' or c in 'ー・' for c in key)
    if katakana:
        return 'common'
    if kana.similar(ko, reading) >= 0.7:
        return 'name'
    return 'common'


# ---------------------------------------------------------------- senses.tsv triggers

def load_senses(path):
    rows = []
    if not os.path.isfile(path):
        return rows
    for i, line in enumerate(open(path, encoding='utf-8')):
        if line.startswith('//') or not line.strip():
            continue
        p = line.rstrip('\n').split('\t')
        rows.append({'key': p[0], 'ko': p[1] if len(p) > 1 else '', 'trig': p[2] if len(p) > 2 else '',
                     'where': p[3] if len(p) > 3 else '', 'pos': p[4] if len(p) > 4 else '', 'line': i + 1})
    return rows


def trigger_cue(pos, where, lemmas):
    vals = '|'.join(lemmas)
    pred = pos in lexlib.PRED or pos == 'na'
    if where == 'sentence':
        return 'ctx=' + vals
    if pred:
        return ('arg=' if where == 'before' else 'next=') + vals
    if pos == 'n':
        return ('mod=' if where == 'before' else 'pred=') + vals
    return ('after=' if where == 'before' else 'next=') + vals


# ---------------------------------------------------------------- main conversion

NOUN_KINDS = ('noun', 'pron', 'other')
CARRIER = 'その{}が'


def noun_toks(an, key):
    """Analysis of a noun key inside a carrier (その…が), so a noun is not read as a verb stem or a suffix."""
    alone = an.get(key, [])
    c = an.get(CARRIER.format(key), [])
    if len(c) >= 3 and c[0][1] == '其の' and c[-1][0] == 'が':
        mid = c[1:-1]
        if ''.join(t[0] for t in mid) == key:
            if len(mid) == 1 and mid[0][3] == 'pn' and len(alone) == 1 and alone[0][3] in ('n', 'adv', 'na'):
                return alone
            return mid
    return alone


KIND_CARRIERS = {'verb': ('それが{}', 'それを{}'), 'adj': ('それが{}',), 'na': ('とても{}だ',), 'adv': ('{}、',),
                 'suf': ('子供{}が', '三{}が', '日本{}'), 'intj': ('{}！', '「{}」'), 'conj': ('{}、それ',), 'pref': ('{}茶',)}
KIND_POS = {'verb': ('v1', 'v5', 'vs', 'vk'), 'adj': ('adj',), 'na': ('na',), 'adv': ('adv',)}


def kind_toks(an, key, kind):
    """Analysis of a key alone, or inside a carrier when alone it is not one word of the row's kind."""
    alone = an.get(key, [])
    want = KIND_POS.get(kind)
    if not want or (len(alone) == 1 and alone[0][3] in want):
        return alone
    for c in KIND_CARRIERS.get(kind, ()):
        toks = an.get(c.format(key), [])
        for i in range(len(toks)):
            acc = ''
            for j in range(i, len(toks)):
                acc += toks[j][0]
                if acc == key:
                    mid = toks[i:j + 1]
                    if len(mid) == 1 and mid[0][3] in want:
                        return mid
                    break
                if len(acc) >= len(key):
                    break
    return alone


def kata(s):
    return ''.join(chr(ord(c) + 0x60) if 'ぁ' <= c <= 'ゖ' else c for c in s)


POSFAM = {'v1': 'v', 'v5': 'v', 'vs': 'v', 'vk': 'v', 'vz': 'v'}
ANY_CARRIERS = ('「{}」', '三{}が', 'それは{}だ', '彼が{}た')
KIND = {'v1': 'verb', 'v5': 'verb', 'vs': 'verb', 'vk': 'verb', 'adj': 'adj', 'na': 'na', 'adv': 'adv', 'suf': 'suf',
        'intj': 'intj', 'conj': 'conj', 'pref': 'pref'}


def resolve_strings(ja, yomi, pos):
    out = []
    for w in (ja, yomi, kata(yomi)):
        if not w:
            continue
        out.append(w)
        if pos in ('n', 'suf', 'pref'):
            out.append(CARRIER.format(w))
        out += [c.format(w) for c in KIND_CARRIERS.get(KIND.get(pos, ''), ())]
        out += [c.format(w) for c in ANY_CARRIERS]
    return out


def resolves(an, ja, yomi, pos):
    """Is (ja, yomi, pos) an analyzer lemma id, reached from its spelling or its reading? Verb classes match
    as one family, since UniDic gives one lemma several conjugation types."""
    fam = POSFAM.get(pos, pos)
    for s in resolve_strings(ja, yomi, pos):
        for t in an.get(s, []):
            if t[1] == ja and POSFAM.get(t[3], t[3]) == fam and (not yomi or t[2] == yomi):
                return True
    return False


POS_FILE = {'n': 'n', 'v1': 'v', 'v5': 'v', 'vs': 'v', 'vk': 'v', 'adj': 'adj', 'na': 'adj'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dicts', default=os.path.join(lexlib.REPO, 'dicts'))
    ap.add_argument('--senses', default=os.path.join(lexlib.REPO, 'src/core/dict/builtin/senses.tsv'))
    ap.add_argument('--trace', default=os.environ.get('HD8_LEX_TRACE', ''))
    ap.add_argument('--out', default=lexlib.LEX)
    ap.add_argument('--build', default=os.path.join(os.path.dirname(lexlib.LEX), 'build'))
    ap.add_argument('-j', type=int, default=min(16, os.cpu_count() or 1))
    a = ap.parse_args()
    t0 = time.time()
    os.makedirs(a.build, exist_ok=True)

    trace = {}
    if a.trace and os.path.isfile(a.trace):
        with open(a.trace, encoding='utf-8') as f:
            f.readline()
            for line in f:
                p = line.rstrip('\n').split('\t')
                if len(p) >= 4:
                    trace[(p[0], p[1], p[2])] = p[3]

    rows = lexlib.load_rows(a.dicts)
    senses_rows = load_senses(a.senses)
    reviewed = set()
    top = os.path.join(a.out, 'lexicon', 'top.tsv')
    if os.path.isfile(top):
        for r in read_tsv(top):
            reviewed.add((r['ja'], r['yomi'], r['pos']))

    # 1. analyze every key's dictionary form once
    strings = []
    for r in rows:
        r['kind'] = lexlib.row_kind(r)
        r['jd'] = lexlib.jdict(r)
        if r['kind'] not in ('ending', 'particle', 'symbol'):
            r['jds'] = lexlib.jdict_candidates(r)
            strings.extend(r['jds'])
            if r['kind'] in NOUN_KINDS:
                strings.append(CARRIER.format(r['jd']))
            for c in KIND_CARRIERS.get(r['kind'], ()):
                strings.extend(c.format(x) for x in r['jds'])
    for s in senses_rows:
        strings.append(s['key'])
        strings.append(CARRIER.format(s['key']))
        strings.extend(x for x in s['trig'].split(',') if x)
    for r in rows:
        if r['layer'] == 'names' or r['kind'] == 'proper':
            strings.append(r['key'])
            strings.append(CARRIER.format(r['key']))
    an = analyze_all(strings, a.j)
    t1 = time.time()
    # a verb or adjective key that is already a dictionary form keeps it (する, not するくる)
    for r in rows:
        if r['kind'] in ('verb', 'adj') and len(r.get('jds', [])) > 1:
            first = kind_toks(an, r['jds'][0], r['kind'])
            if not (len(first) == 1 and first[0][3] in lexlib.PRED):
                alt = kind_toks(an, r['jds'][1], r['kind'])
                if len(alt) == 1 and alt[0][3] in lexlib.PRED and alt[0][0] == alt[0][10]:
                    r['jd'] = r['jds'][1]

    unconv = []          # (file, line, key, reason)
    entries = collections.OrderedDict()   # lemma id tuple -> list of sense dicts
    phrases = []
    names_in = []
    stats = collections.Counter()

    def add_sense(lid, ko, kpos, kcls, cue, r, prov, note, order):
        lst = entries.setdefault(lid, [])
        lst.append({'ko': ko, 'kpos': kpos, 'kcls': kcls, 'cue': cue, 'prov': prov, 'note': note, 'order': order,
                    'file': r.get('file', ''), 'line': r.get('line', 0)})

    for r in rows:
        k = r['kind']
        prov = row_prov(r, trace)
        r['prov'] = prov
        if r['layer'] == 'names':
            names_in.append(r)
            continue
        if k in ('ending', 'particle'):
            unconv.append((r['file'], r['line'], r['key'], 'grammar:' + k))
            continue
        if k == 'symbol':
            unconv.append((r['file'], r['line'], r['key'], 'symbol'))
            continue
        if k == 'proper':
            names_in.append(r)
            continue
        if prov in SHIP_NO:
            unconv.append((r['file'], r['line'], r['key'], 'provenance'))
            continue
        ko, kpos, kcls, junk = korean(r, k)
        if junk:
            unconv.append((r['file'], r['line'], r['key'], junk))
            continue
        toks = noun_toks(an, r['jd']) if k in NOUN_KINDS else kind_toks(an, r['jd'], k)
        rule = r.get('ctx', '') or ''
        assigns = bool(re.search(r'[DY][+-]\d+\s*=\s*"', rule))
        cues, unmapped = rule_cues(rule) if rule else ([''], 0)
        has_rule = bool(r.get('rule')) or bool(rule)
        if has_rule and not cues:
            cues = ['']
        note = ''
        if has_rule:
            note = 'rule' + (':' + r['rule'] if r.get('rule') else '') + (' +unmapped' if unmapped else '')
        if assigns:
            note += ' assigns-neighbour'
        # rank: unconditional rows first; repo fixes, then supplements, then the port; prio; file order
        layer_rank = {'x': 0, 'wl': 1, 'w': 1, 'ji': 1, 'js': 1}.get(prov, 2)
        try:
            prio = -int(r.get('prio') or 0)
        except ValueError:
            prio = 0
        order = (1 if has_rule else 0, layer_rank, prio, r['file'] != 'base.tsv', r['line'])
        content = [t for t in toks if t[3] is not None]
        if len(toks) == 1 and toks[0][3] and toks[0][0] != toks[0][10] and toks[0][3] in lexlib.PRED:
            unconv.append((r['file'], r['line'], r['key'], 'surface-form'))
            continue
        if len(toks) == 1 and toks[0][3] == 'pn':
            t = toks[0]
            yomi = lexlib.hira(r.get('reading', '')) if r.get('reading') else t[8]
            for c in cues:
                add_sense((r['jd'], yomi, 'n'), ko, kpos, kcls, c, r, prov, (note + ' lu').strip(), order)
            stats['long-unit'] += 1
            continue
        if len(toks) == 1 and toks[0][3]:
            t = toks[0]
            cue_extra = ''
            lid = (t[1], t[2], t[3])
            for c in cues:
                add_sense(lid, ko, kpos, kcls, ' '.join(x for x in (c, cue_extra) if x), r, prov, note.strip(), order)
            stats['lemma'] += 1
            continue
        if len(toks) == 1:
            unconv.append((r['file'], r['line'], r['key'], 'grammar-token'))
            continue
        if not toks:
            unconv.append((r['file'], r['line'], r['key'], 'no-analysis'))
            continue
        if len(toks) == 2 and toks[0][3] and toks[1][3] is None and toks[1][4] == '助詞' and toks[1][0] in ('に', 'と', 'の', 'で') \
                and k in ('adv', 'adn', 'na', 'noun', 'other', 'phrase'):
            t = toks[0]
            lid = (t[1], t[2], t[3])
            for c in cues:
                add_sense(lid, ko, kpos, kcls, ' '.join(x for x in (c, 'next=' + toks[1][0]) if x), r, prov,
                          note.strip(), order)
            stats['lemma+particle'] += 1
            continue
        if all(t[3] in ('n', 'pn', 'num', 'suf', 'pref', 'na') for t in toks) and k in ('noun', 'pron', 'other', 'na', 'phrase'):
            yomi = lexlib.hira(r.get('reading', '')) if r.get('reading') else (
                ''.join(t[8] for t in toks) if all(t[8] for t in toks) else '')
            lid = (r['jd'], yomi, 'na' if k == 'na' and toks[-1][3] == 'na' else 'n')
            for c in cues:
                add_sense(lid, ko, kpos, kcls, c, r, prov, (note + ' lu').strip(), order)
            stats['long-unit'] += 1
            continue
        if len(content) == 1 and toks[0][3] and all(t[3] is None for t in toks[1:]) and k in ('verb', 'adj', 'na', 'noun', 'pron'):
            unconv.append((r['file'], r['line'], r['key'], 'surface-form'))
            continue
        phrases.append({'pattern': r['jd'], 'ko': ko, 'cue': cues[0] if cues else '', 'prov': prov,
                        'note': note.strip(), 'kind': k})
        stats['phrase'] += 1

    # 2. senses.tsv: trigger rows become cued senses; rows without triggers become phrases
    for s in senses_rows:
        toks = an.get(s['key'], [])
        if s['pos'] in ('', 'n'):
            nt = noun_toks(an, s['key'])
            if len(nt) == 1 and nt[0][3] == 'n' and (s['pos'] == 'n' or (toks and toks[0][3] in ('suf', 'v1', 'v5'))):
                toks = nt
        if not s['trig']:
            phrases.append({'pattern': s['key'], 'ko': s['ko'], 'cue': '', 'prov': 'x', 'note': 'senses', 'kind': 'phrase'})
            continue
        if len(toks) != 1 or not toks[0][3]:
            phrases.append({'pattern': s['key'], 'ko': s['ko'], 'cue': '', 'prov': 'x', 'note': 'senses multiword',
                            'kind': 'phrase'})
            continue
        t = toks[0]
        lem = []
        for w in s['trig'].split(','):
            ta = an.get(w, [])
            v = ta[0][1] if len(ta) == 1 and ta[0][3] else w
            if v not in lem:
                lem.append(v)
        cue = trigger_cue(t[3], s['where'], lem)
        text, kpos, kcls, ok = lexlib.expand_ko(s['ko'])
        kpos = kpos or ('V' if t[3] in ('v1', 'v5', 'vs', 'vk') else 'A' if t[3] in ('adj', 'na') and text.endswith('다') else None)
        add_sense((t[1], t[2], t[3]), text, kpos, kcls, cue, {'file': 'senses.tsv', 'line': s['line']}, 'x', 'senses',
                  (2, 0, 0, 0, s['line']))
        stats['senses-trigger'] += 1

    # 3. names: split into names, titles and common nouns
    an_names = an
    names_out = collections.defaultdict(list)
    titles = []
    split_stats = collections.Counter()
    common_rows = []
    for r in names_in:
        toks = noun_toks(an_names, r['key'])
        cls = split_name(r, toks)
        split_stats[(name_type(r), cls)] += 1
        r['split'] = cls
        p0 = r['prov'].rstrip('?')
        if p0 in SHIP_NO and not (cls == 'common' and p0 == 'np' and len(toks) == 1 and not toks[0][9]):
            unconv.append((r['file'], r['line'], r['key'], 'provenance'))
            split_stats[('dropped-provenance', cls)] += 1
            continue
        if not r['ko'].strip() or len(r['ko']) > 60 or '、' in r['key'] or '。' in r['key']:
            unconv.append((r['file'], r['line'], r['key'], 'name-filter'))
            continue
        yomi = lexlib.hira(r.get('reading', '')) if r.get('reading') else (
            ''.join(x[8] for x in toks) if all(x[8] for x in toks) else '')
        if cls == 'title':
            titles.append({'ja': r['key'], 'yomi': yomi, 'type': 'title', 'ko': r['ko'], 'conf': 'high', 'needs': '',
                           'prov': r['prov'], 'note': ''})
        elif cls == 'common':
            common_rows.append((r, toks, yomi))
        else:
            t = name_type(r)
            if t == 'term':
                t = 'other'
            conf = 'high' if t in ('place', 'org', 'person') else 'med'
            needs = '' if conf == 'high' else 'honorific|speaker|name-intro|ruby'
            names_out[t].append({'ja': r['key'], 'yomi': yomi, 'type': t, 'ko': r['ko'], 'conf': conf, 'needs': needs,
                                 'prov': r['prov'], 'note': ''})
    for r, toks, yomi in common_rows:
        if len(toks) == 1:
            lid = (toks[0][1], toks[0][2], toks[0][3])
            note = 'from-names'
        else:
            lid = (r['key'], yomi, 'n')
            note = 'from-names lu'
        if lid in reviewed:
            unconv.append((r['file'], r['line'], r['key'], 'reviewed-lemma'))
            split_stats[('common', 'reviewed-lemma')] += 1
            continue
        have = entries.get(lid, [])
        if have and not (sino_match(r['ko'], r['key']) or all('ァ' <= c <= 'ヺ' or c in 'ー・' for c in lid[0])):
            unconv.append((r['file'], r['line'], r['key'], 'name-variant'))
            split_stats[('common', 'name-variant-of-lemma')] += 1
            continue
        if any(s['ko'] == r['ko'].strip() for s in have):
            unconv.append((r['file'], r['line'], r['key'], 'duplicate-sense'))
            split_stats[('common', 'already-in-lexicon')] += 1
            continue
        split_stats[('common', 'new-sense' if have else 'new-entry')] += 1
        add_sense(lid, r['ko'].strip(), None, None, '', r, 'c+' + r['prov'], note, (3, 0, 0, 0, r['line']))
        stats['names-common'] += 1

    # 4. every lemma id must resolve through the analyzer; one that does not becomes a long-unit key, or is
    #    dropped when it is too short to be a safe key
    res = []
    for lid in entries:
        res += resolve_strings(*lid)
    an_res = analyze_all(res, a.j)
    dropped_rows = 0
    for lid in list(entries):
        senses = entries[lid]
        was_lu = any('lu' in s['note'].split() for s in senses)
        if resolves(an_res, *lid):
            for s in senses:
                s['note'] = ' '.join(w for w in s['note'].split() if w != 'lu')
            continue
        if not was_lu and len(lid[0]) <= 2 and all('぀' <= c <= 'ヿ' for c in lid[0]):
            del entries[lid]
            for s in senses:
                unconv.append((s['file'], s['line'], lid[0], 'unresolved-short'))
            dropped_rows += len(senses)
            continue
        for s in senses:
            if 'lu' not in s['note'].split():
                s['note'] = (s['note'] + ' lu').strip()
        if not was_lu:
            stats['unresolved-to-long-unit'] += 1

    # 5. write lexicon files
    lexdir = os.path.join(a.out, 'lexicon')
    bysa = os.path.join(a.out, 'by-sa')
    os.makedirs(lexdir, exist_ok=True)
    os.makedirs(bysa, exist_ok=True)
    os.makedirs(os.path.join(a.out, 'names'), exist_ok=True)
    files = collections.defaultdict(list)
    stems = {}
    multi = collections.Counter()
    for lid, senses in entries.items():
        if lid in reviewed:
            stats['skipped-reviewed-rows'] += len(senses)
            continue
        senses.sort(key=lambda s: (1 if s['cue'] else 0, s['order']))
        seen = set()
        n = 0
        out = []
        for s in senses:
            key = (s['ko'], s['cue'])
            if key in seen:
                stats['merged-duplicate'] += 1
                continue
            seen.add(key)
            # a sense whose Korean was already listed without a cue adds nothing
            if s['cue'] and (s['ko'], '') in seen and any(o['ko'] == s['ko'] and not o['cue'] for o in out):
                stats['merged-cue-of-listed'] += 1
                continue
            n += 1
            feat = ''
            if 'lu' in s['note'].split():
                feat = 'lu'
            out.append({**s, 'sense': n, 'feat': feat})
            if s['kpos'] or (lid[2] in lexlib.PRED and s['ko'].endswith('다')):
                kp = s['kpos'] or ('V' if lid[2] != 'adj' else 'A')
                lw = lexlib.last_word(s['ko'])
                if lw.endswith('다'):
                    cls, sure = (s['kcls'], True) if s['kcls'] else lexlib.guess_class(lw, kp)
                    stems.setdefault(lw.split(':')[0], collections.Counter())[(cls, kp, 'family' if s['kcls'] else 'rule', sure)] += 1
        multi['multi' if len({o['ko'] for o in out}) > 1 else 'single'] += 1
        bysa_entry = all(o['prov'].split('+')[-1] in ('ji', 'js', 'nj') for o in out)
        pf = 'long' if any('lu' in o['feat'] for o in out) else ('names' if all(o['prov'].startswith('c+') for o in out)
                                                                  else POS_FILE.get(lid[2], 'other'))
        dest = os.path.join(bysa, 'lexicon.tsv') if bysa_entry else os.path.join(lexdir, f'gen-{pf}.tsv')
        for o in out:
            note = o['note']
            files[dest].append([lid[0], lid[1], lid[2], str(o['sense']), o['ko'], o['cue'], '', o['feat'], 'u',
                                o['prov'], note.replace(' lu', '').replace('lu', '').strip()])
    for path, rws in files.items():
        write_tsv(path, 'lexicon', 'One row per sense; (ja, yomi, pos) is the lemma id and sense 1 is the default.',
                  LEXCOLS, rws)

    # phrases
    ph_rows, ph_bysa = [], []
    seen_ph = collections.Counter()
    for p in phrases:
        key = p['pattern']
        seen_ph[key] += 1
        row = [f'{key}#{seen_ph[key]}', key, p['ko'], p['cue'], 'u', p['prov'], p['note']]
        (ph_bysa if p['prov'] in ('ji', 'js') else ph_rows).append(row)
    write_tsv(os.path.join(a.out, 'phrases-gen.tsv'), 'phrases',
              'Multiword entries; the pattern is a dictionary-form key whose last predicate inflects.', PHCOLS, ph_rows)
    write_tsv(os.path.join(bysa, 'phrases.tsv'), 'phrases', 'Multiword entries from a share-alike source.', PHCOLS, ph_bysa)

    # names and titles
    for t, rws in names_out.items():
        plain = [[x[c] for c in NAMECOLS] for x in rws if x['prov'].rstrip('?') != 'nj']
        sa = [[x[c] for c in NAMECOLS] for x in rws if x['prov'].rstrip('?') == 'nj']
        write_tsv(os.path.join(a.out, 'names', f'{t}.tsv'), 'names', f'Names of type {t}.', NAMECOLS, plain)
        if sa:
            write_tsv(os.path.join(bysa, f'names-{t}.tsv'), 'names', f'Names of type {t}, share-alike source.', NAMECOLS, sa)
    write_tsv(os.path.join(a.out, 'titles.tsv'), 'titles', 'Titles of works; matched only inside quotes or as a whole line.',
              NAMECOLS, [[x[c] for c in NAMECOLS] for x in titles])

    # Korean predicate stems (seed for the generator)
    st_rows = []
    for lemma, c in sorted(stems.items()):
        (cls, kp, how, sure), _ = c.most_common(1)[0]
        classes = {k[0] for k in c}
        flags = []
        if len(classes) > 1:
            g, gsure = lexlib.guess_class(lemma, kp)
            if gsure and g in classes:
                cls, how = g, 'rule'
                flags.append('resolved:' + '|'.join(sorted(x or '?' for x in classes)))
            else:
                flags.append('conflict:' + '|'.join(sorted(x or '?' for x in classes)))
        if not sure:
            flags.append('homograph')
        st_rows.append([lemma, cls or '?', kp, how, ' '.join(flags)])
    write_tsv(os.path.join(a.out, 'stems.tsv'), 'stems', 'Korean predicate lemmas: class and V/A (seed).',
              ['lemma', 'class', 'kpos', 'from', 'flags'], st_rows)

    # long-unit keys for the analyzer
    lu = []
    for path, rws in files.items():
        for x in rws:
            if x[3] == '1' and ('lu' in x[7] or x[0] != x[0].strip()):
                lu.append([x[0], x[1], x[2], 'lexicon'])
    for p in ph_rows + ph_bysa:
        if p[0].endswith('#1'):
            lu.append([p[1], '', 'phrase', 'phrase'])
    for t, rws in names_out.items():
        for x in rws:
            lu.append([x['ja'], x['yomi'], 'pn', 'name:' + t])
    write_tsv(os.path.join(a.out, 'longunits.tsv'), 'longunits', 'Keys the analyzer adds as long-unit arcs.',
              ['key', 'yomi', 'pos', 'source'], lu)

    with open(os.path.join(a.build, 'unconverted.tsv'), 'w', encoding='utf-8') as o:
        o.write('file\tline\tkey\treason\n')
        for u in unconv:
            o.write('\t'.join(map(str, u)) + '\n')
    rep = collections.Counter(u[3] for u in unconv)
    total_rows = len(rows) + len(senses_rows)
    accounted = stats['lemma'] + stats['lemma+particle'] + stats['long-unit'] + stats['phrase'] + len(unconv) \
        + stats['senses-trigger'] + sum(1 for p in phrases if p['note'].startswith('senses')) \
        + len(titles) + sum(len(v) for v in names_out.values()) + stats['names-common'] - dropped_rows
    report = [['rows', str(total_rows)], ['accounted', str(accounted)]]
    report += [['converted:' + k, str(v)] for k, v in sorted(stats.items())]
    report += [['unconverted:' + k, str(v)] for k, v in sorted(rep.items())]
    report += [['entries:' + k, str(v)] for k, v in sorted(multi.items())]
    report += [['names:' + t, str(len(v))] for t, v in sorted(names_out.items())] + [['titles', str(len(titles))]]
    report += [[f'split:{t}:{c}', str(v)] for (t, c), v in sorted(split_stats.items())]
    report += [['seconds:analysis', f'{t1 - t0:.1f}'], ['seconds:total', f'{time.time() - t0:.1f}']]
    write_tsv(os.path.join(a.out, 'report.tsv'), 'report', 'Conversion counts by class.', ['what', 'count'],
              report[:-2])
    for x in report:
        print('\t'.join(x))


def read_tsv(path):
    with open(path, encoding='utf-8') as f:
        hdr = None
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            p = line.rstrip('\n').split('\t')
            if hdr is None:
                hdr = p
                continue
            yield dict(zip(hdr, p + [''] * (len(hdr) - len(p))))


def write_tsv(path, kind, desc, cols, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as o:
        o.write(f'#! {kind}\n# {desc}\n' + '\t'.join(cols) + '\n')
        for r in rows:
            o.write('\t'.join(str(x).replace('\t', ' ') for x in r) + '\n')


if __name__ == '__main__':
    main()
