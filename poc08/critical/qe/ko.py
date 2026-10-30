"""Korean side of the QE gate: what a Korean line realises, read from Kiwi morphemes (offline; the runtime
version reads the generator's ending tables instead).

Per sentence: negation markers by kind, potential (ㄹ 수 있다/없다), the 잖 ending, question, obligation
(-어야 하다/되다, -야지), prohibition (-지 말다, -면 안 되다), past, honorific -시-, giving (주다/드리다),
the -어 받다 calque, numbers and the Hangul text.
"""
import re

_kiwi = None


def kiwi():
    global _kiwi
    if _kiwi is None:
        from kiwipiepy import Kiwi
        _kiwi = Kiwi()
    return _kiwi


ENDS = set('.!?。！？')
Q_EF = re.compile(r'(까|까요|냐|냐고|니|나|나요|가|가요|ㄹ래|을래|래|래요|ㄹ래요|을래요|ㄴ가|은가|는가|던가|ㄹ까|을까|ㄹ까요|을까요|겠어|지)$')


def sentences(ko):
    """Character spans of the Korean sentences (cut after .!?。！？ runs at quote depth 0; an ASCII double quote
    opens and closes a quote; a run of two or more dots is an ellipsis, not an end)."""
    spans, start, depth, i, n = [], 0, 0, 0, len(ko)
    dq = False
    while i < n:
        ch = ko[i]
        if ch in '「『(“‘':
            depth += 1
        elif ch in '」』)”’':
            depth = max(0, depth - 1)
        elif ch == '"':
            dq = not dq
            depth = depth + 1 if dq else max(0, depth - 1)
        if ch == '.' and ((i + 1 < n and ko[i + 1] == '.') or (i > 0 and ko[i - 1] == '.')):
            i += 1
            continue
        if ch in ENDS and depth == 0:
            j = i + 1
            while j < n and (ko[j] in ENDS or ko[j] in '」』)”’…~ "\''):
                j += 1
            spans.append((start, j))
            start, i = j, j
            continue
        i += 1
    if start < n and ko[start:].strip():
        spans.append((start, n))
    # a piece without letters (a stray quote or ellipsis) belongs to the sentence before it
    out = []
    for a, b in spans:
        if out and not any(ch.isalnum() for ch in ko[a:b]):
            out[-1] = (out[-1][0], b)
        else:
            out.append((a, b))
    return out or [(0, n)]


SINO = {'영': 0, '일': 1, '이': 2, '삼': 3, '사': 4, '오': 5, '육': 6, '륙': 6, '칠': 7, '팔': 8, '구': 9}
NATIVE = {'하나': 1, '한': 1, '둘': 2, '두': 2, '셋': 3, '세': 3, '석': 3, '넷': 4, '네': 4, '다섯': 5, '여섯': 6,
          '일곱': 7, '여덟': 8, '아홉': 9, '열': 10, '스물': 20, '스무': 20, '서른': 30, '마흔': 40, '쉰': 50,
          '예순': 60, '일흔': 70, '여든': 80, '아흔': 90}


NATIVE_RE = re.compile(r'(열|스물|스무|서른|마흔|쉰|예순|일흔|여든|아흔)?(하나|한|둘|두|셋|세|넷|네|다섯|여섯|일곱|여덟|아홉)?')
DAYS = {'하루': 1, '이틀': 2, '사흘': 3, '나흘': 4, '닷새': 5, '엿새': 6, '이레': 7, '여드레': 8, '아흐레': 9, '열흘': 10}
BIGU = {'십': 10, '백': 100, '천': 1000, '만': 10000, '억': 10 ** 8}


