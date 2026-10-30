#!/usr/bin/env python3
"""UniDic teacher dump in the toks.jsonl shape (IR.md §7). Measurement only; it is not shipped.

usage: teacher.py [--doc ID] < lines.txt > toks.jsonl      (needs fugashi and the unidic package)

Each line is tagged after a sentence mark (。), which is then dropped, so line-initial words read as
they do inside a text. Offsets are UTF-8 byte offsets into the input line.
"""
import json, sys

POS_MAP = None


def load_pos_map(path):
    rows = []
    with open(path, encoding='utf-8') as fh:
        header = False
        for line in fh:
            if line.startswith('#'):
                continue
            f = line.rstrip('\n').split('\t')
            if not header:
                header = True
                continue
            if len(f) >= 3:
                rows.append(('' if f[0] == '*' else f[0], '' if f[1] == '*' else f[1], f[2]))
    return rows


def pos_of(ud, ctype):
    for u, c, p in POS_MAP:
        if ud.startswith(u) and ctype.startswith(c):
            return p
    return 'unk'


def main():
    global POS_MAP
    import os
    import fugashi, unidic
    here = os.path.dirname(os.path.abspath(__file__))
    POS_MAP = load_pos_map(os.path.join(here, '..', 'data', 'pos-map.tsv'))
    doc = sys.argv[sys.argv.index('--doc') + 1] if '--doc' in sys.argv else ''
    tagger = fugashi.Tagger('-d ' + unidic.DICDIR + ' -r ' + unidic.DICDIR + '/mecabrc')
    for n, line in enumerate(sys.stdin, 1):
        src = line.rstrip('\n')
        words = list(tagger('。' + src))
        if words and words[0].surface == '。' and not words[0].white_space:
            words = words[1:]
        else:
            words = list(tagger(src))
        toks, pos, space = [], 0, False
        for w in words:
            pos += len(w.white_space.encode())
            space = space or bool(w.white_space)
            b = pos
            pos += len(w.surface.encode())
            f = w.feature
            if f.pos1 == '空白':
                space = True
                continue
            ud = '-'.join(x for x in (f.pos1, f.pos2, f.pos3, f.pos4) if x and x != '*')
            ctype = f.cType if f.cType and f.cType != '*' else ''
            flags = []
            if w.is_unk:
                flags.append('unknown')
            if space and toks:
                flags.append('space-before')
            space = False
            toks.append({'src': [b, pos], 'surf': w.surface, 'lemma': f.lemma or w.surface,
                         'reading': f.kana if f.kana and f.kana != '*' else '', 'pos': pos_of(ud, ctype),
                         'ud': ud, 'ctype': ctype, 'cform': f.cForm if f.cForm and f.cForm != '*' else '',
                         'flags': flags, 'cost': 0, 'margin': 0})
        rec = {'line': n, 'src': src, 'norm': src, 'edits': [], 'toks': toks}
        if doc:
            rec = {'doc': doc, **rec}
        print(json.dumps(rec, ensure_ascii=False))


if __name__ == '__main__':
    main()
