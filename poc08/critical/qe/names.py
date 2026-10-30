"""Is a Japanese name (its katakana reading) written in a Korean line? Kana → Hangul by the standard notation,
compared on a skeleton that ignores the plain/aspirated contrast (가/카, 다/타, 조/초), long vowels and the
final ㅅ/ㄴ of small っ/ん, so that 타카세가와 and 다카세가와 both count."""
import unicodedata

V = {'ア': 'a', 'イ': 'i', 'ウ': 'u', 'エ': 'e', 'オ': 'o'}
ROWS = {
    'カキクケコ': 'k', 'ガギグゲゴ': 'g', 'サシスセソ': 's', 'ザジズゼゾ': 'z', 'タチツテト': 't', 'ダヂヅデド': 'd',
    'ナニヌネノ': 'n', 'ハヒフヘホ': 'h', 'バビブベボ': 'b', 'パピプペポ': 'p', 'マミムメモ': 'm', 'ラリルレロ': 'r',
}
MORA = {}
for row, c in ROWS.items():
    for ch, v in zip(row, 'aiueo'):
        MORA[ch] = (c, v)
for ch, v in V.items():
    MORA[ch] = ('', v)
MORA.update({'ヤ': ('', 'ya'), 'ユ': ('', 'yu'), 'ヨ': ('', 'yo'), 'ワ': ('', 'wa'), 'ヲ': ('', 'o'), 'ヴ': ('b', 'u')})
MORA['シ'] = ('sh', 'i')
MORA['チ'] = ('ch', 'i')
MORA['ツ'] = ('ts', 'u')
MORA['ジ'] = ('j', 'i')
MORA['ヂ'] = ('j', 'i')
MORA['ヅ'] = ('z', 'u')
SMALL = {'ャ': 'ya', 'ュ': 'yu', 'ョ': 'yo', 'ァ': 'a', 'ィ': 'i', 'ゥ': 'u', 'ェ': 'e', 'ォ': 'o'}

# skeleton consonant classes (plain = aspirated = tense)
CC = {'k': 'K', 'g': 'K', 's': 'S', 'sh': 'S', 'z': 'J', 'j': 'J', 'ch': 'J', 'ts': 'S', 't': 'T', 'd': 'T',
      'n': 'N', 'h': 'H', 'b': 'P', 'p': 'P', 'm': 'M', 'r': 'R', '': ''}
# Hangul jamo → skeleton
CHO = ['K', 'K', 'N', 'T', 'T', 'R', 'M', 'P', 'P', 'S', 'S', '', 'J', 'J', 'J', 'K', 'T', 'P', 'H']
JUNG = ['a', 'ae', 'ya', 'yae', 'eo', 'e', 'yeo', 'ye', 'o', 'wa', 'wae', 'oe', 'yo', 'u', 'wo', 'we', 'wi', 'yu',
        'eu', 'ui', 'i']
VMAP = {'a': 'a', 'i': 'i', 'u': 'u', 'eu': 'u', 'e': 'e', 'ae': 'e', 'o': 'o', 'eo': 'o', 'ya': 'ya', 'yu': 'yu',
        'yo': 'yo', 'wa': 'wa'}


def _flat(sk):
    # Korean writes シュ/ジョ/チャ as 슈/조/차: y-glides after S and J are not written
    return [x[0] + x[2:] if len(x) > 2 and x[0] in 'SJ' and x[1] == 'y' else x for x in sk]


def skeleton_kana(reading):
    r = unicodedata.normalize('NFKC', reading)
    r = ''.join(chr(ord(ch) + 0x60) if 'ぁ' <= ch <= 'ゖ' else ch for ch in r)   # hiragana → katakana
    out = []
    i = 0
    while i < len(r):
        ch = r[i]
        if ch in MORA:
            c, v = MORA[ch]
            if i + 1 < len(r) and r[i + 1] in SMALL:
                s = SMALL[r[i + 1]]
                v = s if c in ('', 'k', 'g', 'n', 'h', 'b', 'p', 'm', 'r') and s.startswith('y') else s[-1:] if s[0] == 'y' else s
                if c in ('sh', 'ch', 'j') and s.startswith('y'):
                    v = s[1:]
                i += 1
            out.append(CC[c] + VMAP.get(v, v))
        elif ch in 'ーッン':
            pass   # long vowel, gemination and final n are written in more than one way
        i += 1
    return _flat(out)


def skeleton_hangul(text):
    out = []
    for ch in text:
        o = ord(ch)
        if 0xAC00 <= o <= 0xD7A3:
            s = o - 0xAC00
            cho, jung = s // 588, (s % 588) // 28
            out.append(CHO[cho] + VMAP.get(JUNG[jung], JUNG[jung]))
        else:
            out.append(' ')
    return _flat(out)


def present(name, text):
    """True when the name's reading appears in the text (or the name is written in its own script there)."""
    if name['surf'] in text:
        return True
    if all('ァ' <= ch <= 'ヿ' for ch in name['surf']):
        return True     # a katakana name follows its source language, not its kana: not checked here
    k = skeleton_kana(name['reading'])
    if len(k) < 2:
        return True     # too short to tell
    h = skeleton_hangul(text)
    n = len(k)
    for i in range(len(h) - n + 1):
        if h[i:i + n] == k:
            return True
    # long vowels written out (유우키 for ユーキ is rare; 오오/오 both seen): drop vowel-only syllables and retry
    k2 = [x for j, x in enumerate(k) if not (x in ('a', 'i', 'u', 'e', 'o') and j > 0)]
    h2 = [x for j, x in enumerate(h) if not (x in ('a', 'i', 'u', 'e', 'o') and j > 0 and h[j - 1] != ' ')]
    for i in range(len(h2) - len(k2) + 1):
        if h2[i:i + len(k2)] == k2:
            return True
    return False
