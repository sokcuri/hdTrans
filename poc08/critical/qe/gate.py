#!/usr/bin/env python3
"""QE gate: does a Korean line realise what the Japanese analysis requires?

  gate.py IR.jsonl KO.txt [--json OUT.jsonl]    one verdict per line: the violated checks (empty = pass)

The Japanese side is the 0.8 IR (hdtrans08-ir --each --stages: W1 tokens, W2 clauses: polarity, negation
function, force, tense, voice, benefactive direction); the Korean side is Kiwi morphemes. Works on any
output (0.7.x, a retrofit, a neural n-best). Each violated check names what is missing.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ja  # noqa: E402
import ko  # noqa: E402
import names  # noqa: E402

CHECKS = ['neg-missing', 'pot-missing', 'prohib-missing', 'oblig-missing', 'rhet-as-neg', 'double-as-neg',
          'confq-as-stmt', 'ask-as-neg', 'expl-missing',
          'vol-as-neg', 'q-missing', 'imp-as-pot', 'pass-as-pot', 'pot-voice-missing', 'hon-missing',
          'morau-calque', 'num-missing', 'name-missing']


def units(exp, kos):
    """Triples (clauses, korean sentence facts, negation points) to check: sentence by sentence when the counts
    agree, else the whole line as one unit."""
    if len(exp['sents']) == len(kos):
        out = [([c for c in exp['clauses'] if c['sent'] == k], [kos[k]], [n for n in exp['negs'] if n['sent'] == k])
               for k in range(len(kos))]
        return out
    merged = dict(ko.features([]))
    for k in kos:
        for key, v in k.items():
            if isinstance(v, bool):
                merged[key] = merged.get(key, False) or v
            elif isinstance(v, list):
                merged[key] = merged.get(key, []) + v
    merged['text'] = ' '.join(k['text'] for k in kos)
    merged['last'] = kos[-1] if kos else merged
    return [(exp['clauses'], [merged], exp['negs'])]


ASK_EF = ('래', '래요', '까', '까요', '겠어', '겠어요', '겠나', '겠니', '자', '줘', '줘요', '주세요', '주시겠어요', '주겠니', '못해')


RISK = 0.6     # a negation is flagged when the readings the Korean realises hold less than 1 - RISK of the mass


def consistent(cls, k, fin, unit=()):
    """Is the Korean unit a realisation of this negation function? unit: the classes of the unit's other points."""
    negs = len(k['neg'])
    plain_neg = negs > 0 and not k['jana'] and not fin['q']
    if cls in ('PLAIN', 'NEGCOND'):
        return negs > 0 or k['pot_neg'] or k.get('before', False) or (k['oblig'] and 'OBLIG' in unit)
    if cls == 'POT':
        return k['pot_neg'] or k['oblig']
    if cls == 'PROHIB':
        return k['prohib']
    if cls == 'OBLIG':
        return k['oblig']
    if cls == 'RHET':
        return not plain_neg or k['prohib']
    if cls == 'CONFQ':
        return fin['q'] or k['jana'] or k.get('qemb', False)
    if cls == 'ASK':
        return any(e.endswith(ASK_EF) for e in fin['efs']) or negs == 0
    if cls == 'EXPL':
        return '아니' in k['neg']
    if cls == 'DOUBLE':
        return negs != 1 or not plain_neg
    return True


CHECK_OF = {'PLAIN': 'neg-missing', 'NEGCOND': 'neg-missing', 'POT': 'pot-missing', 'PROHIB': 'prohib-missing',
            'OBLIG': 'oblig-missing', 'RHET': 'rhet-as-neg', 'CONFQ': 'confq-as-stmt', 'ASK': 'ask-as-neg',
            'EXPL': 'expl-missing', 'DOUBLE': 'double-as-neg'}


def neg_checks(k, fin, points, v, risks):
    """Checks from the learned function of each Japanese negation (the gate's own classifier). The risk of a point
    is the probability mass of the readings the Korean unit does NOT realise; it is flagged above RISK."""
    unit = {p['cls'] for p in points}
    for p in points:
        miss = sum(q for c, q in p['probs'].items() if not consistent(c, k, fin, unit))
        # a negation that human translations often write without a negative word (申し訳ない → 죄송하다,
        # しか…ない → 만) counts only with the probability that a reference writes it as a negation
        if p.get('realised') is not None and not (len(k['neg']) or k['pot_neg']):
            miss = min(miss, p['realised'])
        risks.append(miss)
        if miss > RISK:
            worst = max((c for c in p['probs'] if not consistent(c, k, fin, unit)), key=lambda c: p['probs'][c])
            v.append((CHECK_OF.get(worst, 'neg-missing'), p['clause']))


