# Converts 0.7.x --why output into pre-IR lines for hd8retro (the retrofit experiment). Run from the repository
# root, or set HD8_FAMILIES to src/core/output/ko_families_gen.cpp.
# usage: python3 convert.py JA.txt WHY.txt OUT075.txt META.jsonl(or -) [STAGES/input.C.got.tsv] > pre.jsonl
# Each line: {"line", "ja", "out075", "speaker", "scene", "dialogue", "items": [...]}
# items: {"k":"N","t":..} {"k":"P","t":notation} {"k":"Punct","p":kind,"m":mark} {"k":"Raw","t":..}
#        {"k":"V","lemma","pos","cls","row","rest","marker"}
import json, re, sys, collections

import os
SRC = os.environ.get('HD8_FAMILIES', 'src/core/output/ko_families_gen.cpp')  # the 0.7.x marker family table
FAM = {}
for k, st, pos, cl, fl in re.findall(r'\{"([&$%]..@)", "([^"]*)", Pos::(\w+), Class::(\w+), ([^,]+), 0x', open(SRC, encoding='utf-8').read()):
    FAM[k] = (st, pos, cl, fl)
CLS = {'Regular': '', 'Yeo': 'HA', 'Digeut': 'D', 'Bieup': 'B', 'Siot': 'S', 'Hieuh': 'H', 'Reu': 'REU', 'Reo': 'REO',
       'U': 'U', 'Eu': 'EU', 'Rieul': 'L', 'Existential': 'EXIST', 'Copula': 'COP'}
PUNCT = {'、': ('comma', '、'), '，': ('comma', '，'), '。': ('end', '。'), '．': ('end', '．'), '！': ('excl', '！'), '!': ('excl', '!'),
         '？': ('q', '？'), '?': ('q', '?'), '「': ('qopen', '「'), '」': ('qclose', '」'), '『': ('qopen', '『'), '』': ('qclose', '』'),
         '（': ('qopen', '（'), '）': ('qclose', '）'), '(': ('qopen', '('), ')': ('qclose', ')'), '…': ('ellipsis', '…'),
         '‥': ('ellipsis', '‥'), '―': ('dash', '―'), '─': ('dash', '─'), '～': ('mark', '～'), '〜': ('mark', '〜'), '♪': ('mark', '♪'),
         '・': ('pause', '・'), '【': ('qopen', '【'), '】': ('qclose', '】'), '“': ('qopen', '“'), '”': ('qclose', '”')}
KO_PUNCT = {',': ('comma', ''), '.': ('end', ''), '?': ('q', ''), '!': ('excl', ''), '…': ('ellipsis', ''), '「': ('qopen', '「'),
            '」': ('qclose', '」'), '『': ('qopen', '『'), '』': ('qclose', '』'), '~': ('mark', '～')}
PART = re.compile(r'^\((이|을|은|와|으|이어|아)\)([가-힣]+)')

def items_of_target(target, ending, jsurf):
    """0.7.5 target + ending → items."""
    out = []
    t = target
    if t == '' or t == ' ':
        return out
    if t in KO_PUNCT and not ending:
        k, m = KO_PUNCT[t]
        mark = jsurf if jsurf in PUNCT else m
        out.append({'k': 'Punct', 'p': k, 'm': mark})
        return out
    m = re.search(r'([&$%]..@)$', t)
    if m and m.group(1) in FAM:
        st, pos, cl, fl = FAM[m.group(1)]
        prefix = t[:m.start()]
        words = prefix.split(' ')
        head = words[-1]
        for w in words[:-1]:
            out += plain_words(w)
        lead = ''
        pm = PART.match(head)
        if pm:  # particle then a stem: (이)가 아$A3@
            out.append({'k': 'P', 't': '(' + pm.group(1) + ')' + pm.group(2)})
            head = head[pm.end():]
        if '(' in st or not all('가' <= c <= '힣' for c in st + head):
            out.append({'k': 'Raw', 't': t})
            return out
        p = {'Verb': 'V', 'Adjective': 'A'}[pos]
        if cl == 'Existential': p = 'X'
        c = CLS[cl]
        if c == 'B' and 'kBieupWa' in fl: c = 'BWA'
        lemma = head + st + '다'
        if c == 'COP':
            # noun + copula: the noun is its own node, the copula attaches
            if head: out.append({'k': 'N', 't': head})
            lemma = '이다'; p = 'C'
        out.append({'k': 'V', 'lemma': lemma, 'pos': p, 'cls': c, 'row': ending[:1] if ending else '1', 'rest': ending[1:] if ending else '',
                    'marker': m.group(1)[0], 'bare': 'kBareDeclarative' in fl})
        return out
    # copula notations: (이)다, (이)야, (이었)였지만 — not the particles (이)가, (이)나 …
    cm = re.fullmatch(r'\(이([가-힣]*)\)([가-힣]+)', t)
    if cm and t not in COPULA_NOT and not ending:
        inside, vowel = '이' + cm.group(1), cm.group(2)
        cons = inside + vowel if len(inside) == 1 else inside + vowel[1:]
        out.append({'k': 'V', 'lemma': '이다', 'pos': 'C', 'cls': 'COP', 'row': '1', 'rest': cons[1:], 'marker': '&', 'bare': False})
        return out
    # particle notation (이)가, possibly followed by words
    pm = PART.match(t)
    if pm:
        out.append({'k': 'P', 't': '(' + pm.group(1) + ')' + pm.group(2)})
        rest = t[pm.end():].strip()
        if rest: out += plain_words(rest)
        return out
    if re.fullmatch(r'\(이\)다', t):
        out.append({'k': 'V', 'lemma': '이다', 'pos': 'C', 'cls': 'COP', 'row': '1', 'rest': '다', 'marker': '&', 'bare': False})
        return out
    if ending:
        out.append({'k': 'Raw', 't': t + ending})
        return out
    if jsurf in JA_PARTICLES and re.fullmatch(r'[가-힣]{1,4}', t):
        out.append({'k': 'P', 't': t})
        return out
    out += plain_words(t)
    return out

