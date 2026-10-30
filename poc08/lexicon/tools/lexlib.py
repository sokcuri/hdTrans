"""Shared code for the lexicon tools: dictionary rows, lemma ids, Korean lemmas and classes."""
import glob
import os
import re
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
LEX = os.path.abspath(os.path.join(HERE, '..', 'lex'))

# ---------------------------------------------------------------- analyzer (UniDic 3.1 through fugashi)

_TAGGER = None


def tagger():
    global _TAGGER
    if _TAGGER is None:
        import fugashi
        d = os.environ.get('HD8_UNIDIC')
        if not d:
            import unidic
            d = unidic.DICDIR
        _TAGGER = fugashi.Tagger('-d ' + d + ' -r ' + os.path.join(d, 'mecabrc'))
    return _TAGGER


KATA = {chr(c): chr(c - 0x60) for c in range(0x30A1, 0x30F7)}


def hira(s):
    return ''.join(KATA.get(c, c) for c in s)


def lemma_form(lemma, surface):
    """UniDic lemma without the gloss suffix of loanwords (ナイフ-knife → ナイフ)."""
    if not lemma or lemma == '*':
        return surface
    m = re.match(r'^(.+?)-.+$', lemma)
    return m.group(1) if m else lemma


def map_pos(pos1, pos2, ctype):
    """UniDic POS → the closed lexicon pos set (IR.md Pos). None = grammar, not lexicon."""
    if pos1 == '名詞':
        if pos2 == '固有名詞':
            return 'pn'
        if pos2 == '数詞':
            return 'num'
        if pos2 == '助動詞語幹':
            return None
        return 'n'
    if pos1 == '代名詞':
        return 'n'
    if pos1 == '形状詞':
        return None if pos2 == '助動詞語幹' else 'na'
    if pos1 == '形容詞':
        return 'adj'
    if pos1 == '動詞':
        c = ctype or ''
        if c.startswith('五段') or c.startswith('文語四段') or c.startswith('文語ラ行変格') or c.startswith('文語ナ行変格'):
            return 'v5'
        if c.startswith('サ行変格') or c.startswith('文語サ行変格'):
            return 'vs'
        if c.startswith('カ行変格') or c.startswith('文語カ行変格'):
            return 'vk'
        return 'v1'
    if pos1 == '副詞':
        return 'adv'
    if pos1 == '連体詞':
        return 'adn'
    if pos1 == '接続詞':
        return 'conj'
    if pos1 == '感動詞':
        return 'intj'
    if pos1 == '接尾辞':
        return 'suf'
    if pos1 == '接頭辞':
        return 'pref'
    return None


CONTENT = {'n', 'v1', 'v5', 'vs', 'vk', 'adj', 'na', 'adv', 'adn', 'conj', 'intj', 'suf', 'pref'}
PRED = {'v1', 'v5', 'vs', 'vk', 'adj'}


class Tok:
    __slots__ = ('surf', 'lemma', 'yomi', 'pos', 'pos1', 'pos2', 'ctype', 'cform', 'orth', 'kana', 'unk')

    def __init__(self, w):
        f = w.feature
        self.surf = w.surface
        self.pos1 = f.pos1 or ''
        self.pos2 = f.pos2 or ''
        self.ctype = f.cType if f.cType not in (None, '*') else ''
        self.cform = f.cForm if f.cForm not in (None, '*') else ''
        self.lemma = lemma_form(f.lemma, w.surface)
        lf = f.lForm if f.lForm not in (None, '*') else (f.kana or '')
        self.yomi = hira(lf or '')
        self.pos = map_pos(self.pos1, self.pos2, self.ctype)
        self.orth = f.orthBase if f.orthBase not in (None, '*') else w.surface
        self.kana = hira(f.kana) if f.kana not in (None, '*') else ''
        self.unk = bool(w.is_unk)

    @property
    def lid(self):
        return lemma_id(self.lemma, self.yomi, self.pos or '-')


def lemma_id(ja, yomi, pos):
    return f'{ja}/{yomi}/{pos}'


def analyze(text):
    return [Tok(w) for w in tagger()(text) if w.feature.pos1 not in ('空白',)]


# ---------------------------------------------------------------- dictionary rows