def check(exp, kos, risks=None):
    """The violated checks of one (Japanese expectations, Korean analysis) pair, with the clause that asks.
    risks (a list) receives the negation risk of every negation point."""
    v = []
    if risks is None:
        risks = []
    if exp is None:
        return v
    learned = exp.get('learned', False)
    for clauses, ks, points in units(exp, kos):
        k = ks[0]
        fin = k.get('last', k)          # the sentence that closes the unit
        negs = len(k['neg'])
        plain_neg = negs > 0 and not k['jana'] and not fin['q']
        neg_cl = [c for c in clauses if c['pol'] == 'neg']
        if learned:
            neg_checks(k, fin, points, v, risks)
        elif neg_cl and negs == 0 and not k['pot_neg'] and not k['jana'] and not k['oblig']:
            v.append(('neg-missing', neg_cl[0]['idx']))
        for c in clauses:
            fn = c['negFn']
            voice = c['dec'].get('voice', '')
            if not learned:
                pot_neg = fn == 'potential-neg' and (not c['rare'] or voice == 'POTENTIAL') or \
                    (c['pol'] == 'neg' and voice == 'POTENTIAL')
                if pot_neg and not k['pot_neg'] and not k['oblig']:
                    v.append(('pot-missing', c['idx']))
                if (fn == 'prohibitive' or c['force'] == 'prohib') and not k['prohib']:
                    v.append(('prohib-missing', c['idx']))
                if (fn == 'obligation' or 'must' in c['modality']) and not k['oblig']:
                    v.append(('oblig-missing', c['idx']))
                if fn == 'rhetorical' and plain_neg and not neg_cl:
                    v.append(('rhet-as-neg', c['idx']))
                if fn == 'double' and negs == 1 and plain_neg and not neg_cl:
                    v.append(('double-as-neg', c['idx']))
                if fn == 'archaic-vol' and negs > 0 and not neg_cl:
                    v.append(('vol-as-neg', c['idx']))
            elif c['pol'] == 'neg' and c.get('probs', {}).get('voice', {}).get('POTENTIAL', 0) > 0.8 and \
                    not k['pot_neg'] and not k['oblig']:
                v.append(('pot-missing', c['idx']))
            if c['root'] and c['force'] == 'q' and c['qmark'] and fn != 'rhetorical' and not fin['q'] and \
                    (len(ks) == 1 and 'last' not in k or c['sent'] == len(exp['sents']) - 1):
                v.append(('q-missing', c['idx']))
            if c['root'] and c['force'] == 'imp' and (k['pot_pos'] or k['pot_neg']) and \
                    not any(x['voice'] == 'potential' or x['negFn'] == 'potential-neg' for x in clauses):
                v.append(('imp-as-pot', c['idx']))
            vp = c.get('probs', {}).get('voice', {})
            # potential forms the Japanese can account for: likely-potential voice points, potential negations,
            # potential verbs (W2) outside れる/られる
            expected = sum(1 for x in clauses if x.get('probs', {}).get('voice', {}).get('POTENTIAL', 0) >= 0.4 or
                           x['voice'] == 'potential' and not x['rare']) + \
                sum(1 for n in points if n['probs'].get('POT', 0) >= 0.4)
            if vp and 1 - vp.get('POTENTIAL', 0) > RISK and k['npot'] > expected:
                v.append(('pass-as-pot', c['idx']))
            if vp and vp.get('POTENTIAL', 0) > 0.7 and not (k['pot_pos'] or k['pot_neg']):
                v.append(('pot-voice-missing', c['idx']))
            if vp and vp.get('HONORIFIC', 0) > 0.6 and not k['hon']:
                v.append(('hon-missing', c['idx']))
            if (c['voice'] == 'honorific' or c['polite'] == 'sonkei') and c['force'] not in ('imp', 'request', 'prohib') \
                    and not k['hon']:
                v.append(('hon-missing', c['idx']))
            if c['bene'] == 'receive' and k['calque']:
                v.append(('morau-calque', c['idx']))
    kvals = set(x for k in kos for x in k['numbers'])
    for n in exp['numbers']:
        if n not in kvals:
            v.append(('num-missing', -1))
            break
    text = ' '.join(k['text'] for k in kos)
    for nm in exp['names']:
        if not names.present(nm, text):
            v.append(('name-missing', -1))
            break
    return sorted(set(v))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ir')
    ap.add_argument('ko')
    ap.add_argument('--json')
    ap.add_argument('--decisions', help='hd8decide --decide --docs output on the same IR')
    ap.add_argument('--feats', help='hd8decide --dump --docs output on the same IR (negation points)')
    a = ap.parse_args()
    recs = ja.load_ir(a.ir)
    import negfn
    dec = ja.load_decisions(a.decisions, a.feats, negfn.Table() if a.feats else None) if a.decisions else {}
    kol = open(a.ko, encoding='utf-8').read().split('\n')
    out = []
    for i, r in enumerate(recs):
        exp = ja.expectations(r, dec.get(i, ()))
        kos = ko.analyse(kol[i] if i < len(kol) else '')
        out.append(dict(line=i + 1, v=check(exp, kos)))
    if a.json:
        with open(a.json, 'w') as f:
            for o in out:
                f.write(json.dumps(o, ensure_ascii=False) + '\n')
    for o in out:
        print('\t'.join(f'{c}@{k}' for c, k in o['v']))


if __name__ == '__main__':
    main()