COPULA_NOT = {'(이)가', '(이)나', '(이)랑', '(이)라도', '(이)라고', '(이)야말로', '(이)든지', '(이)든', '(이)여', '(이)며', '(이)란', '(이)라든가', '(이)라든지', '(이)나마', '(이)고'}
JA_PARTICLES = {'の', 'に', 'で', 'と', 'も', 'へ', 'から', 'まで', 'より', 'や', 'は', 'が', 'を', 'には', 'では', 'とは', 'にも', 'でも', 'へと', 'だけ', 'しか', 'ばかり', 'など', 'こそ', 'さえ', 'すら', 'とか', 'って', 'ほど', 'くらい', 'ぐらい', 'ずつ', 'について', 'によって', 'として', 'にとって'}
PARTICLES = {'의', '에', '에서', '도', '만', '까지', '부터', '처럼', '보다', '마다', '조차', '밖에', '에게', '에게서', '한테', '께', '로부터', '이나', '나', '랑', '이랑', '하고', '께서', '라도', '이라도', '야', '이야', '요', '은', '는', '이', '가', '을', '를', '과', '와', '로', '으로', '든지', '이든지', '뿐'}
def plain_words(t):
    out = []
    t = re.sub(r'\([가-힣 ]+\)(?=[가-힣])|(?<=[가-힣])\([가-힣 ]+\)', '', t)
    for w in t.split(' '):
        if not w: continue
        if PART.fullmatch(w) if hasattr(PART, 'fullmatch') else False:
            out.append({'k': 'P', 't': w}); continue
        if w in KO_PUNCT:
            k, m = KO_PUNCT[w]; out.append({'k': 'Punct', 'p': k, 'm': m}); continue
        if re.fullmatch(r'[가-힣]+', w):
            out.append({'k': 'N', 't': w})
        else:
            out.append({'k': 'Raw', 't': w})
    return out

def parse_why(path):
    lines = collections.defaultdict(list)
    for l in open(path, encoding='utf-8'):
        if not l.startswith('WHY\t'): continue
        f = l.rstrip('\n').split('\t')
        if len(f) < 18: f += [''] * (18 - len(f))
        seg = f[4].split(':')[0]
        lines[int(f[1])].append({'sent': int(f[3]), 'seg': int(seg) if seg.isdigit() else 0, 'surf': f[5], 'target': f[9],
                                 'ending': f[17], 'chunk': int(f[2])})
    return lines

