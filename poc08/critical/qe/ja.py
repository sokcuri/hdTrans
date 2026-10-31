"""Japanese side of the QE gate: what a translation must realise, read from the 0.8 IR (W1 tokens, W2 clauses).

Input: hdtrans08-ir --each --stages DIR (DIR/ir.jsonl: a doc record, then one line record per input line).
Output per line: sentences (byte spans) and, per clause, the expectations the checks read:
pol (neg|pos), negFn, force, tense, voice, bene, polite, modality, link, linkSub, its sentence, and the
tokens that carry numbers and person names. Clause heads are not read (the clause link says whether a clause ends
its sentence).
"""
import json
import re

ENDS = set('。！？!?')


def load_ir(path):
    """ir.jsonl → list of line records (None for a line without a record)."""
    out, cur = [], None
    for l in open(path, encoding='utf-8'):
        d = json.loads(l)
        if 'doc' in d:
            if cur is not None:
                out.append(cur)
            cur = {}
            continue
        if 'src' in d:
            cur = d
    if cur is not None:
        out.append(cur)
    return [d if d else None for d in out]


def sentences(src):
    """Byte spans of the sentences of a line: cut after 。！？!? (and their runs, closing quotes) at depth 0."""
    b = src.encode()
    spans, start, depth, i = [], 0, 0, 0
    chars = list(src)
    pos = 0
    offs = []
    for ch in chars:
        offs.append(pos)
        pos += len(ch.encode())
    offs.append(pos)
    n = len(chars)
    while i < n:
        ch = chars[i]
        if ch in '「『（(“【':
            depth += 1
        elif ch in '」』）)”】':
            depth = max(0, depth - 1)
        if ch in ENDS and depth == 0:
            j = i + 1
            while j < n and (chars[j] in ENDS or chars[j] in '」』）)”…‥ 　'):
                j += 1
            spans.append((offs[start], offs[j]))
            start, i = j, j
            continue
        i += 1
    if offs[start] < len(b) and src[len(b[:offs[start]].decode()):].strip():
        spans.append((offs[start], len(b)))
    return spans or [(0, len(b))]


def clause_tokens(rec, c):
    buns = rec.get('buns', [])
    fb, lb = c.get('firstBun', 0), c.get('lastBun', 0)
    if not buns or lb >= len(buns):
        return []
    first = buns[fb].get('first', 0)
    last = buns[lb].get('last', 0)
    return list(range(first, last + 1))


NUM_KANJI = {'〇': 0, '零': 0, '一': 1, '二': 2, '三': 3, '四': 4, '五': 5, '六': 6, '七': 7, '八': 8, '九': 9}
UNITS = {'十': 10, '百': 100, '千': 1000}
BIG = {'万': 10 ** 4, '億': 10 ** 8}


def kanji_number(s):
    """Value of a kanji numeral (二十一, 五百, 二一 read digit by digit); None if not one."""
    if not s or any(ch not in NUM_KANJI and ch not in UNITS and ch not in BIG for ch in s):
        return None
    if not any(ch in UNITS or ch in BIG for ch in s):
        return int(''.join(str(NUM_KANJI[ch]) for ch in s))
    total, sect, cur = 0, 0, 0
    for ch in s:
        if ch in NUM_KANJI:
            cur = NUM_KANJI[ch]
        elif ch in UNITS:
            sect += (cur or 1) * UNITS[ch]
            cur = 0
        else:
            total += (sect + cur or 1) * BIG[ch]
            sect = cur = 0
    return total + sect + cur


def numbers(rec):
    """Integer values written in the line (digits, kanji numerals), with their byte offsets."""
    out = []
    src = rec['src']
    import unicodedata
    norm = unicodedata.normalize('NFKC', src)
    for m in re.finditer(r'\d+(?:,\d{3})*', norm):
        out.append(int(m.group(0).replace(',', '')))
    run = ''
    for t in rec.get('toks', []) + [{}]:
        if t.get('pos') == 'num' or t.get('ud', '').startswith('名詞-数詞'):
            run += t['surf']      # 十 五 → 十五
            continue
        if run:
            v = kanji_number(run)
            if v is not None and not re.fullmatch(r'[一]', run):   # 一 alone is mostly "one/a" (一人, 一度 …)
                out.append(v)
            run = ''
    return out


def names(rec):
    """Person and place names the analyzer reads (UniDic 固有名詞, not organisations) with their readings."""
    out = []
    for t in rec.get('toks', []):
        ud = t.get('ud', '')
        if ud.startswith('名詞-固有名詞') and not ud.startswith('名詞-固有名詞-組織') and t.get('reading'):
            out.append(dict(surf=t['surf'], reading=t['reading'], kind=ud.split('-')[2] if ud.count('-') >= 2 else ''))
    return out


def expectations(rec):
    """Per-line expectations: sentences and clause facts (the features the L rules decided)."""
    if rec is None:
        return None
    sents = sentences(rec['src'])
    toks = rec.get('toks', [])
    clauses = []
    for ci, c in enumerate(rec.get('clauses', [])):
        tk = clause_tokens(rec, c)
        end = toks[tk[-1]]['src'][1] if tk and tk[-1] < len(toks) else 0
        sent = next((k for k, (a, b) in enumerate(sents) if a < end <= b), len(sents) - 1)
        clauses.append(dict(
            idx=ci, sent=sent,
            link=c.get('link', 'final'), pol=c.get('pol', 'pos'), negFn=c.get('negFn', 'none'),
            force=c.get('force', 'decl'), tense=c.get('tense', 'nonpast'), voice=c.get('voice', 'active'),
            bene=c.get('bene', 'none'), polite=c.get('polite', 'plain'), modality=c.get('modality', []),
            linkSub=c.get('linkSub', ''),
            toks=[toks[k]['surf'] for k in tk if k < len(toks)]))
    q = [any(ch in '？?' for ch in rec['src'].encode()[a:b].decode('utf-8', 'replace')) for a, b in sents]
    for c in clauses:
        c['qmark'] = q[c['sent']] if c['sent'] < len(q) else False
        c['rare'] = any(t in ('れ', 'られ', 'れる', 'られる', 'れれ') for t in c['toks'])
    return dict(src=rec['src'], sents=sents, clauses=clauses, numbers=numbers(rec), names=names(rec))
