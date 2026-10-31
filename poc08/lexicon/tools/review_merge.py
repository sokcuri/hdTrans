#!/usr/bin/env python3
"""Merge review outputs (JSON Lines) into lex/lexicon/top.tsv and lex/phrases-top.tsv (st=m).

usage: review_merge.py --review DIR [--lex DIR] [--effort FILE] [--draft FILE...]

DIR holds out-*.jsonl and ids.tsv. The merge normalizes the Korean, the cue syntax and the frames, drops
later senses without a cue and unknown slots or classes, and writes one effort row per lemma to --effort.
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
from lexconv import LEXCOLS, PHCOLS, read_tsv, write_tsv  # noqa: E402
from lexcheck import SLOTS, FORMS  # noqa: E402

FEAT = re.compile(r'^(vol=[+-]|asp=(state|activity|change|instant)|anim=\+|cls=@[\w.]+(\|@[\w.]+)*)$')
KO_TAG = re.compile(r':(ㄷ|ㅂ|ㅂ와|ㅅ|ㅎ|르|러|우|으|ㄹ)$')


def norm_ko(ko):
    ko = re.sub(r'\s+', ' ', (ko or '').strip())
    ko = ko.split('/')[0].strip()
    ko = re.sub(r'\s*[(（].*?[)）]\s*', ' ', ko).strip()
    if not ko or re.search(r'[぀-ヿ一-鿿@&$%{}<>\[\]]', ko):
        return None
    return ko


def norm_cue(cue, classes, log):
    atoms = []
    for raw in (cue or '').replace('，', ' ').replace(',', ' ').split():
        # a|dat=b written as one atom: split at each |slot=
        parts = re.split(r'\|(?=[a-z.\-]+=)', raw)
        for p in parts:
            if '=' not in p:
                log['cue-bad-atom'] += 1
                continue
            slot, val = p.split('=', 1)
            if slot not in SLOTS:
                log['cue-unknown-slot'] += 1
                continue
            vals = []
            for v in val.split('|'):
                v = v.strip()
                if not v:
                    continue
                if v.startswith('@') and v not in classes:
                    log['cue-unknown-class'] += 1
                    continue
                if slot == 'form' and v not in FORMS:
                    log['cue-unknown-form'] += 1
                    continue
                vals.append(v)
            if vals:
                atoms.append(f'{slot}={"|".join(dict.fromkeys(vals))}')
    return ' '.join(atoms)


def norm_frame(fr):
    if not fr:
        return ''
    if isinstance(fr, dict):
        return ' '.join(f'{k}={v}' for k, v in fr.items())
    if isinstance(fr, list):
        return ' '.join(str(x) for x in fr)
    return ' '.join(str(fr).replace(',', ' ').split())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--review', required=True)
    ap.add_argument('--lex', default=lexlib.LEX)
    ap.add_argument('--effort', default='')
    ap.add_argument('--draft', action='append', default=[])
    a = ap.parse_args()
    classes = {r['class']: r for r in read_tsv(os.path.join(a.lex, 'classes.tsv'))}
    order = [r['lid'] for r in read_tsv(os.path.join(a.review, 'ids.tsv'))]
    draft = collections.defaultdict(list)
    for f in sorted(glob.glob(os.path.join(a.lex, 'lexicon', 'gen-*.tsv'))) + [os.path.join(a.lex, 'by-sa', 'lexicon.tsv')]:
        if os.path.isfile(f):
            for r in read_tsv(f):
                draft[lexlib.lemma_id(r['ja'], r['yomi'], r['pos'])].append(r)
    hand = collections.defaultdict(list)
    for f in a.draft:
        for r in read_tsv(f):
            hand[lexlib.lemma_id(r['ja'], r['yomi'], r['pos'])].append(r)
    for lid, rs in hand.items():
        draft[lid] = rs
    got = {}
    newclasses = {}
    for f in sorted(glob.glob(os.path.join(a.review, 'out-*.jsonl'))):
        pkt = int(re.search(r'out-(\d+)', f).group(1))
        for line in open(f, encoding='utf-8'):
            if line.strip():
                x = json.loads(line)
                x['_packet'] = pkt
                got[x['lid']] = x
                for c, v in (x.get('newclass') or {}).items():
                    newclasses.setdefault(c, v)
    # new classes: parent first in the value, then examples
    for c, v in newclasses.items():
        if not c.startswith('@') or c in classes:
            continue
        parts = [p.strip() for p in re.split(r'[,;]', str(v)) if p.strip()]
        parent = parts[0] if parts and parts[0] in classes else '@entity'
        classes[c] = {'class': c, 'parent': parent, 'codes': '', 'members (examples)': ', '.join(parts[1:] if parts and parts[0].startswith('@') else parts)}
    log = collections.Counter()
    rows, prow, effort = [], [], []
    ph_seen = collections.Counter()
    for lid in order:
        x = got.get(lid)
        if not x:
            log['missing-review'] += 1
            continue
        ja, yomi, pos = lid.split('/')
        d = draft.get(lid, [])
        dko = {r['ko']: r.get('prov') or 'r' for r in d}
        out = []
        seen = set()
        for i, s in enumerate(x.get('senses') or []):
            ko = norm_ko(s.get('ko'))
            if not ko:
                log['sense-bad-ko'] += 1
                continue
            cue = norm_cue(s.get('cue', ''), classes, log) if out else ''
            if out and not cue:
                log['sense-uncued-dropped'] += 1
                continue
            if (ko, cue) in seen:
                log['sense-duplicate'] += 1
                continue
            seen.add((ko, cue))
            if pos in ('adj', 'na') and not lexlib.last_word(ko).endswith('다') and 'form=' not in cue and out:
                cue = (cue + ' form=adv').strip()
            out.append((ko, cue, norm_frame(s.get('frame')), (s.get('ex') or '').replace('\t', ' ')))
        feats = [f for f in str(x.get('feat') or '').split() if FEAT.match(f)]
        feats = [f for f in feats if not f.startswith('cls=') or all(c in classes for c in f[4:].split('|'))]
        for n, (ko, cue, frame, ex) in enumerate(out, 1):
            prov = dko.get(ko, 'r')
            rows.append([ja, yomi, pos, str(n), ko, cue, frame, ' '.join(feats) if n == 1 else '', 'm', prov, ex])
        nph = 0
        for p in x.get('phrases') or []:
            pat = (p.get('pattern') or '').strip()
            ko = (p.get('ko') or '').strip()
            if not pat or not ko or re.search(r'[぀-ヿ一-鿿]', ko):
                log['phrase-bad'] += 1
                continue
            nph += 1
            ph_seen[pat] += 1
            prow.append([f'{pat}#r{ph_seen[pat]}', pat, ko, norm_cue(p.get('cue', ''), classes, log), 'm', 'r', lid])
        dflt = d[0]['ko'] if d else ''
        effort.append([lid, str(x['_packet']), x.get('effort', ''), str(len({r['ko'] for r in d})), str(len(out)),
                       out[0][0] if out else '', dflt, '1' if out and dflt == out[0][0] else '0',
                       str(len(x.get('removed') or [])), str(sum(1 for o in out if o[1])), str(nph)])
        if not out:
            log['no-senses'] += 1
    write_tsv(os.path.join(a.lex, 'lexicon', 'top.tsv'), 'lexicon',
              'Machine-judged senses of the most frequent lemmas (st=m); feat on sense 1 describes the lemma.', LEXCOLS, rows)
    write_tsv(os.path.join(a.lex, 'phrases-top.tsv'), 'phrases', 'Phrases found in the lexicon review (st=m).', PHCOLS, prow)
    cols = ['class', 'parent', 'codes', 'members (examples)']
    write_tsv(os.path.join(a.lex, 'classes.tsv'), 'classes',
              'Named semantic classes for cues (obj=@headwear) and noun features (cls=@person). codes: 0.7 meaning codes whose rows seed the class.',
              cols, [[c[k] for k in cols] for c in classes.values()])
    if a.effort:
        with open(a.effort, 'w', encoding='utf-8') as o:
            o.write('lid\tpacket\teffort\tdraft_senses\tfinal_senses\tdefault\tdraft_default\tdefault_kept\tremoved\tcued\tphrases\n')
            for e in effort:
                o.write('\t'.join(e) + '\n')
    print(f'lemmas {len(effort)} rows {len(rows)} phrases {len(prow)} new classes {len(newclasses)}')
    for k, v in sorted(log.items()):
        print(f'{k}\t{v}')


if __name__ == '__main__':
    main()
