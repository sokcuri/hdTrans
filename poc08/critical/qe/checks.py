"""The QE checks as rows (data/check.tsv): each row names the Japanese clause features that ask for something and
the Korean sentence facts that realise it. This module only reads the rows and applies them; what is checked lives
in the table, with its spec id and cases (data/check.cases.tsv)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TABLE = os.path.join(HERE, '..', 'data', 'check.tsv')

CLAUSE_FEATS = {'pol', 'negFn', 'force', 'voice', 'bene', 'polite', 'link', 'linkSub', 'mod'}
CLAUSE_FLAGS = {'rare', 'qmark', 'final'}
KO_ATOMS = {'neg', 'pot_neg', 'pot', 'jana', 'oblig', 'prohib', 'q', 'hon', 'calque', 'plain_neg', 'neg1', 'before',
            'numbers', 'names'}
SCOPES = {'unit', 'last', 'line'}


class Row:
    __slots__ = ('id', 'spec', 'check', 'scope', 'when', 'unless', 'ko')


def _atoms(s, sep):
    return [] if s.strip() in ('', '-') else [a.strip() for a in s.split(sep)]


def _clause_atom(a):
    neg = a.startswith('!')
    a = a[1:] if neg else a
    if not a.startswith('c.'):
        raise ValueError(f'clause atom {a}: needs c.')
    a = a[2:]
    for op in ('!=', '='):
        if op in a:
            f, v = a.split(op, 1)
            if f not in CLAUSE_FEATS:
                raise ValueError(f'unknown clause feature {f}')
            return (f, op, set(v.split('|')), neg)
    if a not in CLAUSE_FLAGS:
        raise ValueError(f'unknown clause flag {a}')
    return (a, 'flag', None, neg)


def _unless_atom(a):
    if not a.startswith('any.') or '=' not in a:
        raise ValueError(f'unless atom {a}: needs any.feat=v')
    f, v = a[4:].split('=', 1)
    if f not in CLAUSE_FEATS:
        raise ValueError(f'unknown clause feature {f}')
    return (f, v)


def _ko_atom(a):
    neg = a.startswith('!')
    a = a[1:] if neg else a
    if a not in KO_ATOMS:
        raise ValueError(f'unknown Korean atom {a}')
    return (a, neg)


def load(path=TABLE):
    rows, ids = [], set()
    cols = None
    for n, line in enumerate(open(path, encoding='utf-8'), 1):
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        c = line.split('\t')
        if cols is None:
            cols = c
            continue
        d = dict(zip(cols, c))
        try:
            r = Row()
            r.id, r.spec, r.check, r.scope = d['id'], d['spec'], d['check'], d['scope']
            if not r.id or r.id in ids:
                raise ValueError('missing or repeated id')
            if not r.spec.startswith('CHK-'):
                raise ValueError('spec id must be CHK-n')
            if r.scope not in SCOPES:
                raise ValueError(f'unknown scope {r.scope}')
            ids.add(r.id)
            r.when = [_clause_atom(a) for a in _atoms(d['when'], '&')]
            r.unless = [_unless_atom(a) for a in _atoms(d['unless'], '|')]
            r.ko = [_ko_atom(a) for a in _atoms(d['ko'], '|')]
            if not r.ko:
                raise ValueError('empty ko')
            if (r.scope == 'line') != any(a in ('numbers', 'names') for a, _ in r.ko):
                raise ValueError('numbers and names are the atoms of line rows, and only of them')
            if r.scope == 'line' and (r.when or any(neg for _, neg in r.ko)):
                raise ValueError('a line row has no when and no negated atom')
        except (ValueError, KeyError) as e:
            raise SystemExit(f'{path}:{n}: {e}')
        rows.append(r)
    return rows


def _clause_value(c, f):
    if f == 'mod':
        return set(c.get('modality', []))
    return {c.get(f)}


def _flag(c, f):
    if f == 'final':
        return c.get('link', 'final') == 'final'
    return bool(c.get(f))


def clause_matches(r, c):
    for f, op, vals, neg in r.when:
        if op == 'flag':
            ok = _flag(c, f)
        elif op == '=':
            ok = bool(_clause_value(c, f) & vals)
        else:
            ok = not (_clause_value(c, f) & vals)
        if ok == neg:
            return False
    return True


def ko_facts(k, fin):
    negs = len(k['neg'])
    return {'neg': negs > 0, 'pot_neg': k['pot_neg'], 'pot': k['pot_pos'] or k['pot_neg'], 'jana': k['jana'],
            'oblig': k['oblig'], 'prohib': k['prohib'], 'q': fin['q'], 'hon': k['hon'], 'calque': k['calque'],
            'plain_neg': negs > 0 and not k['jana'] and not fin['q'], 'neg1': negs == 1, 'before': k.get('before', False)}


def realised(r, facts):
    return any(facts[a] != neg for a, neg in r.ko)


def check(rows, exp, kos, units, names_present):
    """Violated (check, clause index, row id) of one line. units(exp, kos) cuts the line into aligned units."""
    v = []
    if exp is None:
        return v
    for clauses, ks in units(exp, kos):
        k = ks[0]
        fin = k.get('last', k)
        facts = ko_facts(k, fin)
        merged = 'last' in k
        # per clause and spec id, the most specific matching row decides (most when atoms; ties all apply)
        for c in clauses:
            best = {}
            for r in rows:
                if r.scope == 'line' or not clause_matches(r, c):
                    continue
                if any(any(c2.get(f) == val or (f == 'mod' and val in c2.get('modality', [])) for c2 in clauses)
                       for f, val in r.unless):
                    continue
                if r.scope == 'last' and merged and c['sent'] != len(exp['sents']) - 1:
                    continue
                cur = best.get(r.spec)
                if cur is None or len(r.when) > len(cur[0].when):
                    best[r.spec] = [r]
                elif len(r.when) == len(cur[0].when):
                    cur.append(r)
            for rs in best.values():
                for r in rs:
                    if not realised(r, facts):
                        v.append((r.check, c['idx'], r.id))
    text = ' '.join(k['text'] for k in kos)
    kvals = set(x for k in kos for x in k['numbers'])
    for r in rows:
        if r.scope != 'line':
            continue
        for a, _neg in r.ko:
            if a == 'numbers' and any(n not in kvals for n in exp['numbers']):
                v.append((r.check, -1, r.id))
            if a == 'names' and any(not names_present(nm, text) for nm in exp['names']):
                v.append((r.check, -1, r.id))
    return sorted(set(v))
