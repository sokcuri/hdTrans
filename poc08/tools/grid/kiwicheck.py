#!/usr/bin/env python3
"""Cross-check a sample of the grid's Korean with Kiwi (a check of the oracle, never a source of it).

  PYTHON_WITH_KIWI kiwicheck.py GRID.tsv [--n 300] [--seed 8]

For each sampled row the expected Korean is analysed into morphemes and joined back (Kiwi.join); a row whose
re-joined text differs from the original is listed. A difference marks a form or a spacing worth a look: an ending
Kiwi does not recognise, a word it would split or join, or an irregular stem it reads differently.
"""
import argparse
import random
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('grid')
    ap.add_argument('--n', type=int, default=300)
    ap.add_argument('--seed', type=int, default=8)
    a = ap.parse_args()
    from kiwipiepy import Kiwi
    kiwi = Kiwi()
    with open(a.grid, encoding='utf-8') as f:
        rows = [l.rstrip('\n').split('\t') for l in f][1:]
    sample = random.Random(a.seed).sample(rows, min(a.n, len(rows)))
    diff = 0
    for rid, dims, ja, ko in sample:
        text = ko.strip('「」')
        joined = kiwi.join(kiwi.tokenize(text))
        if joined != text:
            diff += 1
            print('%s\t%s\t%s\tkiwi=%s' % (rid, ja, text, joined))
    print('sample %d, kiwi join differs %d' % (len(sample), diff), file=sys.stderr)


if __name__ == '__main__':
    main()
