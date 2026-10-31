"""Reference semantics of sense choice by cue rules (DESIGN-v3 §5), shared by the tools and the tests.

A sense applies when every atom of its cue holds. Among the senses that apply, the most specific wins; when none
applies, sense 1. Specificity is a tuple compared left to right:
  1. atoms satisfied by a lemma value (written lemma, not a class);
  2. the summed depth of the classes that satisfied class atoms (a deeper class is more specific);
  3. atoms over forms and neighbours (form, next, after, mod, pred, main.vol);
  4. atoms over domain and register;
  5. the number of atoms.
Equal tuples fall to the lower sense number, recorded as a default (margin 0). There are no weights.

The context is a plain object with:
  args: dict slot -> list of noun lemmas (subj obj dat loc with from to; 'arg' is any of them);
  head: noun lemma or None; mods, nexts, afters, preds: lists of lemmas; forms: set of form names;
  domain, register: strings or ''; main_vol: '+', '-' or ''.
"""
import collections

LEMMA, CLASS, NEIGH, REG = range(4)
ARG_SLOTS = ('subj', 'obj', 'dat', 'loc', 'with', 'from', 'to')


class Classes:
    """classes.tsv parents and the membership given by the lexicon's cls features."""

    def __init__(self, parents, member):
        self.parent = parents  # class -> parent ('' for roots)
        self.member = member   # noun lemma -> set of its own classes

    def depth(self, c):
        d, seen = 0, set()
        while c and c not in seen:
            seen.add(c)
            c = self.parent.get(c, '')
            d += 1
        return d

    def classes_of(self, lemma):
        out = set()
        for c in self.member.get(lemma, ()):
            while c and c not in out:
                out.add(c)
                c = self.parent.get(c, '')
        return out


def parse_cue(cue):
    out = []
    for atom in cue.split():
        slot, _, val = atom.partition('=')
        out.append((slot, val.split('|')))
    return out


def _match_nouns(nouns, vals, C):
    """Best (kind, depth) over the nouns, or None."""
    best = None
    for n in nouns:
        cl = C.classes_of(n)
        for v in vals:
            if v.startswith('@'):
                if v in cl:
                    k = (CLASS, C.depth(v))
                    if best is None or best[0] != LEMMA and k[1] > best[1]:
                        best = k
            elif v == n:
                return (LEMMA, 0)
    return best


def atom(ctx, slot, vals, C):
    """(kind, depth) when the atom holds, else None."""
    if slot in ARG_SLOTS or slot == 'arg':
        nouns = [n for s in ARG_SLOTS for n in ctx.args.get(s, [])] if slot == 'arg' else ctx.args.get(slot, [])
        if vals == ['-']:
            return (NEIGH, 0) if not nouns else None
        return _match_nouns(nouns, vals, C)
    if slot == 'head':
        if vals == ['-']:
            return (NEIGH, 0) if not ctx.head else None
        return _match_nouns([ctx.head] if ctx.head else [], vals, C)
    if slot in ('mod', 'next', 'after', 'pred'):
        pool = {'mod': ctx.mods, 'next': ctx.nexts, 'after': ctx.afters, 'pred': ctx.preds}[slot]
        if vals == ['-']:
            return (NEIGH, 0) if not pool else None
        return (NEIGH, 0) if _match_nouns(pool, vals, C) else None
    if slot == 'form':
        return (NEIGH, 0) if any(v in ctx.forms for v in vals) else None
    if slot == 'main.vol':
        return (NEIGH, 0) if ctx.main_vol in vals else None
    if slot == 'register':
        return (REG, 0) if ctx.register in vals else None
    if slot == 'domain':
        return (REG, 0) if ctx.domain in vals else None
    return None


def specificity(ctx, cue, C):
    """Specificity tuple when every atom holds, else None. An empty cue holds with the lowest tuple."""
    t = [0, 0, 0, 0, 0]
    for slot, vals in parse_cue(cue):
        m = atom(ctx, slot, vals, C)
        if m is None:
            return None
        kind, depth = m
        if kind == LEMMA:
            t[0] += 1
        elif kind == CLASS:
            t[1] += depth
        elif kind == NEIGH:
            t[2] += 1
        else:
            t[3] += 1
        t[4] += 1
    return tuple(t)


def choose(senses, ctx, C):
    """senses: list of (sense number, ko, cue) in order. Returns (sense number, ko, why)."""
    best, why = None, 'default'
    for no, ko, cue in senses:
        if not cue:
            continue
        sp = specificity(ctx, cue, C)
        if sp is not None and (best is None or sp > best[0]):
            best, why = (sp, no, ko), cue
    if best is None:
        no, ko, _ = senses[0]
        return no, ko, 'default'
    return best[1], best[2], why


def load(lexdir, read_tsv):
    """Classes and membership from a lex/ directory; read_tsv is lexconv.read_tsv."""
    import glob
    import os
    parents = {r['class']: r['parent'] for r in read_tsv(os.path.join(lexdir, 'classes.tsv'))}
    member = collections.defaultdict(set)
    senses = collections.defaultdict(list)
    for f in sorted(glob.glob(os.path.join(lexdir, 'lexicon', '*.tsv'))):
        for r in read_tsv(f):
            if r['sense'] == '1':
                for x in r['feat'].split():
                    if x.startswith('cls='):
                        member[r['ja']].update(v for v in x[4:].split('|') if v in parents)
            senses[(r['ja'], r['yomi'], r['pos'])].append((int(r['sense']), r['ko'], r['cue'], r['st']))
    for k in senses:
        senses[k].sort()
    return Classes(parents, member), senses