DEPS = {'것', '거', '수', '줄', '데', '때', '뿐', '만큼', '대로', '듯', '척', '체', '채', '적', '바', '리', '지', '터', '김', '중', '뻔', '동안'}
SYM = set('≪≫〈〉《》〔〕［］｛｝{}<>＜＞')
def convert_line(ja, rows):
    # pass 1: source positions of the items whose surface is in the line
    cursor = 0; pos = []
    for r in rows:
        p = ja.find(r['surf'], cursor) if r['surf'] else -1
        if p >= 0 and p - cursor > 60: p = -1
        pos.append(p)
        if p >= 0: cursor = p + len(r['surf'])
    covered = [(p, p + len(r['surf'])) for p, r in zip(pos, rows) if p >= 0]
    def cov(i): return any(a <= i < b for a, b in covered)
    runs = []  # uncovered punctuation, symbols and stutters in source order: (pos, items)
    i = 0
    while i < len(ja):
        c = ja[i]
        if cov(i): i += 1; continue
        if c in PUNCT:
            k, m = PUNCT[c]; runs.append((i, [{'k': 'Punct', 'p': k, 'm': m, 'gap': 1}])); i += 1; continue
        if c in SYM:
            runs.append((i, [{'k': 'Raw', 't': c, 'gap': 1}])); i += 1; continue
        # stutter: one kana before 、 that the next word repeats (ど、どういう)
        if re.match(r'[ぁ-んァ-ヶ]', c) and i + 1 < len(ja) and ja[i + 1] == '、' and i + 2 < len(ja) and ja[i + 2] == c:
            runs.append((i, [{'k': 'Raw', 't': '\u0001', 'stutter': 1}])); i += 1; continue
        i += 1
    out = []; ri = 0; prev = None
    for n, r in enumerate(rows):
        p = pos[n]
        if p >= 0:
            while ri < len(runs) and runs[ri][0] < p:
                out += runs[ri][1]; ri += 1
        elif prev is not None and (r['seg'] > prev['seg'] + 1 or r['sent'] != prev['sent']):
            nxt = next((pos[j] for j in range(n + 1, len(rows)) if pos[j] >= 0), None)
            if ri < len(runs) and (nxt is None or runs[ri][0] < nxt):
                out += runs[ri][1]; ri += 1
        its = items_of_target(r['target'], r['ending'], r['surf'])
        if p < 0:
            # a mark the chain wrote itself (? for ？): it stands for the next uncovered mark of its kind
            for it in its:
                if it['k'] != 'Punct': continue
                for j in range(ri, len(runs)):
                    rr = runs[j][1]
                    if len(rr) == 1 and rr[0].get('k') == 'Punct' and rr[0]['p'] == it['p']:
                        it['m'] = rr[0]['m']; del runs[j]; break
        if r['surf'] in PUNCT:
            for it in its:
                if it['k'] == 'Punct': it['m'] = r['surf']
        out += its
        prev = r
    while ri < len(runs):
        out += runs[ri][1]; ri += 1
    return out

def read_c(path):
    c = collections.defaultdict(str)
    line = 0
    for l in open(path, encoding='utf-8'):
        if l.startswith('#LINE'):
            line = int(l.split('\t')[1]); continue
        f = l.rstrip('\n').split('\t')
        if f[0] == 'C' and len(f) >= 4: c[line] += f[3]
    return c

def glue(items, ctext):
    cur = 0; prev_end = -1; prev_kind = None
    for it in items:
        k = it['k']
        if k in ('N', 'Raw', 'P') and it.get('t'):
            t = it['t']
            if k == 'P':
                t = re.sub(r'^\([^)]*\)', '', t)
            pos = ctext.find(t, cur) if t else -1
            if pos >= 0 and pos - cur < 40:
                if k in ('N', 'Raw') and prev_kind in ('N', 'Raw') and pos == prev_end and t not in DEPS:
                    it['glue'] = 1
                prev_end = pos + len(t); cur = prev_end; prev_kind = k
                continue
        if k == 'V' and prev_kind in ('N', 'Raw') and prev_end >= 0 and prev_end < len(ctext) and ctext[prev_end] != ' ' \
                and it.get('lemma') in ('하다', '되다', '시키다', '당하다', '드리다', '받다', '스럽다', '답다', '롭다', '이다'):
            it['glue'] = 1
        prev_kind = k if k != 'Punct' else None
        prev_end = -1

def main():
    ja = open(sys.argv[1], encoding='utf-8').read().split('\n')
    why = parse_why(sys.argv[2])
    out = open(sys.argv[3], encoding='utf-8').read().split('\n')
    meta = [json.loads(l) for l in open(sys.argv[4], encoding='utf-8')] if len(sys.argv) > 4 and sys.argv[4] != '-' else None
    ctexts = read_c(sys.argv[5]) if len(sys.argv) > 5 else None
    n = len(ja) - (1 if ja and ja[-1] == '' else 0)
    for i in range(n):
        rec = {'line': i + 1, 'ja': ja[i], 'out075': out[i] if i < len(out) else '', 'items': convert_line(ja[i], why.get(i + 1, []))}
        if ctexts is not None: glue(rec['items'], ctexts.get(i + 1, ''))
        rec['imp'] = bool(re.search(r'(て|で|ろ|れ|なさい|ください|下さい|くれ|てよ|ないで|ちょうだい)[！!。…～〜ー\s]*[」』）]?\s*$', ja[i].strip()))
        rec['lead'] = re.match(r'^[ \u3000]*', rec['out075']).group(0)
        if meta:
            m = meta[i]
            rec['speaker'] = m.get('speaker', ''); rec['scene'] = m.get('scene', str(i)); rec['dialogue'] = m.get('dialogue', False) or ja[i].lstrip().startswith('≪')
            rec['work'] = m.get('work', '')
        else:
            rec['speaker'] = ''; rec['scene'] = str(i); rec['dialogue'] = ('「' in ja[i] or '『' in ja[i][:1] or ja[i].lstrip().startswith('≪')); rec['work'] = ''
        print(json.dumps(rec, ensure_ascii=False))

if __name__ == '__main__':
    main()
