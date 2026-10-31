#!/usr/bin/env python3
"""QE gate: does a Korean line realise what the Japanese analysis requires?

  gate.py IR.jsonl KO.txt [--json OUT.jsonl]    one verdict per line: the violated checks (empty = pass)

The Japanese side is the 0.8 IR (hdtrans08-ir --each --stages: W1 tokens, W2 clauses: polarity, negation
function, force, tense, voice, benefactive direction); the Korean side is Kiwi morphemes. Works on any
output (0.7.x, a retrofit, a neural n-best). Each violated check names what is missing. The checks are the rows of
data/check.tsv (qe/checks.py applies them); no learned table is read.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ja  # noqa: E402
import ko  # noqa: E402
import names  # noqa: E402
import checks  # noqa: E402


def units(exp, kos):
    """Pairs (clauses, korean sentence facts) to check: sentence by sentence when the counts agree, else the whole
    line as one unit."""
    if len(exp['sents']) == len(kos):
        return [([c for c in exp['clauses'] if c['sent'] == k], [kos[k]]) for k in range(len(kos))]
    merged = dict(ko.features([]))
    for k in kos:
        for key, v in k.items():
            if isinstance(v, bool):
                merged[key] = merged.get(key, False) or v
            elif isinstance(v, list):
                merged[key] = merged.get(key, []) + v
    merged['text'] = ' '.join(k['text'] for k in kos)
    merged['last'] = kos[-1] if kos else merged
    return [(exp['clauses'], [merged])]


RULES = None


def check(exp, kos, risks=None):
    """The violated checks of one (Japanese expectations, Korean analysis) pair: (check, clause index), from the rows
    of data/check.tsv. risks is kept for callers of the earlier API and stays empty."""
    global RULES
    if RULES is None:
        RULES = checks.load()
    return sorted({(c, i) for c, i, _ in checks.check(RULES, exp, kos, units, names.present)})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ir')
    ap.add_argument('ko')
    ap.add_argument('--json')
    a = ap.parse_args()
    recs = ja.load_ir(a.ir)
    kol = open(a.ko, encoding='utf-8').read().split('\n')
    out = []
    for i, r in enumerate(recs):
        exp = ja.expectations(r)
        kos = ko.analyse(kol[i] if i < len(kol) else '')
        out.append(dict(line=i + 1, v=check(exp, kos)))
    if a.json:
        with open(a.json, 'w') as f:
            for o in out:
                f.write(json.dumps(o, ensure_ascii=False) + '\n')
    for o in out:
        print('\t'.join(f'{c}@{k}' for c, k in o['v']))


if __name__ == '__main__':
    main()
