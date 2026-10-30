#!/usr/bin/env python3
"""Compare C's bunsetsu and heads with GiNZA silver; write gold heads for training.

usage: align_heads.py IR.jsonl SILVER.jsonl [--gold OUT.jsonl] [--errors N]

Alignment is by content token: a silver bunsetsu maps to the bunsetsu of ours that holds its head token.
Ours is aligned when a silver bunsetsu maps to it and our content token lies inside that silver bunsetsu.
Head accuracy counts every aligned, non-root bunsetsu of ours over all sentences (no sentence is skipped).
Chunk agreement: silver bunsetsu (punctuation trimmed) whose span equals one of ours.
"""
import json
import sys
import unicodedata


def trim(src_b, b, e):
    s = src_b[b:e].decode('utf-8', 'replace')
    lead = 0
    while lead < len(s) and unicodedata.category(s[lead])[0] in 'PZS':
        lead += 1
    trail = len(s)
    while trail > lead and unicodedata.category(s[trail - 1])[0] in 'PZS':
        trail -= 1
    if lead >= trail:
        return None
    return b + len(s[:lead].encode()), b + len(s[:trail].encode())


def main():
    irp, silp = sys.argv[1], sys.argv[2]
    goldp = sys.argv[sys.argv.index('--gold') + 1] if '--gold' in sys.argv else None
    nerr = int(sys.argv[sys.argv.index('--errors') + 1]) if '--errors' in sys.argv else 0
    sil = {}
    for l in open(silp, encoding='utf-8'):
        o = json.loads(l)
        sil[(o['doc'], o['line'])] = o
    gout = open(goldp, 'w', encoding='utf-8') if goldp else None
    tot_b = agree_b = 0
    ours_total = aligned = 0
    h_tot = h_ok = 0
    base_ok = 0
    h2_tot = h2_ok = 0
    errs = []
    for l in open(irp, encoding='utf-8'):
        o = json.loads(l)
        s = sil.get((o['doc'], o['line']))
        if s is None:
            continue
        src_b = o['src'].encode()
        buns = o['buns']
        # silver bunsetsu with punctuation-only ones removed (heads followed through them)
        sb, sh = [], []
        for sent in s['sents']:
            keep = [k for k, b in enumerate(sent['buns']) if trim(src_b, b[2], b[3]) is not None]
            pos = {k: i for i, k in enumerate(keep)}
            off = len(sb)

            def up(h, sent=sent, pos=pos):
                seen = 0
                while h != -1 and h not in pos and seen < 100:
                    h = sent['heads'][h]
                    seen += 1
                return pos.get(h, -1)
            for k in keep:
                sb.append(sent['buns'][k])
                h = up(sent['heads'][k])
                sh.append(-1 if h == -1 else h + off)
        # chunk agreement
        ours_spans = set()
        for b in buns:
            t = trim(src_b, b['src'][0], b['src'][1])
            if t:
                ours_spans.add(t)
        for b in sb:
            t = trim(src_b, b[0], b[1])
            if t:
                tot_b += 1
                agree_b += t in ours_spans
        # map silver bunsetsu -> ours by head token
        def ours_of(byte):
            for i, b in enumerate(buns):
                if b['src'][0] <= byte < b['src'][1]:
                    return i
            return -1
        s2o = [ours_of(b[2]) for b in sb]
        o2s = {}
        for k, i in enumerate(s2o):
            if i < 0:
                continue
            b = sb[k]
            c = buns[i]['contentSrc']
            if b[0] <= c[0] < b[1]:
                o2s.setdefault(i, k)
        gold = []
        for i, b in enumerate(buns):
            ours_total += 1
            k = o2s.get(i)
            if k is None:
                gold.append(-2)
                continue
            aligned += 1
            g = -1 if sh[k] == -1 else s2o[sh[k]]
            gold.append(g if g != i else -2)
            if g == -1 or g == i:
                continue
            if g < 0:
                continue
            h_tot += 1
            ok = b['head'] == g
            h_ok += ok
            base_ok += g == i + 1
            if b['head2'] >= 0:
                h2_tot += 1
                h2_ok += g in (b['head'], b['head2'])
            if not ok and len(errs) < nerr:
                errs.append(' '.join(('[' + x['text'] + ']') if j == i else ('<G>' + x['text'] if j == g else (
                    '<R>' + x['text'] if j == b['head'] else x['text'])) for j, x in enumerate(buns)))
        if gout:
            gout.write(json.dumps({'doc': o['doc'], 'line': o['line'], 'heads': gold}, ensure_ascii=False) + '\n')
    print(f'chunk boundary agreement {agree_b / max(tot_b, 1):.4f} ({agree_b}/{tot_b})')
    print(f'aligned bunsetsu {aligned / max(ours_total, 1):.4f} ({aligned}/{ours_total})')
    print(f'head accuracy {h_ok / max(h_tot, 1):.4f} ({h_ok}/{h_tot}); next-bunsetsu baseline {base_ok / max(h_tot, 1):.4f}')
    if h2_tot:
        print(f'head2 present {h2_tot}; gold in head/head2 {h2_ok / h2_tot:.4f}')
    for e in errs:
        print(e)


if __name__ == '__main__':
    main()