def load_rows(dicts, skip=('kana.tsv',)):
    """Every row of dicts/*.tsv as a dict with file, line, layer and userfmt keys."""
    rows = []
    for f in sorted(glob.glob(os.path.join(dicts, '*.tsv'))):
        name = os.path.basename(f)
        if name in skip:
            continue
        layer = 'base'
        hdr = None
        with open(f, encoding='utf-8') as fh:
            for i, line in enumerate(fh):
                line = line.rstrip('\n')
                if i == 0 and line.startswith('#!'):
                    layer = line.split('=', 1)[1].strip()
                    continue
                if not line or line.startswith('#') or line.startswith('//'):
                    continue
                parts = line.split('\t')
                if hdr is None and parts[0] == 'key':
                    hdr = parts
                    continue
                if hdr is None:
                    r = {'key': parts[0], 'ko': parts[1] if len(parts) > 1 else '',
                         'pos': parts[2] if len(parts) > 2 else 'n',
                         'opt': '\t'.join(parts[3:]) if len(parts) > 3 else '', 'userfmt': True}
                else:
                    r = dict(zip(hdr, parts + [''] * (len(hdr) - len(parts))))
                    r['userfmt'] = False
                r['file'] = name
                r['layer'] = layer
                r['line'] = i + 1
                rows.append(r)
    return rows


ENGINE_POS = re.compile(r'^[A-Z][0-9][0-9A-Z][0-9A-Za-z:;<=>?]$')
# 4th character of a verb stem code → the dictionary-form ending of the key
VERB_END = {'0': 'う', '1': 'く', '2': 'ぐ', '3': 'す', '4': 'つ', '5': 'む', '6': 'ぶ', '7': 'る', '8': 'る',
            '9': 'する', ':': 'ずる', ';': 'くる', '<': 'ぬ', '=': '来る', '?': 'く'}


def row_kind(r):
    """Coarse class of a row from its POS code: noun, proper, pron, verb, adj, na, adv, adn, particle,
    ending, symbol, phrase, intj, or a user-format POS name."""
    p = r['pos']
    if not r['userfmt'] and ENGINE_POS.match(p):
        d = p[1]
        if d == '1':
            return {'3': 'proper', '2': 'pron'}.get(p[2], 'noun')
        return {'0': 'adn', '2': 'particle', '3': 'adv', '4': 'verb', '5': 'ending', '6': 'symbol',
                '7': 'adj', '8': 'na', '9': 'phrase'}.get(d, 'other')
    base = p.split(':')[0].strip()
    return {'n': 'noun', 'pron': 'pron', 'name': 'proper', 'surname': 'proper', 'place': 'proper',
            'v': 'verb', 'v1': 'verb', 'v5': 'verb', 'vs': 'verb', 'adj': 'adj', 'na': 'na', 'adv': 'adv',
            'ono': 'adv', 'adn': 'adn', 'intj': 'intj', 'phrase': 'phrase', 'end': 'ending', 'pat': 'ending',
            'conj': 'adv', 'suf': 'noun', 'pre': 'noun', 'hon': 'noun', 'counter': 'noun'}.get(base, 'other')


def jdict_candidates(r):
    """Possible dictionary forms of the row's key, most likely first."""
    k, p = r['key'], r['pos']
    if r['userfmt'] or not ENGINE_POS.match(p):
        return [k]
    if p[1] == '4':
        e = VERB_END.get(p[3])
        return [k + e, k] if e else [k]
    if p[1] == '7':
        return [k + 'い', k]
    return [k]


def jdict(r):
    """Japanese dictionary form implied by the row: a verb stem gets its ending, an i-adjective stem い."""
    k, p = r['key'], r['pos']
    if r['userfmt'] or not ENGINE_POS.match(p):
        return k
    if p[1] == '4':
        e = VERB_END.get(p[3])
        return k + e if e else k
    if p[1] == '7':
        return k + 'い'
    return k


# ---------------------------------------------------------------- Korean lemmas and classes

FAM_CLASS = {'Regular': 'REG', 'Yeo': 'HA', 'Digeut': 'D', 'Bieup': 'B', 'Siot': 'S', 'Hieuh': 'H',
             'Reu': 'REU', 'Reo': 'REO', 'Eu': 'EU', 'Rieul': 'L', 'Copula': 'COP', 'Existential': 'EXIST'}
_FAM = None
MARK = re.compile(r'[&$%][0-9A-Za-z]{2}@')


def families():
    """Marker → (stem, 'V'|'A', class) from the 0.7 family table."""
    global _FAM
    if _FAM is None:
        _FAM = {}
        src = open(os.path.join(REPO, 'src/core/output/ko_families_gen.cpp'), encoding='utf-8').read()
        for m in re.finditer(r'\{"([&$%].{2}@)", "([^"]*)", Pos::(\w+), Class::(\w+), ([^,]+),', src):
            cls = FAM_CLASS[m.group(4)]
            if cls == 'B' and 'kBieupWa' in m.group(5):
                cls = 'BWA'
            _FAM[m.group(1)] = (m.group(2), 'V' if m.group(3) == 'Verb' else 'A', cls)
    return _FAM


