#!/usr/bin/env python3
"""Export a frame pack: the decision rows of chosen lemmas as one override file, with their cases.

usage: framepack.py LEMMAS.txt OUT.tsv [--lex DIR] [--cases FILE ...] [--fault-rows FILE]

LEMMAS.txt: one lemma id per line (ja/yomi/pos), `#` comments allowed.
OUT.tsv columns: id slot lemma pos kind cond target pri prov note
  slot  the decision slot the row overrides on the 0.7.x chain: sense for every row of a frame pack
  kind  frame (a sense of the lemma: cond in the cue DSL, `*` for the default row) or colloc (a phrase whose
        pattern contains the lemma; cond = the pattern, and the row blocks decomposition)
  pri   tie-break only (higher first); resolution is fixed: every atom must hold, then lexical atoms > class
        depth > form/neighbour atoms > register, then pri. The default row has pri 0 and always holds.
  prov  review (reviewed lexicon row), fault:<id> (row added for a ledger fault), plus the row's own note.
OUT.cases.tsv: the fix/keep cases of the lemmas (id kind lemma ja ko src).
--fault-rows adds rows written for ledger faults (same columns as OUT.tsv, prov fault:<id>); they replace a lexicon
row with the same lemma and cond.
"""
import argparse
import collections
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lexconv import read_tsv  # noqa: E402

COLS = ['id', 'slot', 'lemma', 'pos', 'kind', 'cond', 'target', 'pri', 'prov', 'note']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lemmas')
    ap.add_argument('out')
    ap.add_argument('--lex', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lex'))
    ap.add_argument('--cases', action='append', default=[])
    ap.add_argument('--fault-rows')
    a = ap.parse_args()
    want = [l.split('#')[0].strip() for l in open(a.lemmas, encoding='utf-8')]
    want = [w for w in want if w]
    wantset = set(want)
    sen = collections.defaultdict(list)
    for f in sorted(glob.glob(os.path.join(a.lex, 'lexicon', '*.tsv'))):
        for r in read_tsv(f):
            lid = f"{r['ja']}/{r['yomi']}/{r['pos']}"
            if lid in wantset:
                sen[lid].append(r)
    ph = collections.defaultdict(list)
    for f in sorted(glob.glob(os.path.join(a.lex, 'phrases*.tsv'))):
        for r in read_tsv(f):
            if r['st'] not in ('r', 'm'):
                continue
            for lid in want:
                ja = lid.split('/')[0]
                if ja in r['pattern'] or r['note'] == lid:
                    ph[lid].append(r)
                    break
    fault = collections.defaultdict(list)
    if a.fault_rows:
        for r in read_tsv(a.fault_rows):
            fault[r['lemma']].append(r)
    rows, missing = [], []
    for lid in want:
        ja, yomi, pos = lid.split('/')
        ss = sorted(sen.get(lid, []), key=lambda r: int(r['sense']))
        if not ss:
            missing.append(lid)
        fconds = {r['cond'] for r in fault.get(lid, [])}
        n = len(ss)
        for r in ss:
            cond = r['cue'] or '*'
            if cond in fconds:
                continue
            pri = 0 if cond == '*' else 100 - int(r['sense'])
            prov = 'review' if r['st'] == 'r' else 'machine'
            rows.append([f"{ja}#{r['sense']}", 'sense', lid, pos, 'frame', cond, r['ko'], pri, prov, r['note']])
        for r in fault.get(lid, []):
            rows.append([r['id'], 'sense', lid, pos, r.get('kind') or 'frame', r['cond'], r['target'], r.get('pri') or 50,
                         r['prov'], r.get('note', '')])
        for i, r in enumerate(ph.get(lid, []), 1):
            rows.append([f'{ja}#p{i}', 'sense', lid, pos, 'colloc', r['pattern'], r['ko'], 200, 'review', r['cue']])
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('#! pack\n# Frame pack: decision rows (frame, colloc) of the listed lemmas, exported from the lexicon.\n')
        f.write('\t'.join(COLS) + '\n')
        for r in rows:
            f.write('\t'.join(str(x).replace('\t', ' ') for x in r) + '\n')
    lemmas = {lid.split('/')[0] for lid in want}
    cases = 0
    with open(os.path.splitext(a.out)[0] + '.cases.tsv', 'w', encoding='utf-8') as f:
        f.write('#! cases\n# Fix/keep cases of the pack lemmas.\nid\tkind\tlemma\tja\tko\tsrc\n')
        for cf in a.cases:
            for r in read_tsv(cf):
                if r['lemma'] in lemmas:
                    f.write('\t'.join(r[c] for c in ('id', 'kind', 'lemma', 'ja', 'ko', 'src')) + '\n')
                    cases += 1
    print(f'{len(want)} lemmas, {len(rows)} rows ({sum(r[4] == "colloc" for r in rows)} colloc), {cases} cases;'
          f' no entry: {" ".join(missing) or "-"}')


if __name__ == '__main__':
    main()
