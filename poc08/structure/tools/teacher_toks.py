#!/usr/bin/env python3
"""Stand-in teacher dump: UniDic 3.1 (fugashi) tokens as toks.jsonl (IR §7). Measurement only.

usage: teacher_toks.py [--doc NAME] < lines.txt > toks.jsonl
Each input line becomes one object {doc, line, src, norm, edits, toks}. Offsets are UTF-8 bytes.
"""
import json
import sys

POS1 = {'名詞': 'n', '代名詞': 'n', '形状詞': 'na', '副詞': 'adv', '連体詞': 'adn', '接続詞': 'conj',
        '感動詞': 'intj', '助動詞': 'aux', '接頭辞': 'pref', '接尾辞': 'suf', '形容詞': 'adj',
        '補助記号': 'sym', '記号': 'sym', '空白': 'sym'}
PART = {'格助詞': 'p_case', '係助詞': 'p_topic', '接続助詞': 'p_conj', '終助詞': 'p_final',
        '副助詞': 'p_adv', '準体助詞': 'p_case'}


def ir_pos(f):
    if f.pos1 == '動詞':
        ct = f.cType or ''
        if ct.startswith('五段') or ct.startswith('文語四段'):
            return 'v5'
        if 'サ行変格' in ct:
            return 'vs'
        if 'カ行変格' in ct:
            return 'vk'
        if 'ザ行変格' in ct:
            return 'vz'
        return 'v1'
    if f.pos1 == '名詞':
        if f.pos2 == '固有名詞':
            return 'pn'
        if f.pos2 == '数詞':
            return 'num'
        return 'n'
    if f.pos1 == '助詞':
        return PART.get(f.pos2, 'p_case')
    return POS1.get(f.pos1, 'unk')


def clean(x):
    return '' if x in (None, '*') else x


def main():
    import fugashi
    import unidic
    doc = 'stdin'
    if len(sys.argv) > 2 and sys.argv[1] == '--doc':
        doc = sys.argv[2]
    tagger = fugashi.Tagger('-d ' + unidic.DICDIR + ' -r ' + unidic.DICDIR + '/mecabrc')
    out = sys.stdout
    for n, line in enumerate(sys.stdin, 1):
        src = line.rstrip('\n')
        toks = []
        pos = 0
        for w in tagger(src):
            f = w.feature
            pos += len(w.white_space.encode())
            b = len(w.surface.encode())
            ud = '-'.join(x for x in (f.pos1, f.pos2, f.pos3, f.pos4) if clean(x))
            flags = []
            if w.white_space:
                flags.append('SpaceBefore')
            if w.is_unk:
                flags.append('Unknown')
            toks.append({'src': [pos, pos + b], 'surf': w.surface, 'lemma': clean(f.lemma) or w.surface,
                         'reading': clean(f.kana), 'pos': ir_pos(f), 'ud': ud, 'ctype': clean(f.cType),
                         'cform': clean(f.cForm), 'flags': flags, 'cost': 0, 'margin': 0})
            pos += b
        out.write(json.dumps({'doc': doc, 'line': n, 'src': src, 'norm': src, 'edits': [], 'toks': toks},
                             ensure_ascii=False) + '\n')


if __name__ == '__main__':
    main()
