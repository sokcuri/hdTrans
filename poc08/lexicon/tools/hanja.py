#!/usr/bin/env python3
"""Writes lex/hanja.tsv, the Korean readings of each kanji, from the Unihan database (Unicode License v3).

  tools/hanja.py UNIHAN_DIR [OUT]

UNIHAN_DIR holds Unihan_Readings.txt and Unihan_Variants.txt. A simplified or shinjitai form without its
own kHangul takes the readings of its traditional variant. Used by the converter to tell a translated
word from a transliterated name; not runtime data.
"""
import os
import re
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lex', 'hanja.tsv')
    read = {}
    ver = 'unknown'
    for line in open(os.path.join(root, 'Unihan_Readings.txt'), encoding='utf-8'):
        m = re.match(r'# Unicode Version (\S+)', line)
        if m:
            ver = m.group(1)
        if line.startswith('#') or '\tkHangul\t' not in line:
            continue
        code, _, val = line.rstrip('\n').split('\t', 2)
        read[chr(int(code[2:], 16))] = [v.split(':')[0] for v in val.split()]
    for line in open(os.path.join(root, 'Unihan_Variants.txt'), encoding='utf-8'):
        if line.startswith('#') or line.split('\t')[1:2] not in (['kTraditionalVariant'], ['kZVariant'], ['kSemanticVariant'], ['kSpecializedSemanticVariant'], ['kJapaneseOldVariant']):
            continue
        code, _, val = line.rstrip('\n').split('\t', 2)
        c = chr(int(code[2:], 16))
        if c in read:
            continue
        for v in val.split():
            t = chr(int(v.split('<')[0][2:], 16))
            if t in read:
                read[c] = read[t]
                break
    with open(out, 'w', encoding='utf-8') as o:
        o.write(f'#! hanja\n# Korean readings of kanji, from Unihan of Unicode {ver} (tools/hanja.py).\nkanji\thangul\n')
        for c in sorted(read):
            o.write(c + '\t' + ' '.join(dict.fromkeys(read[c])) + '\n')


if __name__ == '__main__':
    main()
