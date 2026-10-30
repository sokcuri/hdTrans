"""Rough kana → Hangul reading, used only to tell a transliterated name from a translated word."""

ROW = {
    'あ': '아', 'い': '이', 'う': '우', 'え': '에', 'お': '오',
    'か': '카', 'き': '키', 'く': '쿠', 'け': '케', 'こ': '코', 'が': '가', 'ぎ': '기', 'ぐ': '구', 'げ': '게', 'ご': '고',
    'さ': '사', 'し': '시', 'す': '스', 'せ': '세', 'そ': '소', 'ざ': '자', 'じ': '지', 'ず': '즈', 'ぜ': '제', 'ぞ': '조',
    'た': '타', 'ち': '치', 'つ': '츠', 'て': '테', 'と': '토', 'だ': '다', 'ぢ': '지', 'づ': '즈', 'で': '데', 'ど': '도',
    'な': '나', 'に': '니', 'ぬ': '누', 'ね': '네', 'の': '노',
    'は': '하', 'ひ': '히', 'ふ': '후', 'へ': '헤', 'ほ': '호', 'ば': '바', 'び': '비', 'ぶ': '부', 'べ': '베', 'ぼ': '보',
    'ぱ': '파', 'ぴ': '피', 'ぷ': '푸', 'ぺ': '페', 'ぽ': '포',
    'ま': '마', 'み': '미', 'む': '무', 'め': '메', 'も': '모',
    'や': '야', 'ゆ': '유', 'よ': '요', 'ら': '라', 'り': '리', 'る': '루', 'れ': '레', 'ろ': '로',
    'わ': '와', 'を': '오', 'ゔ': '부', 'ぁ': '아', 'ぃ': '이', 'ぅ': '우', 'ぇ': '에', 'ぉ': '오',
}
YOON = {'ゃ': 'ㅑ', 'ゅ': 'ㅠ', 'ょ': 'ㅛ'}
V = {'ㅏ': 0, 'ㅑ': 2, 'ㅓ': 4, 'ㅗ': 8, 'ㅛ': 12, 'ㅜ': 13, 'ㅠ': 17, 'ㅡ': 18, 'ㅣ': 20, 'ㅔ': 5}


def _set(syl, vowel=None, final=None):
    o = ord(syl) - 0xAC00
    i, v, f = o // 588, (o % 588) // 28, o % 28
    if vowel is not None:
        v = vowel
    if final is not None:
        f = final
    return chr(0xAC00 + (i * 21 + v) * 28 + f)


def to_hangul(kana):
    out = []
    s = ''.join(chr(ord(c) - 0x60) if 'ァ' <= c <= 'ヶ' else c for c in kana)
    for c in s:
        if c in YOON and out and '가' <= out[-1] <= '힣':
            out[-1] = _set(out[-1], vowel=V[YOON[c]])
        elif c == 'ん' and out and '가' <= out[-1] <= '힣':
            out[-1] = _set(out[-1], final=4)
        elif c == 'っ' and out and '가' <= out[-1] <= '힣':
            out[-1] = _set(out[-1], final=19)
        elif c == 'ー':
            continue
        elif c in ROW:
            out.append(ROW[c])
    return ''.join(out)


def onset(s):
    """Initial consonant index per Hangul syllable, voicing and aspiration merged."""
    merge = {0: 0, 1: 0, 15: 0, 3: 3, 4: 3, 16: 3, 7: 7, 8: 7, 17: 7, 9: 9, 10: 9, 12: 12, 13: 12, 14: 12}
    return [merge.get((ord(c) - 0xAC00) // 588, (ord(c) - 0xAC00) // 588) for c in s if '가' <= c <= '힣']


def similar(ko, kana):
    """Share of Korean syllables whose onset matches the kana reading in order (0..1)."""
    a = onset(ko.replace(' ', ''))
    b = onset(to_hangul(kana))
    if not a or not b:
        return 0.0
    # longest common subsequence over onsets
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b):
            cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return prev[-1] / max(len(a), len(b))
