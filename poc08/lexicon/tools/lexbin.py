#!/usr/bin/env python3
"""Compile the lexicon sources into the binary the runtime loads (format: lexicon.h).

usage: lexbin.py [--lex DIR] [--out FILE] [--names-out FILE] [--ship] [--by-sa] [-j N]

--out gets the lexicon, phrases and classes; --names-out gets names and titles (a separate, optional pack).
--ship leaves out rows that may not ship: codes marked `no`, and `review` codes on st=u rows; --by-sa adds
the share-alike files (without it they are left out).
"""
import argparse
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexlib  # noqa: E402
from lexconv import read_tsv, analyze_all  # noqa: E402

MAGIC = b'HD8LEX\0\0'
VERSION = 1
POS = ['n', 'pn', 'num', 'suf', 'pref', 'v1', 'v5', 'vs', 'vk', 'vz', 'adj', 'na', 'adv', 'adn', 'conj', 'intj']
ST = {'r': 0, 'm': 1, 'u': 2}
KCLS = ['', 'REG', 'HA', 'D', 'B', 'BWA', 'S', 'H', 'REU', 'REO', 'U', 'EU', 'L', 'COP', 'EXIST']
KPOS = {'': 0, 'V': 1, 'A': 2}
SLOTS = ['subj', 'obj', 'dat', 'loc', 'with', 'from', 'to', 'arg', 'head', 'mod', 'pred', 'after', 'next', 'form',
         'domain', 'register', 'main.vol', 'ctx', 'subj-of', 'obj-of', 'dat-of', 'verb']
ALT_LEMMA, ALT_CLASS, ALT_ABSENT = 0, 1, 2
NAME_TYPES = ['family', 'given', 'person', 'place', 'org', 'title', 'other', 'ship', 'work']
CONF = {'high': 0, 'med': 1, 'low': 2}
ENTRY_LU = 1


class Strings:
    def __init__(self):
        self.buf = bytearray(b'\0')
        self.idx = {'': 0}

    def add(self, s):
        if s not in self.idx:
            self.idx[s] = len(self.buf)
            self.buf += s.encode('utf-8') + b'\0'
        return self.idx[s]


def ship_ok(prov, st, ship, codes):
    if not ship:
        return True
    for c in prov.split('+'):
        v = codes.get(c.rstrip('?'), 'no')
        if v == 'no' or (v == 'review' and st == 'u') or v == 'row':
            if v == 'row':
                continue
            return False
    return True


