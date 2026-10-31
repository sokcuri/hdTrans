#!/usr/bin/env python3
"""Score engine output against the N5 grid.

  check.py GRID.tsv OUTPUT.txt [--cells] [--max-fail N]

GRID.tsv is the grid (id dims ja ko); OUTPUT.txt holds the engine's Korean, one line per grid row in the same order
(e.g. `cut -f3 GRID.tsv | tail -n +2 | hdtrans08 --poc08 native --each > OUTPUT.txt`). A row passes when its output is
exactly the expected Korean, spacing and punctuation included.

Prints the pass rate per block, per dimension value and per cell (only the cells with a failure unless --cells),
then the failing rows. Exit status 0 when every row passes.
"""
import argparse
import collections
import sys


def read_grid(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        header = f.readline().rstrip('\n').split('\t')
        if header[:4] != ['id', 'dims', 'ja', 'ko']:
            raise SystemExit('%s: expected the columns id dims ja ko' % path)
        for line in f:
            rid, dims, ja, ko = line.rstrip('\n').split('\t')[:4]
            rows.append((rid, dims, ja, ko))
    return rows


def table(title, counts, only_failing=False):
    items = [(k, p, n) for k, (p, n) in counts.items() if not (only_failing and p == n)]
    if not items:
        return
    print('%s' % title)
    width = min(max(len(k) for k, _, _ in items), 110)
    for k, p, n in items:
        print('  %-*s  %5d / %5d  %6.2f%%' % (width, k, p, n, 100.0 * p / n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('grid')
    ap.add_argument('output')
    ap.add_argument('--cells', action='store_true', help='print every cell, not only the failing ones')
    ap.add_argument('--max-fail', type=int, default=200, help='list at most N failing rows (0 = all)')
    a = ap.parse_args()
    rows = read_grid(a.grid)
    with open(a.output, encoding='utf-8') as f:
        outs = [l.rstrip('\n') for l in f]
    if len(outs) != len(rows):
        print('warning: %d output lines for %d grid rows; missing lines count as failures' % (len(outs), len(rows)),
              file=sys.stderr)
    outs += [''] * (len(rows) - len(outs))

    total = [0, 0]
    by_cell = collections.OrderedDict()
    by_dim = collections.defaultdict(lambda: [0, 0])
    by_block = collections.OrderedDict()
    fails = []
    for (rid, dims, ja, ko), got in zip(rows, outs):
        ok = got == ko
        kv = [d.split('=', 1) for d in dims.split(';')]
        block = dict(kv).get('block', '-')
        for agg, key in ((by_cell, dims), (by_block, block)):
            c = agg.setdefault(key, [0, 0])
            c[0] += ok
            c[1] += 1
        for k, v in kv:
            if k == 'block':
                continue
            c = by_dim['%s=%s' % (k, v)]
            c[0] += ok
            c[1] += 1
        total[0] += ok
        total[1] += 1
        if not ok:
            fails.append((rid, dims, ja, ko, got))

    print('pass %d / %d (%.2f%%)' % (total[0], total[1], 100.0 * total[0] / max(total[1], 1)))
    table('per block', by_block)
    table('per dimension value', collections.OrderedDict(sorted(by_dim.items())))
    table('per cell' if a.cells else 'per cell (failing cells)', by_cell, only_failing=not a.cells)
    if fails:
        shown = fails if a.max_fail == 0 else fails[:a.max_fail]
        print('failures (%d%s)' % (len(fails), '' if len(shown) == len(fails) else ', first %d' % len(shown)))
        for rid, dims, ja, ko, got in shown:
            print('  %s\t%s\n    ja   %s\n    want %s\n    got  %s' % (rid, dims, ja, ko, got))
    return 0 if total[0] == total[1] else 1


if __name__ == '__main__':
    sys.exit(main())
