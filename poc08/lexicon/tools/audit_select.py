#!/usr/bin/env python3
"""Tail lexicon audit, step 1: pick lemmas, run the 0.7 engine on example lines, write audit packets.

usage: audit_select.py --corpus DIR --engine BIN --dicts DIR --out DIR [--ranks 500:20000] [--examples 3]
                       [--size 150] [--avoid FILE ...] [--fresh N --fresh-src NAME=GLOB ... --pool PY]

--corpus holds lemmas.tsv and examples.json (corpus_count.py). --avoid files hold lines (one per line, or
TSV with a `ja` column) that no packet may show: measurement lines. With --fresh, N lines are drawn first
from --fresh-src, outside --avoid and outside the exclusion pool that the --pool script's load() returns,
and written to OUT/fresh.tsv; they are then avoided too. Packets quote corpus lines: OUT must be outside
the repository.
"""
import argparse
import collections
import glob
import importlib.util
import json
import os
import random
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexlib  # noqa: E402
from lexconv import read_tsv  # noqa: E402

AUDIT_POS = {'n', 'v1', 'v5', 'adj', 'na', 'adv', 'intj', 'adn', 'conj'}
RULED = {'LocalRule', 'GlobalRule', 'Sense', 'SenseDefault', 'Rewrite', 'Initial', 'Cg'}
KANA = re.compile(r'[぀-ゟ゠-ヿ]')


def lines_of(path):
    out = set()
    if path.endswith('.tsv'):
        for r in read_tsv(path):
            if r.get('ja'):
                out.add(r['ja'].strip())
        return out
    for ln in open(path, encoding='utf-8', errors='replace'):
        if ln.strip():
            out.add(ln.strip())
    return out


def run_engine(engine, dicts, lines, env):
    """0.7 engine --json --each over lines; returns (outputs, choices per line)."""
    outs, choices = [], []
    for i in range(0, len(lines), 20000):
        part = lines[i:i + 20000]
        p = subprocess.run([engine, '--dicts', dicts, '--json', '--each'], input='\n'.join(part) + '\n',
                           capture_output=True, text=True, env=env)
        d = json.loads(p.stdout)
        text = d['text'].split('\n')
        per = [[] for _ in part]
        for c in d['choices']:
            if c['line'] < len(per):
                per[c['line']].append(c)
        outs += text[:len(part)] + [''] * max(0, len(part) - len(text))
        choices += per
    return outs, choices


