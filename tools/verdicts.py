#!/usr/bin/env python3
"""Counts the verdicts of a judged change and lists the lines two judges both call worse.

  tools/verdicts.py PAIRS.tsv FIRST.tsv [SECOND.tsv]
  tools/verdicts.py --self-test

PAIRS.tsv is the judge input (key, ja, old, new; tools/e2e-pairs.py or tools/measure.sh). FIRST
and SECOND hold `key verdict judge [note]` rows with verdict improved, same or worse, as
tools/judge.py writes them; FIRST judges every pair, SECOND the lines FIRST called worse or same.
A key judged in SECOND takes that verdict for the counts. Prints
`changed N improved X same Y worse Z unjudged U`, then `ratio improved:worse`, then each key both
judges call worse (`both-worse KEY`).
Exit: 0 counted, 1 some pair has no verdict, 2 invalid input.
"""
import collections
import csv
import sys
import tempfile
from pathlib import Path


def read(path, width):
    with open(path, encoding="utf-8", newline="") as f:
        return [r for r in csv.reader(f, delimiter="\t") if len(r) >= width and r[0]]


def summarize(keys, first, second):
    a = {r[0]: r[1] for r in first}
    b = {r[0]: r[1] for r in second}
    final = {k: b.get(k, a.get(k)) for k in keys}
    counts = collections.Counter(v for v in final.values() if v)
    unjudged = sum(1 for v in final.values() if not v)
    both = sorted(k for k in keys if a.get(k) == "worse" and b.get(k) == "worse")
    return counts, unjudged, both


def report(keys, counts, unjudged, both):
    print(f"changed {len(keys)} improved {counts['improved']} same {counts['same']} worse {counts['worse']} "
          f"unjudged {unjudged}")
    print(f"ratio {counts['improved']}:{counts['worse']}")
    for k in both:
        print(f"both-worse {k}")


def self_test():
    keys = ["s:1", "s:2", "s:3", "s:4"]
    first = [["s:1", "improved", "a"], ["s:2", "worse", "a"], ["s:3", "same", "a"]]
    second = [["s:2", "worse", "b"], ["s:3", "improved", "b"]]
    counts, unjudged, both = summarize(keys, first, second)
    assert (counts["improved"], counts["worse"], counts["same"], unjudged, both) == (2, 1, 0, 1, ["s:2"])
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "v.tsv"
        p.write_text('s:1\timproved\ta\t"note\twith tab"\n', encoding="utf-8")
        assert read(p, 2)[0][3] == "note\twith tab"
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) not in (2, 3):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    keys = [r[0] for r in read(argv[0], 4)]
    counts, unjudged, both = summarize(keys, read(argv[1], 2), read(argv[2], 2) if len(argv) == 3 else [])
    report(keys, counts, unjudged, both)
    return 1 if unjudged else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
