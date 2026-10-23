#!/usr/bin/env python3
"""Corpus chrF of line-aligned outputs against references.

  tools/chrf.py REF A [B]
  tools/chrf.py --self-test

chrF uses character 1..6-grams with spaces removed, beta 2, summed over all lines. With B it
prints `chrF A -> B; changed N better X worse Y`, where a changed line counts as better or worse
when its own chrF moves by more than 0.5.
"""
import collections
import sys


def stats(hyp, ref):
    hyp, ref = hyp.replace(" ", ""), ref.replace(" ", "")
    out = []
    for n in range(1, 7):
        h = collections.Counter(hyp[i:i + n] for i in range(len(hyp) - n + 1))
        r = collections.Counter(ref[i:i + n] for i in range(len(ref) - n + 1))
        out.append((sum((h & r).values()), sum(h.values()), sum(r.values())))
    return out


def chrf(st, beta=2.0):
    p = r = 0.0
    k = 0
    for match, hyp, ref in st:
        if hyp and ref:
            p += match / hyp
            r += match / ref
            k += 1
    if not k:
        return 0.0
    p, r = p / k, r / k
    return 0.0 if p + r == 0 else 100 * (1 + beta ** 2) * p * r / (beta ** 2 * p + r)


def total(pairs):
    tot = [[0, 0, 0] for _ in range(6)]
    for hyp, ref in pairs:
        for k, s in enumerate(stats(hyp, ref)):
            for j in range(3):
                tot[k][j] += s[j]
    return chrf(tot)


def compare(ref, a, b):
    changed = better = worse = 0
    for x, y, r in zip(a, b, ref):
        if x != y:
            changed += 1
            d = chrf(stats(y, r)) - chrf(stats(x, r))
            better += d > 0.5
            worse += d < -0.5
    return changed, better, worse


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read().split("\n")


def self_test():
    assert chrf(stats("고양이", "고양이")) == 100.0
    assert chrf(stats("", "고양이")) == 0.0
    assert stats("고 양", "고양") == stats("고양", "고양")
    assert 0 < total([("고양이다", "고양이이다")]) < 100
    assert compare(["고양이다"], ["개다"], ["고양이다"]) == (1, 1, 0)
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) not in (2, 3):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    ref, a = read(argv[0]), read(argv[1])
    if len(argv) == 2:
        print(f"chrF {total(zip(a, ref)):.2f}")
        return 0
    b = read(argv[2])
    if not len(ref) == len(a) == len(b):
        print(f"line counts differ: {len(ref)} {len(a)} {len(b)}", file=sys.stderr)
        return 2
    changed, better, worse = compare(ref, a, b)
    print(f"chrF {total(zip(a, ref)):.2f} -> {total(zip(b, ref)):.2f}; "
          f"changed {changed} better {better} worse {worse}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
