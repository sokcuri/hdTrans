#!/usr/bin/env python3
"""Select the most frequent lemmas and write review packets (draft senses, 0.7 rows, corpus evidence).

usage: review_packets.py --corpus DIR --out DIR [--top 2000] [--size 50] [--dicts DIR] [--draft FILE...]

DIR holds lemmas.tsv, examples.json and colloc.json from corpus_count.py. The packets quote corpus lines,
so --out must be outside the repository. --draft adds hand-written draft rows (lexicon format) that
replace the converted draft of their lemma.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexlib  # noqa: E402
from lexconv import read_tsv  # noqa: E402


def select(corpus, top):
    out = []
    for r in read_tsv(os.path.join(corpus, 'lemmas.tsv')):
        if r['pos'] not in lexlib.CONTENT:
            continue
        if re.fullmatch(r'[\x00-\x7f]+', r['ja']):
            continue
        if int(r.get('fiction', 0)) + int(r.get('numreg', 0)) + int(r.get('game', 0)) < 5:
            continue
        out.append(r)
    return out[:top] if top else out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--corpus', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--top', type=int, default=2000)
    ap.add_argument('--size', type=int, default=50)
    ap.add_argument('--dicts', default=os.path.join(lexlib.REPO, 'dicts'))
    ap.add_argument('--draft', action='append', default=[])
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    lem = select(a.corpus, a.top)
    ex = json.load(open(os.path.join(a.corpus, 'examples.json'), encoding='utf-8'))
    co = json.load(open(os.path.join(a.corpus, 'colloc.json'), encoding='utf-8'))

    draft = collections.defaultdict(list)
    by_ja = collections.defaultdict(set)
    for f in sorted(glob.glob(os.path.join(lexlib.LEX, 'lexicon', 'gen-*.tsv'))) + [os.path.join(lexlib.LEX, 'by-sa', 'lexicon.tsv')]:
        for r in read_tsv(f):
            lid = lexlib.lemma_id(r['ja'], r['yomi'], r['pos'])
            draft[lid].append(r)
            by_ja[r['ja']].add(lid)
    hand = collections.defaultdict(list)
    for f in a.draft:
        for r in read_tsv(f):
            hand[lexlib.lemma_id(r['ja'], r['yomi'], r['pos'])].append(r)
    phr = collections.defaultdict(list)
    for r in read_tsv(os.path.join(lexlib.LEX, 'phrases-gen.tsv')):
        phr[r['pattern'][:1]].append(r)

    rows = lexlib.load_rows(a.dicts)
    by_key = collections.defaultdict(list)
    for r in rows:
        if r['layer'] != 'names' and lexlib.row_kind(r) not in ('ending', 'particle', 'symbol'):
            by_key[r['key']].append(r)
            by_key[lexlib.jdict(r)].append(r)

    def old_rows(r):
        keys = [r['ja'], r['yomi']]
        for f in r['forms'].split(' '):
            s = f.rsplit(':', 1)[0]
            if s:
                keys.append(s)
                if r['pos'] in lexlib.PRED and len(s) > 1:
                    keys.append(s[:-1])
        seen, out = set(), []
        for k in keys:
            for x in by_key.get(k, []):
                rid = (x['file'], x['line'])
                if rid in seen:
                    continue
                seen.add(rid)
                out.append(x)
        return out

    ids = []
    for pi in range(0, len(lem), a.size):
        part = lem[pi:pi + a.size]
        no = pi // a.size + 1
        lines = []
        for r in part:
            lid = r['lid']
            ids.append((no, lid))
            lines.append(f'=== LEMMA {lid}   (UniDic {r["pos1"]}/{r["pos2"]} {r["ctype"]}; dev tokens {r["total"]}: '
                         f'fiction {int(r["fiction"]) + int(r["numreg"])}, game {r["game"]}, captions {r["caption"]}; '
                         f'surface forms {r["forms"]})')
            src = hand.get(lid) or draft.get(lid, [])
            tag = 'hand-written draft' if hand.get(lid) else 'converted draft'
            lines.append(f'-- {tag} senses (sense | ko | cue | note):')
            if not src:
                lines.append('   (none: no converted row has this lemma id)')
            for d in src[:14]:
                lines.append(f'   {d["sense"]} | {d["ko"]} | {d.get("cue", "")} | {d.get("note", "")}')
            if len(src) > 14:
                lines.append(f'   ... {len(src) - 14} more converted senses not shown')
            sib = sorted(x for x in by_ja.get(r['ja'], ()) if x != lid)
            for s in sib[:4]:
                ks = ', '.join(d['ko'] for d in draft[s][:5])
                lines.append(f'-- sibling lemma {s}: {ks}')
            old = old_rows(r)
            if old:
                lines.append('-- 0.7.5 rows (key | ko | pos | file | rule | note):')
                for x in old[:14]:
                    rule = (x.get('ctx') or '')[:80]
                    note = (x.get('note') or '')[:160]
                    lines.append(f'   {x["key"]} | {x["ko"]} | {x["pos"]} | {x["file"]} | {rule} | {note}')
                if len(old) > 14:
                    lines.append(f'   ... {len(old) - 14} more rows')
            c = co.get(lid, [])
            facts = [f'{k} {n}' for k, n in c if not k.startswith(('prev=', 'next='))][:16]
            nx = [f'{k} {n}' for k, n in c if k.startswith('next=')][:5]
            if facts or nx:
                lines.append('-- corpus collocations (counts): ' + '; '.join(facts + nx))
            ph = [p for p in phr.get(r['ja'][:1], []) if p['pattern'].startswith(r['ja'])][:6]
            if ph:
                lines.append('-- converted phrases starting with it: ' + '; '.join(f'{p["pattern"]} → {p["ko"]}' for p in ph))
            lines.append('-- corpus examples:')
            shown = []
            for e in ex.get(lid, []):
                if e[1] not in [x[1] for x in shown]:
                    shown.append(e)
            for e in shown[:6]:
                ref = f'   [human ref: {e[2]}]' if e[2] else ''
                lines.append(f'   ({e[0]}) {e[1]}{ref}')
            lines.append('')
        with open(os.path.join(a.out, f'packet-{no:02d}.txt'), 'w', encoding='utf-8') as o:
            o.write('\n'.join(lines))
    with open(os.path.join(a.out, 'ids.tsv'), 'w', encoding='utf-8') as o:
        o.write('packet\tlid\n')
        for no, lid in ids:
            o.write(f'{no}\t{lid}\n')
    print('lemmas', len(lem), 'packets', (len(lem) + a.size - 1) // a.size,
          'with draft', sum(1 for r in lem if draft.get(r['lid']) or hand.get(r['lid'])))


if __name__ == '__main__':
    main()