def expand_ko(ko):
    """Korean marker string → (lemma text, kpos, kcls, ok). 개&AJ@ → 개이다."""
    m = MARK.search(ko)
    if not m:
        return ko, None, None, True
    fam = families().get(m.group(0))
    if not fam:
        return ko, None, None, False
    rest = ko[m.end():]
    if MARK.search(rest):
        return ko, None, None, False
    return ko[:m.start()] + fam[0] + '다' + rest, fam[1], fam[2], True


def jamo(c):
    o = ord(c) - 0xAC00
    if 0 <= o < 11172:
        return o // 588, (o % 588) // 28, o % 28
    return None


F_B, F_D, F_S, F_H, F_L = 17, 7, 19, 27, 8
D_IRREG = {'듣', '걷', '묻', '싣', '깨닫', '붇', '일컫', '긷', '겯', '눋', '닫닫'}
S_IRREG = {'낫', '짓', '붓', '잇', '긋', '젓', '잣', '긋'}
B_VERB = {'돕', '굽', '눕', '줍', '깁', '곱', '여쭙', '여쭈', '가깝'}
BWA = {'돕', '곱'}
H_REG = {'좋', '놓', '넣', '낳', '닿', '쌓', '찧', '않', '괜찮', '많', '싫', '끓', '잃', '앓', '귀찮', '점잖', '하찮', '언짢', '편찮', '같잖', '시답잖'}
EU_RU = {'따르', '치르', '들르', '다다르', '우러르', '잇따르', '뒤따르', '뒤치르'}
REO_RU = {'누르', '푸르', '이르'}
B_REG_ADJ = {'좁', '넓', '수줍'}
# stems whose class depends on the meaning; a lemma must carry it (묻다:ㄷ)
HOMOGRAPH = {'묻': ('D', 'REG'), '걷': ('D', 'REG'), '이르': ('REO', 'REU'), '굽': ('B', 'REG'), '붇': ('D', 'REG')}
CLASS_TAG = {'ㄷ': 'D', 'ㅂ': 'B', 'ㅂ와': 'BWA', 'ㅅ': 'S', 'ㅎ': 'H', '르': 'REU', '러': 'REO', '우': 'U', '으': 'EU',
             'ㄹ': 'L', '규칙': 'REG', '하다': 'HA', '이다': 'COP'}


def last_word(lemma):
    return lemma.split(' ')[-1]


def guess_class(lemma, kpos):
    """Class of a Korean predicate lemma (…다) by spelling, for the ko stems seed. Returns (class, sure)."""
    w = last_word(lemma)
    if ':' in w:
        w, tag = w.split(':', 1)
        if tag in CLASS_TAG:
            return CLASS_TAG[tag], True
    if not w.endswith('다') or len(w) < 2:
        return None, False
    stem = w[:-1]
    last = stem[-1]
    j = jamo(last)
    if j is None:
        return None, False
    if stem.endswith('하'):
        return 'HA', True
    if stem == '이' or (stem.endswith('이') and kpos == 'COP'):
        return 'COP', True
    if stem.endswith('있') or stem.endswith('없'):
        return 'EXIST', True
    if stem == '푸':
        return 'U', True
    for h in HOMOGRAPH:
        if stem == h or (stem.endswith(h) and len(stem) > len(h) and h in ('묻', '걷')):
            # untagged homographs take the plain class; the irregular one is written with its tag (묻다:ㄷ)
            return HOMOGRAPH[h][1], True
    _, v, f = j
    if f == F_L:
        return 'L', True
    if f == F_D:
        return ('D' if any(stem.endswith(x) for x in D_IRREG) else 'REG'), True
    if f == F_S:
        return ('S' if any(stem.endswith(x) for x in S_IRREG) else 'REG'), True
    if f == F_H:
        if any(stem.endswith(x) for x in H_REG) or kpos == 'V':
            return 'REG', True
        return 'H', True
    if f == F_B:
        if kpos == 'A' and not any(stem.endswith(x) for x in B_REG_ADJ):
            return ('BWA' if any(stem.endswith(x) for x in BWA) else 'B'), True
        if any(stem.endswith(x) for x in B_VERB):
            return ('BWA' if any(stem.endswith(x) for x in BWA) else 'B'), True
        if stem.endswith('럽') or stem.endswith('롭') or stem.endswith('답') or stem.endswith('스럽'):
            return 'B', True
        return 'REG', kpos is not None
    if f == 0 and v == 18:  # ㅡ
        if last == '르':
            if any(stem.endswith(x) for x in REO_RU):
                return 'REO', stem not in ('이르',)
            if any(stem.endswith(x) for x in EU_RU):
                return 'EU', True
            return 'REU', True
        return 'EU', True
    return 'REG', True


HANGUL = re.compile(r'^[가-힣]')
HANGUL_ONLY = re.compile(r'^[가-힣 ]+$')


def nfkc(s):
    return unicodedata.normalize('NFKC', s)
