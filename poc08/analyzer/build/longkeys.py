#!/usr/bin/env python3
"""Translation-dictionary keys → long-unit candidates for `hd8ana longunits`.

usage: longkeys.py DICT.tsv... > keys.tsv

Reads 0.7.x dictionary files (columns key, ko, pos, ...). The second character of the pos code is the class:
0 adnominal or linking expression, 1 noun, 3 adverb, 4 verb stem, 7 i-adjective stem, 8 na-adjective stem,
9 phrase. Particles (2), endings (5) and symbols (6) are function units the analyzer has already.
Keys that start or end with punctuation are contextual rows and are left out. Output: key, kind, class.
"""
import collections, re, sys

KIND = {'0': 'adn', '1': 'n', '3': 'adv', '4': 'v', '7': 'adj', '8': 'na', '9': 'phrase'}
JP = re.compile(r'^[ぁ-ヿ㐀-鿿豈-﫿々〆ヶー・]+$')


def main():
    stats = collections.Counter()
    seen = set()
    print('key\tkind\tclass')
    for path in sys.argv[1:]:
        with open(path, encoding='utf-8') as fh:
            header = None
            for line in fh:
                if line.startswith('#'):
                    continue
                f = line.rstrip('\n').split('\t')
                if header is None:
                    header = f
                    continue
                if len(f) < 3:
                    continue
                key, pos = f[0], f[2]
                cls = pos[1] if len(pos) > 1 else '?'
                stats['rows'] += 1
                stats['class ' + cls] += 1
                kind = KIND.get(cls)
                if kind is None:
                    stats['function class'] += 1
                    continue
                if kind == 'n' and len(pos) > 3 and pos[2] == '3':
                    kind = 'pn'
                if not JP.match(key):
                    stats['not plain Japanese'] += 1
                    continue
                if (key, kind) in seen:
                    stats['duplicate'] += 1
                    continue
                seen.add((key, kind))
                stats['kept'] += 1
                print(f'{key}\t{kind}\t{cls}')
    for k, v in sorted(stats.items()):
        print(f'{k}\t{v}', file=sys.stderr)


if __name__ == '__main__':
    main()
