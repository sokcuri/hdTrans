#!/usr/bin/env python3
"""Coverage of judged faults: does the matcher pick the gold construction variant at the fault span?

  coverage.py FAULTS.tsv LABELS.tsv CX.jsonl [--impl IDS.txt] [--rounds r00,r01] [--list]

FAULTS.tsv  fid, round, file, line, ja, out, verdict, span, expected, layer, fix (one judged fault row)
LABELS.tsv  fid, primary, also, conf, note (blind labels: an inventory id or PHRASE/SENSE/…)
CX.jsonl    hd8xfer output for the same rows, in the same order ("case" = fid)
--impl      the ids the table implements (one per line); rows whose gold is outside are "not implemented"

A row is right when a winning construction that overlaps the fault span (±3 characters) has the gold id,
a sub-variant of it (gold link.tame.cause, winner link.tame.cause.x) or an equivalent id (EQUIV below);
"family" when only the family matches; "miss" when no winner of the family is there.
"""
import json
import re
import sys
from collections import Counter, defaultdict

# Ids whose emitted Korean features are the same reading under another name.
EQUIV = {
    'link.kedo.bg': {'mod.noda.ndesu.ga'},
    'mod.noda.past.expl': {'mod.noda.narr'},
    'mod.noda.expl': {'mod.noda.narr'},
    'np.no.pronoun': {'np.no.nominal'},
    'case.nado': {'quote.nante'},
    'quote.nante': {'case.nado'},
    'pol.neg.potential': {'voice.potential'},
    'mod.ka.rhetorical': {'mod.darou.q', 'mod.mono.ka'},
    'np.no.compound': {'np.no.drop'},
    'asp.teita.past': {'asp.teiru'},
    'mod.ba.ii': {'link.ba.ii'},
    'link.ba.ii': {'mod.ba.ii'},
    'mod.nai.koto.wa.nai': {'mod.koto.wa.nai'},
    'case.de': {'case.de'},
    'mod.mitai.like': {'mod.youda'},
}
NON = {'PHRASE', 'SENSE', 'LEX', 'GEN', 'SEG', 'REG', 'OTHER', 'NORM'}


def family(i):
    return i.rsplit('.', 1)[0] if i.count('.') >= 2 else i


def matches(gold, got):
    if got == gold or got.startswith(gold + '.'):
        return True
    for e in EQUIV.get(gold, ()):
        if got == e or got.startswith(e + '.'):
            return True
    return False


def span_ja(span):
    s = re.split(r'\s*(?:→|=>|->|⇒)\s*', span)[0]
    s = re.split(r'[가-힣]', s)[0].strip()
    return s.strip(' /「」')


def main():
    a = sys.argv[1:]
    impl = None
    rounds = None
    listing = '--list' in a
    if '--impl' in a:
        impl = set(x.strip() for x in open(a[a.index('--impl') + 1]) if x.strip())
    if '--rounds' in a:
        rounds = set(a[a.index('--rounds') + 1].split(','))
    faults = {}
    for line in open(a[0], encoding='utf-8'):
        c = line.rstrip('\n').split('\t')
        if c[0] == 'fid' or len(c) < 10:
            continue
        faults[c[0]] = c
    labels = {}
    for line in open(a[1], encoding='utf-8'):
        c = line.rstrip('\n').split('\t')
        if c[0] == 'fid' or len(c) < 2:
            continue
        labels[c[0]] = c
    res = {}
    for line in open(a[2], encoding='utf-8'):
        o = json.loads(line)
        res[o['key']] = o
    stat = Counter()
    per = defaultdict(Counter)
    rows = []
    for fid, f in faults.items():
        if rounds and f[1] not in rounds:
            continue
        lab = labels.get(fid)
        if not lab:
            continue
        gold = lab[1].strip()
        stat['rows'] += 1
        if gold in NON:
            stat['non-cx'] += 1
            continue
        if gold.startswith('NEW'):
            stat['new'] += 1
            continue
        stat['cx'] += 1
        if impl is not None and not any(gold == i or i.startswith(gold + '.') or gold.startswith(i + '.') or
                                        any(i == e or i.startswith(e + '.') for e in EQUIV.get(gold, ()))
                                        for i in impl):
            stat['not-impl'] += 1
            per[gold]['not-impl'] += 1
            continue
        stat['impl'] += 1
        o = res.get(fid)
        src = f[4].strip()
        sj = span_ja(f[7])
        b = src.find(sj) if sj else -1
        if b >= 0:
            lo = len(src[:max(0, b - 3)].encode())
            hi = len(src[:b + len(sj) + 3].encode())
        else:
            lo, hi = 0, len(src.encode())
            stat['span-not-found'] += 1
        # offsets in the IR are into the stripped source line
        win = [c for c in (o['cx'] if o else []) if c['src'][0] < hi and c['src'][1] > lo]
        ids = [c['id'] for c in win]
        if any(matches(gold, i) for i in ids):
            v = 'right'
        elif any(family(i) == family(gold) or family(gold).startswith(family(i)) for i in ids):
            v = 'family'
        else:
            v = 'miss'
        stat[v] += 1
        per[gold][v] += 1
        rows.append((fid, gold, v, ' '.join(ids), sj, src[:60]))
    n = stat['impl']
    print('rows', stat['rows'], 'non-construction', stat['non-cx'], 'new (not in inventory)', stat['new'],
          'inventory', stat['cx'], 'not implemented', stat['not-impl'], 'implemented', n)
    if n:
        print('right %d (%.1f%%)  family-only %d (%.1f%%)  miss %d (%.1f%%)  span-not-found %d' % (
            stat['right'], 100 * stat['right'] / n, stat['family'], 100 * stat['family'] / n, stat['miss'],
            100 * stat['miss'] / n, stat['span-not-found']))
    if listing:
        for g, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values())):
            print('%-28s right %3d family %3d miss %3d not-impl %3d' % (g, c['right'], c['family'], c['miss'], c['not-impl']))
        for r in rows:
            if r[2] != 'right':
                print('\t'.join(r))


if __name__ == '__main__':
    main()