def parse_cue(cue):
    atoms = []
    for atom in cue.split():
        if '=' not in atom:
            continue
        slot, val = atom.split('=', 1)
        if slot not in SLOTS:
            continue
        alts = []
        for v in val.split('|'):
            if v == '-':
                alts.append((ALT_ABSENT, ''))
            elif v.startswith('@'):
                alts.append((ALT_CLASS, v))
            else:
                alts.append((ALT_LEMMA, v))
        atoms.append((SLOTS.index(slot), alts))
    return atoms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lex', default=lexlib.LEX)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(lexlib.LEX), 'build', 'lexicon.hdlx'))
    ap.add_argument('--names-out', default=os.path.join(os.path.dirname(lexlib.LEX), 'build', 'names.hdlx'))
    ap.add_argument('--ship', action='store_true')
    ap.add_argument('--by-sa', action='store_true')
    ap.add_argument('-j', type=int, default=min(16, os.cpu_count() or 1))
    ap.add_argument('--no-analysis', action='store_true', help='phrase lemma sequences are the pattern text')
    a = ap.parse_args()
    codes = {r['code']: r['ship'] for r in read_tsv(os.path.join(a.lex, 'provenance.tsv'))}
    stems = {r['lemma']: r for r in read_tsv(os.path.join(a.lex, 'stems.tsv'))}
    classes = [(r['class'], r['parent']) for r in read_tsv(os.path.join(a.lex, 'classes.tsv'))]
    cls_index = {c: i for i, (c, _) in enumerate(classes)}

    lex_files = sorted(glob.glob(os.path.join(a.lex, 'lexicon', '*.tsv')))
    ph_files = sorted(glob.glob(os.path.join(a.lex, 'phrases*.tsv')))
    if a.by_sa:
        lex_files += sorted(glob.glob(os.path.join(a.lex, 'by-sa', 'lexicon*.tsv')))
        ph_files += sorted(glob.glob(os.path.join(a.lex, 'by-sa', 'phrases*.tsv')))
    S = Strings()
    entries = {}
    dropped = 0
    for f in lex_files:
        for r in read_tsv(f):
            if not ship_ok(r['prov'], r['st'], a.ship, codes):
                dropped += 1
                continue
            lid = (r['ja'], r['yomi'], r['pos'])
            entries.setdefault(lid, []).append(r)

    E, SN, AT, AL = [], [], [], []

    def add_atoms(cue):
        first = len(AT)
        for slot, alts in parse_cue(cue):
            fa = len(AL)
            for kind, v in alts:
                AL.append(struct.pack('<BBHI', kind, 0, 0, S.add(v)))
            AT.append(struct.pack('<BBHI', slot, 0, len(alts), fa))
        return first, len(AT) - first

    keyed = sorted(entries.items(), key=lambda kv: '\x1f'.join(kv[0]).encode('utf-8'))
    for (ja, yomi, pos), rs in keyed:
        rs.sort(key=lambda r: int(r['sense']))
        if pos not in POS:
            continue
        feat = rs[0]['feat']
        flags = ENTRY_LU if 'lu' in feat.split() else 0
        feat = ' '.join(x for x in feat.split() if x != 'lu')
        first = len(SN)
        for r in rs:
            ko = r['ko']
            lw = lexlib.last_word(ko)
            kcls, kpos = '', ''
            if lw.split(':')[0].endswith('다'):
                st = stems.get(lw.split(':')[0])
                if ':' in lw:
                    kcls = lexlib.guess_class(lw, None)[0] or ''
                elif st:
                    kcls, kpos = st['class'], st['kpos']
                else:
                    kcls = lexlib.guess_class(lw, 'V' if pos.startswith('v') else 'A')[0] or ''
                kpos = kpos or ('V' if pos.startswith('v') else 'A')
            fa, na = add_atoms(r['cue'])
            SN.append(struct.pack('<IBBBBIHHII', S.add(ko), KCLS.index(kcls) if kcls in KCLS else 0, KPOS.get(kpos, 0),
                                  ST.get(r['st'], 2), 0, fa, na, int(r['sense']), S.add(r['frame']), S.add(r['prov'])))
        E.append(struct.pack('<IIBBBBIHHI', S.add(ja), S.add(yomi), POS.index(pos), ST.get(rs[0]['st'], 2), flags, 0,
                             first, len(rs), 0, S.add(feat)))
    by_ja = sorted(range(len(keyed)), key=lambda i: (keyed[i][0][0].encode('utf-8'), i))

    # phrases: lemma sequence of the pattern, slots kept as {X…}
    prows = []
    for f in ph_files:
        for r in read_tsv(f):
            if ship_ok(r['prov'], r['st'], a.ship, codes):
                prows.append(r)
    pieces = set()
    for r in prows:
        for part in re.split(r'\{[^}]*\}', r['pattern']):
            if part:
                pieces.add(part)
    an = {} if a.no_analysis else analyze_all(list(pieces), a.j)

    def lemma_seq(pattern):
        out = []
        for part in re.split(r'(\{[^}]*\})', pattern):
            if not part:
                continue
            if part.startswith('{'):
                out.append(part)
            else:
                toks = an.get(part)
                out += [t[1] for t in toks if t[4] not in ('補助記号', '記号')] if toks is not None else [part]
        return ' '.join(out)
    P = []
    pkeyed = []
    for r in prows:
        seq = lemma_seq(r['pattern'])
        pkeyed.append((seq.split(' ')[0] if seq else '', seq, r))
    pkeyed.sort(key=lambda x: (x[0].encode('utf-8'), x[1].encode('utf-8')))
    for head, seq, r in pkeyed:
        fa, na = add_atoms(r['cue'])
        P.append(struct.pack('<IIIIIHBBI', S.add(r['id']), S.add(r['pattern']), S.add(seq), S.add(r['ko']), fa, na,
                             ST.get(r['st'], 2), 0, S.add(head)))
    C = [struct.pack('<Ii', S.add(c), cls_index.get(p, -1)) for c, p in classes]

    write(a.out, S, E, SN, AT, AL, P, [], C, by_ja, [])

    # names pack
    N = []
    S2 = Strings()
    nfiles = sorted(glob.glob(os.path.join(a.lex, 'names', '*.tsv'))) + [os.path.join(a.lex, 'titles.tsv')]
    if a.by_sa:
        nfiles += sorted(glob.glob(os.path.join(a.lex, 'by-sa', 'names-*.tsv')))
    nrows = []
    for f in nfiles:
        for r in read_tsv(f):
            if ship_ok(r['prov'], 'u', a.ship, codes) and r['type'] in NAME_TYPES:
                nrows.append(r)
    nrows.sort(key=lambda r: (r['ja'].encode('utf-8'), r['type']))
    for r in nrows:
        N.append(struct.pack('<IIIIBBH', S2.add(r['ja']), S2.add(r['yomi']), S2.add(r['ko']), S2.add(r['needs']),
                             NAME_TYPES.index(r['type']), CONF.get(r['conf'], 1), 0))
    write(a.names_out, S2, [], [], [], [], [], N, [], [], [])
    print(f'entries {len(E)} senses {len(SN)} atoms {len(AT)} alts {len(AL)} phrases {len(P)} classes {len(C)} '
          f'names {len(N)} dropped-for-ship {dropped}')
    for p in (a.out, a.names_out):
        print(p, os.path.getsize(p))


def write(path, S, E, SN, AT, AL, P, N, C, by_ja, _):
    """Header, then tables in a fixed order, each 4-byte aligned."""
    tables = [b''.join(E), b''.join(SN), b''.join(AT), b''.join(AL), b''.join(P), b''.join(N), b''.join(C),
              struct.pack(f'<{len(by_ja)}I', *by_ja), bytes(S.buf)]
    counts = [len(E), len(SN), len(AT), len(AL), len(P), len(N), len(C), len(by_ja), len(S.buf)]
    hdr_size = 8 + 4 + 4 + len(tables) * 8
    offs = []
    pos = hdr_size
    for t in tables:
        pos = (pos + 3) & ~3
        offs.append(pos)
        pos += len(t)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as o:
        o.write(MAGIC + struct.pack('<II', VERSION, len(tables)))
        for off, n in zip(offs, counts):
            o.write(struct.pack('<II', off, n))
        cur = hdr_size
        for off, t in zip(offs, tables):
            o.write(b'\0' * (off - cur))
            o.write(t)
            cur = off + len(t)
        o.write(b'\0' * ((4 - cur % 4) % 4))


if __name__ == '__main__':
    main()