def numbers(ko, toks):
    import unicodedata
    t = unicodedata.normalize('NFKC', ko)
    out = []
    for m in re.finditer(r'(\d+(?:,\d{3})*)\s*([십백천만억])?', t):
        v = int(m.group(1).replace(',', ''))
        out.append(v)
        if m.group(2):
            out.append(v * BIGU[m.group(2)])     # 5만 → 50000
    for m in NATIVE_RE.finditer(t):
        if m.group(0):
            v = (NATIVE.get(m.group(1), 0) if m.group(1) else 0) + (NATIVE.get(m.group(2), 0) if m.group(2) else 0)
            out.append(v)
            if m.group(1) and m.group(2):
                out.append(NATIVE[m.group(1)])
    for k, v in DAYS.items():
        if k in t:
            out.append(v)
    for m in re.finditer(r'(한|두|세|네|다섯)(두|세|네|다섯|여섯)', t):   # 두세 번, 네다섯 살
        out += [NATIVE[m.group(1)], NATIVE[m.group(2)]]
    prev = None
    for t in toks:
        v = None
        if t.tag in ('NR', 'MM', 'NNG', 'NNB') and t.form in NATIVE:
            v = NATIVE[t.form]
        elif t.tag == 'NR':
            v = sino(t.form)
        if v is not None:
            out.append(v)
            # native tens + units written together (스물한, 열두): add the sum
            if prev is not None and prev >= 10 and prev % 10 == 0 and v < 10:
                out.append(prev + v)
        prev = v
    return out


def sino(s):
    units = {'십': 10, '백': 100, '천': 1000, '만': 10000}
    if not s or any(ch not in SINO and ch not in units for ch in s):
        return None
    total, cur = 0, 0
    for ch in s:
        if ch in SINO:
            cur = SINO[ch]
        else:
            total += (cur or 1) * units[ch]
            cur = 0
    return total + cur


JAMO = str.maketrans({'ᆫ': 'ㄴ', 'ᆯ': 'ㄹ', 'ᆷ': 'ㅁ', 'ᆸ': 'ㅂ', 'ᆻ': 'ㅆ', 'ᆼ': 'ㅇ'})


class T:
    """A Kiwi token with its form in compatibility jamo (Kiwi writes ᆫ, ᆯ … for bare final consonants)."""
    __slots__ = ('form', 'tag')

    def __init__(self, t):
        self.form = t.form.translate(JAMO)
        self.tag = t.tag


