"""aux.tsv reader and matcher, the same rules as poc08/structure/aux_table.cpp.

A token gets the most specific matching row: after +8, surf +4, form +2, ctype +2, a named lemma +2, then the
longer ud prefix, then file order. Form rows (kind=form) describe the token's own inflection and are matched
separately (FormRow).
"""

LEMMA_OF = {}


def load(path):
    rows = []
    hdr = None
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            if line.startswith('#') or not line.strip():
                continue
            p = line.rstrip('\n').split('\t')
            if hdr is None:
                hdr = p
                continue
            r = dict(zip(hdr, p + [''] * (len(hdr) - len(p))))
            alts = lambda v: [] if v in ('*', '') else v.split('|')  # noqa: E731
            r['lemmas'] = alts(r['match'])
            r['surfs'] = alts(r['surf'])
            r['uds'] = alts(r['ud'])
            r['forms'] = alts(r['form'])
            r['ctypes'] = alts(r['ctype'])
            r['afters'] = [a.partition('=')[::2] for a in alts(r['after'])]
            r['roleset'] = ''
            for kv in r['sets'].split(';'):
                if kv.startswith('role='):
                    r['roleset'] = kv[5:]
            r['spec'] = (8 if r['afters'] else 0) + (4 if r['surfs'] else 0) + (2 if r['forms'] else 0) + \
                (2 if r['ctypes'] else 0) + (2 if r['lemmas'] else 0) + max([len(u) for u in r['uds']] or [0]) / 100
            r['order'] = len(rows)
            rows.append(r)
    return rows


def _lemma_ok(r, lemma):
    if not r['lemmas']:
        return True
    for x in r['lemmas']:
        if x.startswith('~'):
            if lemma.endswith(x[1:]):
                return True
        elif x == lemma:
            return True
    return False


def _surf_ok(r, surf, lemma):
    if not r['surfs']:
        return True
    for x in r['surfs']:
        if x.startswith('@stem+'):
            stem = lemma[:-1]
            if stem and surf.startswith(stem + x[6:]):
                return True
        elif x == surf:
            return True
    return False


def _after_ok(r, prev, prevrole):
    if not r['afters']:
        return True
    if prev is None:
        return False
    for k, v in r['afters']:
        if k == 'prev' and prev[1] == v:
            return True
        if k == 'prevsurf' and prev[0] == v:
            return True
        if k == 'prevud' and prev[2].startswith(v):
            return True
        if k == 'prevform' and prev[3].startswith(v):
            return True
        if k == 'prevrole' and prevrole == v:
            return True
    return False


def token_row(table, m, prev, prevrole, want_form=False):
    """m = (surface, lemma, ud, cform, ctype)."""
    best = None
    for r in table:
        if (r['kind'] == 'form') != want_form:
            continue
        if not _lemma_ok(r, m[1]) or not _surf_ok(r, m[0], m[1]):
            continue
        if r['uds'] and not any(m[2].startswith(u) for u in r['uds']):
            continue
        if r['forms'] and not any(m[3].startswith(f) for f in r['forms']):
            continue
        if r['ctypes'] and not any(m[4].startswith(c) for c in r['ctypes']):
            continue
        if not _after_ok(r, prev, prevrole):
            continue
        if best is None or r['spec'] > best['spec']:
            best = r
    return best


def roles(table, toks):
    """Row per token, left to right, so prevrole is defined; content tokens get their form row's role."""
    out = []
    prevrole = ''
    for i, t in enumerate(toks):
        prev = toks[i - 1] if i else None
        r = token_row(table, t, prev, prevrole)
        if r is None and t[3]:
            r = token_row(table, t, prev, prevrole, want_form=True)
        out.append(r)
        prevrole = r['roleset'] if r else ''
        # an inflected word with a voice row (potential verb) still passes its form role on
        if r is not None and r['kind'] != 'form' and t[3] and t[2].startswith(('動詞', '形容詞')) and r['kind'] in ('voice',):
            fr = token_row(table, t, prev, '', want_form=True)
            prevrole = fr['roleset'] if fr else prevrole
    return out


def match(table, m, a):
    toks = a['all']
    rs = roles(table, toks)
    return rs[toks.index(m)] is not None


def form_folds(table, cform):
    return any(r['kind'] == 'form' and (not r['forms'] or any(cform.startswith(f) for f in r['forms'])) for r in table)


def bare_form_folds(table, key):
    ud = {'V': '動詞', 'I': '形容詞', 'D': '助動詞'}[key]
    return any(r['kind'] == 'form' and (not r['uds'] or any(u.startswith(ud) or ud.startswith(u) for u in r['uds']))
               for r in table)