def byte_starts(line, toks):
    """Byte offset of each analyzer token in line."""
    out, pos = [], 0
    for t in toks:
        i = line.find(t.surf, pos)
        if i < 0:
            out.append(-1)
            continue
        out.append(len(line[:i].encode('utf-8')))
        pos = i + len(t.surf)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--corpus', required=True)
    ap.add_argument('--engine', required=True)
    ap.add_argument('--dicts', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--ranks', default='500:20000')
    ap.add_argument('--examples', type=int, default=3)
    ap.add_argument('--size', type=int, default=150)
    ap.add_argument('--avoid', action='append', default=[])
    ap.add_argument('--avoid-fresh', action='append', default=[], help='lines kept out of the fresh draw only')
    ap.add_argument('--fresh', type=int, default=0)
    ap.add_argument('--fresh-src', action='append', default=[])
    ap.add_argument('--pool', default='')
    ap.add_argument('--seed', type=int, default=803)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    env = dict(os.environ, HDTRANS_CACHE_DIR='off')
    lo, hi = (int(x) for x in a.ranks.split(':'))

    avoid = set()
    for f in a.avoid:
        avoid |= lines_of(f)
    ex = json.load(open(os.path.join(a.corpus, 'examples.json'), encoding='utf-8'))

    if a.fresh:
        pool = set()
        if a.pool:
            spec = importlib.util.spec_from_file_location('pool', a.pool)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            got = mod.load()
            pool = got[0] if isinstance(got, tuple) else got
        rnd = random.Random(a.seed)
        per = a.fresh // max(1, len(a.fresh_src))
        fresh = []
        for spec_ in a.fresh_src:
            name, g = spec_.split('=', 1)
            cand = []
            avoid_fresh = set()
            for af in a.avoid_fresh:
                avoid_fresh |= lines_of(af)
            for f in sorted(glob.glob(g)):
                for ln in open(f, encoding='utf-8', errors='replace'):
                    s = ln.strip()
                    if 8 <= len(s) <= 80 and KANA.search(s) and s not in pool and s not in avoid and s not in avoid_fresh:
                        cand.append(s)
            cand = sorted(set(cand))
            rnd.shuffle(cand)
            fresh += [(name, s) for s in cand[:per]]
        with open(os.path.join(a.out, 'fresh.tsv'), 'w', encoding='utf-8') as o:
            o.write('id\tset\tja\n')
            for i, (n, s) in enumerate(fresh, 1):
                o.write(f'fr{i:03d}\t{n}\t{s}\n')
        avoid |= {s for _, s in fresh}

    lem = []
    for r in read_tsv(os.path.join(a.corpus, 'lemmas.tsv')):
        if r['pos'] not in lexlib.CONTENT or re.fullmatch(r'[\x00-\x7f]+', r['ja']):
            continue
        kana_lines = [e for e in ex.get(r['lid'], []) if KANA.search(e[1])]
        if len(kana_lines) < 3:
            continue
        lem.append(r)
    for i, r in enumerate(lem):
        r['rank'] = i + 1
    tail = [r for r in lem[lo - 1:hi] if r['pos'] in AUDIT_POS]

    picks = []
    for r in tail:
        cands = [e for e in ex.get(r['lid'], []) if KANA.search(e[1]) and e[1] not in avoid and 6 <= len(e[1]) <= 70]
        seen = []
        for e in sorted(cands, key=lambda e: len(e[1])):
            if e[1] not in seen:
                seen.append(e[1])
        if seen:
            picks.append((r, seen[:a.examples]))
    all_lines = sorted({s for _, ls in picks for s in ls})
    outs, choices = run_engine(a.engine, a.dicts, all_lines, env)
    out_of = dict(zip(all_lines, outs))
    ch_of = dict(zip(all_lines, choices))

    stats = collections.Counter()
    items = []
    for r, ls in picks:
        exs = []
        skip = None
        for s in ls:
            toks = lexlib.analyze(s)
            starts = byte_starts(s, toks)
            k = next((i for i, t in enumerate(toks) if t.lid == r['lid']), None)
            if k is None or starts[k] < 0:
                continue
            b = starts[k]
            surf, orth = toks[k].surf, toks[k].orth

            def same(c):
                cs = c['surface']
                return cs == surf or surf.startswith(cs) or cs.startswith(surf) or orth.startswith(cs)
            # the engine's offsets are into its normalized line: take the matching choice nearest the offset
            near = sorted((c for c in ch_of[s] if same(c)), key=lambda c: abs(c['start'] - b))
            c = near[0] if near else None
            if c is None:
                inside = next((c for c in ch_of[s] if surf in c['surface']), None)
                skip = skip or ('in-longer-key' if inside else 'no-choice')
                continue
            if len(c['surface']) > len(toks[k].surf) + 1 and not c['surface'].startswith(toks[k].surf):
                skip = skip or 'longer-key'
                continue
            if len(c['surface']) > len(toks[k].surf) + 2:
                skip = skip or 'longer-key'
                continue
            prov = c['choice']['provenance']
            if prov in RULED:
                skip = 'rule'
                break
            ko, _, _, _ = lexlib.expand_ko(c['choice']['text'])
            exs.append({'ja': s, 'out': out_of[s], 'key': c['surface'], 'ko': ko, 'prov': prov,
                        'layer': c['choice']['layer'], 'dict': toks[k].orth, 'surf': toks[k].surf})
        if skip == 'rule':
            stats['skip:rule'] += 1
            continue
        if not exs:
            stats['skip:' + (skip or 'no-example')] += 1
            continue
        by_key = collections.OrderedDict()
        for e in exs:
            by_key.setdefault(e['key'], []).append(e)
        for key, kex in by_key.items():
            kos = collections.Counter(e['ko'] for e in kex)
            items.append({'lid': r['lid'], 'key': key, 'rank': r['rank'], 'pos': r['pos'], 'count': int(r['total']),
                          'ko': kos.most_common(1)[0][0], 'varies': len(kos) > 1,
                          'fixed': any(e['prov'] == 'Fixed' for e in kex), 'examples': kex})
            stats['item'] += 1
            stats['item:fixed-layer' if items[-1]['fixed'] else 'item:dictionary'] += 1
    json.dump(items, open(os.path.join(a.out, 'items.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    # packets
    for pi in range(0, len(items), a.size):
        part = items[pi:pi + a.size]
        no = pi // a.size + 1
        with open(os.path.join(a.out, f'p1-{no:03d}.txt'), 'w', encoding='utf-8') as o:
            for j, it in enumerate(part, pi + 1):
                ja, yomi, pos = it['lid'].split('/')
                o.write(f'### {j} | {it["key"]} (lemma {ja}, {yomi}, {pos}) | engine word: {it["ko"]}\n')
                for e in it['examples']:
                    o.write(f'- {e["ja"]}\n  → {e["out"]}   [{e["surf"]} = {e["ko"]}]\n')
                o.write('\n')
    print(dict(stats), 'lemmas in band', len(tail), 'packets', (len(items) + a.size - 1) // a.size)


if __name__ == '__main__':
    main()