def features(toks):
    """Facts of one Korean sentence from its Kiwi tokens."""
    toks = [t if isinstance(t, T) else T(t) for t in toks]
    f = dict(neg=[], pot_neg=False, pot_pos=False, jana=False, q=False, oblig=False, prohib=False, past=False,
             hon=False, give=False, calque=False, before=False, qemb=False, npot=0, efs=[])
    n = len(toks)
    for i, t in enumerate(toks):
        form, tag = t.form, t.tag
        prev = toks[i - 1] if i else None
        nxt = toks[i + 1] if i + 1 < n else None
        if tag == 'MAG' and form in ('안', '못'):
            f['neg'].append(form)
        elif tag.startswith('VX') and form in ('않', '못하'):
            f['neg'].append(form)
        elif tag.startswith('VX') and form == '말' and prev is not None and prev.form == '지':
            f['neg'].append('말')
            f['prohib'] = True
        elif tag.startswith(('VA', 'VV')) and form.endswith('없'):
            f['neg'].append('없')        # 없다 and its compounds (변함없다, 틀림없다, 쓸데없다)
        elif tag == 'MAG' and form.endswith('없이'):
            f['neg'].append('없')        # 없이, 예외 없이, 끊임없이
        elif tag.startswith('VV') and form == '못하':
            f['neg'].append('못하')
        elif tag == 'VCN':
            f['neg'].append('아니')
        elif tag.startswith('VV') and form == '모르':
            f['neg'].append('모르')
        if form == '수' and tag == 'NNB' and nxt is not None:
            # 수 (가/는/도) 없다/있다
            k = i + 1
            while k < n and toks[k].tag.startswith('J'):
                k += 1
            if k < n and toks[k].form == '없':
                f['pot_neg'] = True
                f['npot'] += 1
            if k < n and toks[k].form == '있':
                f['pot_pos'] = True
                f['npot'] += 1
        if form in ('불가능',) and tag.startswith('NN'):
            f['pot_neg'] = True
        if form in ('가능',) and tag.startswith('NN'):
            f['pot_pos'] = True
        if form == '못' and tag == 'MAG' or form == '못하' and tag.startswith('VX'):
            f['pot_neg'] = True
        if tag in ('EF', 'EC') and form.startswith('잖'):
            f['jana'] = True
        if tag == 'EC' and form in ('어야', '아야', '여야', '야') and nxt is not None and nxt.form in ('하', '되', '돼'):
            f['oblig'] = True
        if tag == 'EF' and form.endswith('야지') or tag == 'EF' and form.endswith('야죠'):
            f['oblig'] = True
        if (form == '안' and tag == 'MAG' and nxt is not None and nxt.form in ('되', '돼')) or \
                (form in ('안되', '안돼') and tag.startswith('VV')):
            # 안 되다 after a connective (-면, -어서는, -어선): a ban; after -지 않으면: an obligation
            k = i - 1
            while k >= 0 and toks[k].tag.startswith('J'):
                k -= 1
            if k >= 0 and toks[k].tag == 'EC' and toks[k].form in ('면', '으면', '어서', '아서', '여서', '서', '어선', '아선', '선', '어야', '아야'):
                f['prohib'] = True
                if k >= 1 and toks[k - 1].form == '않':
                    f['oblig'] = True
        if form in ('안되', '안돼') and tag.startswith('VV'):
            f['neg'].append('안')
        if tag == 'EP' and form in ('었', '았', '였', 'ㅆ', '었었', '았었'):
            f['past'] = True
        if tag == 'ETM' and form in ('ㄴ', '은', '던') and prev is not None and prev.tag.startswith(('VV', 'XSV', 'VX')):
            f['past'] = True      # past adnominal (쓴 거, 된 것 같다)
        if tag == 'EP' and form in ('시', '으시', '셨', '으셨'):
            f['hon'] = True
        if tag == 'EF' and ('세요' in form or '십시오' in form or '셔요' in form or '십니' in form):
            f['hon'] = True
        if tag == 'NNG' and form == '전' and prev is not None and prev.tag == 'ETN' and prev.form == '기':
            f['before'] = True        # -기 전에 (ないうちに: "before … not yet")
        if form in ('계시', '드시', '주무시', '말씀', '여쭈', '뵙', '드리') and tag[0] in 'VN':
            f['hon'] = True
        if form == '바라' and tag.startswith('VV'):
            k = i - 1
            while k >= 0 and toks[k].tag.startswith('J'):
                k -= 1
            if k >= 1 and toks[k].tag == 'ETN' and toks[k - 1].form in ('않', '말'):
                f['prohib'] = True    # -지 않기를 바라다 (a negative wish)
        if tag.startswith(('VX', 'VV')) and (form in ('주', '드리') or form.endswith(('주', '드리')) and len(form) > 1):
            f['give'] = True          # 주다, 드리다 and the lexicalised 도와주다, 빌려주다 …
        if form == '받' and tag.startswith(('VV', 'VX')) and prev is not None and prev.tag == 'EC' and prev.form in ('어', '아', '여'):
            f['calque'] = True
        if tag == 'EC' and form.endswith(('까', '냐', '는지', 'ㄴ지', '은지', 'ㄹ지', '을지', '던지')):
            f['qemb'] = True          # an embedded question (-ㄹ까 하고, -는지)
        if tag == 'EF':
            f['efs'].append(form)
            if form.endswith(('까', '까요', '냐', '니', '나요', '래', '래요', '는가', '은가', 'ㄴ가', '던가', '나')):
                f['q'] = True
        if tag == 'SF' and form in ('?', '？'):
            f['q'] = True
    # -지 않을 수 없다 / -지 않으면 안 되다: two negations that make an obligation
    if f['pot_neg'] and f['neg'].count('않') >= 1 and len(f['neg']) >= 2:
        f['oblig'] = True
    return f


def analyse(ko):
    """Korean line → list of sentence dicts: span, text, features; plus line numbers."""
    k = kiwi()
    out = []
    for a, b in sentences(ko):
        text = ko[a:b]
        toks = k.tokenize(text) if text.strip() else []
        f = features(toks)
        f['span'] = (a, b)
        f['text'] = text
        if '?' in text or '？' in text:
            f['q'] = True
        f['numbers'] = numbers(text, toks)
        out.append(f)
    return out
