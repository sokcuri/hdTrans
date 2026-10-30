#!/usr/bin/env python3
"""Map each 0.7 ending row to (a) Japanese clause features and (b) a generator bundle, checked by round trip.

usage: endings075.py --base BASE_TSV --schema POS_SCHEMA --decomp ENDINGS_DUMP --struct HD8STRUCT --data DATA_DIR
                     --gen HD8GEN --gendata GEN_DATA --out OUT_TSV [--report]

(a) The key is attached to its host as in the decomposition dump (endings.py --dump), analysed with the teacher
    and folded by hd8struct; the features of the host's clause are written as k=v pairs.
(b) The Korean side is "<form code><tail>": the 0.7 stem form (inflect.cpp: 0 는, 1 stem, 2 으, 3 ㄹ/을, 4 ㄴ/는,
    5 past, 6 어, 7 ㅂ/습, 8 ㄴ/은, 9 ㅂ/읍, A stem, B ㅁ/음) and literal text. It is realized on two test stems
    per class; Kiwi (measurement only) splits the surface into morphemes, which become generator nodes; hd8gen
    generates them again. The bundle passes when hd8gen gives the 0.7 surface on both stems.
Needs fugashi + unidic (teacher) and kiwipiepy; run the parts with --stage.
"""
import argparse
import csv
import json
import os
import re
import subprocess
import sys
import tempfile

# ------------------------------------------------------------------ Hangul helpers
BASE, NJ, NF = 0xAC00, 21, 28
JONG = ['', 'ㄱ', 'ㄲ', 'ㄳ', 'ㄴ', 'ㄵ', 'ㄶ', 'ㄷ', 'ㄹ', 'ㄺ', 'ㄻ', 'ㄼ', 'ㄽ', 'ㄾ', 'ㄿ', 'ㅀ', 'ㅁ', 'ㅂ', 'ㅄ', 'ㅅ',
        'ㅆ', 'ㅇ', 'ㅈ', 'ㅊ', 'ㅋ', 'ㅌ', 'ㅍ', 'ㅎ']


def final(ch):
    c = ord(ch) - BASE
    return c % NF if 0 <= c < 11172 else -1


def with_final(ch, j):
    c = ord(ch) - BASE
    return chr(BASE + c - c % NF + JONG.index(j))


# Test stems: (lemma, stem, pos, past, infinitive). Regular stems only, so the forms are unambiguous.
STEMS = {
    'S': [('먹다', '먹', 'V', '먹었', '먹어'), ('가다', '가', 'V', '갔', '가')],
    'T': [('좋다', '좋', 'A', '좋았', '좋아'), ('싸다', '싸', 'A', '쌌', '싸')],
    'U': [('이다', '이', 'C', '이었', '이어')],
}


def form(stem, pos, past, inf, code):
    """0.7 form code → stem text (inflect.cpp Form for regular classes)."""
    last = stem[-1]
    fin = final(last)
    if pos in ('A', 'C'):
        if code == '4':
            code = '8'
        elif code == '8':
            code = 'B'
        elif code == '0':
            code = '8'
    if code == '0':
        return stem + '는'
    if code in ('1', 'A'):
        return stem
    if code == '2':
        return stem + ('으' if fin > 0 else '')
    if code == '3':
        return stem[:-1] + with_final(last, 'ㄹ') if fin == 0 else stem + '을'
    if code == '4':
        return stem[:-1] + with_final(last, 'ㄴ') if fin == 0 else stem + '는'
    if code == '5':
        return past
    if code == '6':
        return inf
    if code == '7':
        return stem[:-1] + with_final(last, 'ㅂ') if fin == 0 else stem + '습'
    if code == '8':
        return stem[:-1] + with_final(last, 'ㄴ') if fin == 0 else stem + '은'
    if code == '9':
        return stem[:-1] + with_final(last, 'ㅂ') if fin == 0 else stem + '읍'
    if code == 'B':
        return stem[:-1] + with_final(last, 'ㅁ') if fin == 0 else stem + '음'
    return None


def surfaces(fam, ko):
    m = re.match(r'^([0-9A-C]?)(.*)$', ko)
    code, tail = m.group(1), m.group(2)
    out = []
    for lemma, stem, pos, past, inf in STEMS.get(fam, STEMS['S']):
        host = '학생' if fam == 'U' else ''
        if not code:
            s = (host + tail) if fam == 'U' else None
            if s is None:
                return None
        else:
            f = form(stem, pos, past, inf, code)
            if f is None:
                return None
            s = host + f + tail
        out.append((lemma, host, s.strip()))
    return out


# ------------------------------------------------------------------ eomi table
def load_eomi(path):
    rows = []
    hdr = None
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        p = line.rstrip('\n').split('\t')
        if hdr is None:
            hdr = p
            continue
        rows.append(dict(zip(hdr, p)))
    return rows


JAMO = {'ㄴ': 'ᆫ', 'ㄹ': 'ᆯ', 'ㅁ': 'ᆷ', 'ㅂ': 'ᆸ'}


def variants(form):
    """eomi.tsv form notation → the strings Kiwi may print for it."""
    out = set()
    for alt in form.split('/'):
        alt = alt.replace('(으)', '')
        cands = {alt}
        if form.find('(으)') >= 0:
            cands.add(with_final('으', alt[0]) + alt[1:] if alt and alt[0] in JAMO else '으' + alt)
        more = set()
        for c in cands:
            if c.startswith('어'):
                more |= {'아' + c[1:], '여' + c[1:], c}
            else:
                more.add(c)
        for c in more:
            if c and c[0] in JAMO:
                out.add(JAMO[c[0]] + c[1:])
                out.add(c)
            else:
                out.add(c)
    return out


def eomi_index(rows):
    idx = {}
    for r in rows:
        for v in variants(r['form']):
            idx.setdefault((r['slot'], v), []).append(r)
    return idx


# ------------------------------------------------------------------ Kiwi → nodes
ASPECT = {('고', '있'): 'prog', ('어', '있'): 'result', ('아', '있'): 'result', ('어', '버리'): 'compl',
          ('아', '버리'): 'compl', ('어', '주'): 'give', ('아', '주'): 'give', ('어', '보'): 'try', ('아', '보'): 'try',
          ('어', '오'): 'dir-come', ('아', '오'): 'dir-come', ('어', '가'): 'dir-go', ('아', '가'): 'dir-go'}
NEG = {('지', '않'): 'ji-anh', ('지', '못하'): 'ji-mot', ('지', '말'): 'ji-ma'}
PARTICLE = {'JKS': 'subj', 'JKO': 'obj', 'JKG': 'gen'}


def to_nodes(morphs, host, lemma):
    """Kiwi morphemes of host+surface → generator nodes (first V is the host predicate)."""
    nodes = []
    i = 0
    if host:
        while i < len(morphs) and morphs[i][1].startswith('NN') and ''.join(m[0] for m in morphs[:i + 1]) != host:
            i += 1
        i += 1
        nodes.append({'kind': 'N', 'lemma': host})
    else:
        i = 1
    cur = {'kind': 'V', 'lemma': lemma}
    if host:
        # host noun + copula 이 from the surface
        if i < len(morphs) and morphs[i][1] == 'VCP':
            i += 1
        cur['glue'] = True
    nodes.append(cur)
    pending_ec = None
    while i < len(morphs):
        f, t = morphs[i]
        t = t.split('-')[0]  # VA-I, VV-R: irregular stems
        if t in ('XSV', 'XSA') and nodes and nodes[-1]['kind'] == 'N':
            base = nodes.pop()['lemma']
            cur = {'kind': 'V', 'lemma': base + f + '다'}
            nodes.append(cur)
            pending_ec = None
            i += 1
            continue
        if t == 'XR':
            nodes.append({'kind': 'N', 'lemma': f})
            i += 1
            continue
        if t == 'XSN' and nodes and nodes[-1]['kind'] == 'N':
            nodes[-1]['lemma'] += f
            i += 1
            continue
        if t == 'MAG' and f == '못' and i + 1 < len(morphs) and morphs[i + 1][1] in ('XSV', 'VV', 'VX') \
                and morphs[i + 1][0] == '하':
            morphs = morphs[:i] + [('못하', 'VX')] + morphs[i + 2:]
            continue
        if t == 'MAG':
            nodes.append({'kind': 'Adv', 'lemma': f})
            i += 1
            continue
        if t == 'EP':
            if f in ('었', '았', '였', 'ᆻ', '었었'):
                cur['tense'] = 'past'
            elif f == '겠':
                cur['modal'] = 'gess'
            elif f in ('시', '으시'):
                cur['_hon'] = True
            else:
                cur.setdefault('_ep', []).append(f)
        elif t in ('EF', 'EC', 'ETM', 'ETN'):
            cur.setdefault('_end', []).append((t, f))
            if t == 'EC':
                pending_ec = f
        elif t in ('VX', 'VV', 'VA', 'VCP', 'VCN'):
            key = (pending_ec, f) if pending_ec else None
            if key in ASPECT and cur.get('_end') and cur['_end'][-1][0] == 'EC':
                cur['_end'].pop()
                cur['aspect'] = ASPECT[key]
            elif key in NEG and cur.get('_end') and cur['_end'][-1][0] == 'EC':
                cur['_end'].pop()
                cur['neg'] = NEG[key]
            else:
                nl = {'VCP': '이다', 'VCN': '아니다'}.get(t, f + '다')
                cur = {'kind': 'V', 'lemma': nl}
                if t == 'VCP' and nodes and nodes[-1]['kind'] == 'N':
                    cur['glue'] = True
                elif t in ('VX',):
                    cur['aux'] = True
                nodes.append(cur)
            pending_ec = None
        elif t.startswith('NN') or t == 'NP':
            nodes.append({'kind': 'N', 'lemma': f, 'dep': t == 'NNB'})
            pending_ec = None
        elif t.startswith('J'):
            p = {'kind': 'P'}
            if t in PARTICLE:
                p['kase'] = PARTICLE[t]
            elif t == 'JX' and f in ('은', '는', 'ᆫ'):
                p['kase'] = 'top'
            elif t == 'JKB' and f in ('로', '으로'):
                p['kase'] = 'instr'
            elif t == 'JC' and f in ('와', '과'):
                p['kase'] = 'com'
            else:
                p['kase'] = 'lex'
                p['lemma'] = f
            nodes.append(p)
        elif t.startswith('S'):
            pass
        else:
            return None
        i += 1
    return nodes


def endings_of(node, idx):
    """Candidate (fields, level) settings for the endings Kiwi gave a V node."""
    ends = node.get('_end', [])
    if not ends:
        return [({'ctype': 'stem'} if node is not None else {}, None)]
    if len(ends) > 1:
        # two endings on one predicate (-다는, -다고 …): try the joined form as one ending
        joined = ''.join(e[1] for e in ends)
        ends = [(ends[-1][0], joined)]
    t, f = ends[-1]
    out = []
    slots = {'EF': ['final', 'nom', 'conn'], 'EC': ['conn', 'quote', 'final'], 'ETM': ['adn', 'quote'],
             'ETN': ['nom']}[t]
    if f == '예요':
        f = '에요'
    for slot in slots:
        for r in idx.get((slot, f), []):
            fields = {}
            if slot == 'final':
                fields = {'ctype': 'final', 'eomi': r['id']}
                if r['mood'] not in ('-', ''):
                    fields['mood'] = r['mood']
            elif slot == 'conn':
                fields = {'ctype': 'conn', 'eomi': r['id']}
            elif slot == 'quote':
                fields = {'ctype': 'quote', 'eomi': r['id']}
                if r['family'] == 'adn':
                    fields['rel'] = 'adn'
                else:
                    fields['mood'] = r['family']
            elif slot == 'adn':
                fields = {'ctype': 'adn', 'rel': r['family']}
            elif slot == 'nom':
                fields = {'ctype': 'nom', 'rel': r['family']}
            lv = r['level'] if r['level'] not in ('-', '') else None
            out.append((fields, lv))
    if not out:
        MISSING[(t, f)] = MISSING.get((t, f), 0) + 1
    return out


MISSING = {}


def bundles(nodes, idx, limit=24):
    """Every combination of ending candidates over the V nodes (small)."""
    vs = [k for k, n in enumerate(nodes) if n['kind'] == 'V']
    choices = []
    for k in vs:
        c = endings_of(nodes[k], idx)
        if not c:
            return []
        choices.append(c)
    combos = [[]]
    for c in choices:
        combos = [x + [y] for x in combos for y in c][:limit]
    out = []
    for combo in combos:
        ns = []
        level = None
        for k, n in enumerate(nodes):
            m = {kk: vv for kk, vv in n.items() if not kk.startswith('_')}
            if k in vs:
                fields, lv = combo[vs.index(k)]
                m.update(fields)
                if m.get('ctype') == 'adn' and m.get('tense') == 'past' and m.get('rel') == 'retro':
                    m['rel'] = 'past-retro'
                    del m['tense']
                if n.get('_hon'):
                    m['_hon'] = True
                if lv:
                    level = lv
            ns.append(m)
        out.append((ns, level or 'haera'))
    return out


# ------------------------------------------------------------------ main stages
def stage_features(args, rows):
    """(a): host+key through the teacher and hd8struct; features of the host clause."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    dump = {}
    for line in open(args.decomp, encoding='utf-8'):
        p = line.rstrip('\n').split('\t')
        if p[0] == 'key':
            continue
        dump[(p[0], p[1])] = p[4]
    texts = []
    for r in rows:
        host = dump.get((r['key'], r['fam']), '')
        r['host'] = host
        r['jtext'] = host + r['key'] if host and not r['key'].isascii() else ''
        if r['jtext']:
            texts.append(r['jtext'])
    uniq = list(dict.fromkeys(texts))
    teacher = subprocess.run([args.teacher_python or sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'teacher_toks.py')],
                             input='\n'.join(uniq) + '\n', capture_output=True, text=True, check=True).stdout
    ir = subprocess.run([args.struct, '--data', args.data], input=teacher, capture_output=True, text=True, check=True).stdout
    feats = {}
    for line in ir.splitlines():
        o = json.loads(line)
        cl = o['clauses']
        if not cl:
            continue
        c = cl[0]
        f = [f"link={c['link']}" + (f":{c['linkSub']}" if c.get('linkSub') else ''), f"tense={c['tense']}",
             f"pol={c['pol']}"]
        for k, d in (('negFn', 'none'), ('aspect', 'none'), ('voice', 'active'), ('bene', 'none'),
                     ('force', 'decl'), ('polite', 'plain')):
            if c[k] != d:
                f.append(f'{k}={c[k]}')
        for m in c['modality']:
            f.append(f'mod+={m}')
        for j in c['jreg']:
            f.append(f'jreg+={j}')
        if c['unfolded']:
            f.append('unfolded=%d' % len(c['unfolded']))
        if len(cl) > 1:
            f.append('clauses=%d' % len(cl))
        feats[o['src']] = ';'.join(f)
    for r in rows:
        r['jfeat'] = feats.get(r['jtext'], 'bare' if r['key'].isascii() else '')


def stage_korean(args, rows):
    from kiwipiepy import Kiwi
    kiwi = Kiwi()
    idx = eomi_index(load_eomi(os.path.join(args.gendata, 'ko', 'eomi.tsv')))
    cases = []
    for n, r in enumerate(rows):
        r['bundle'] = ''
        r['rt'] = 'nosurface'
        s = surfaces(r['fam'], r['ko']) if r['ko'] else None
        if not s:
            continue
        r['surf'] = [x[2] for x in s]
        cand_per_stem = []
        for lemma, host, surf in s:
            morphs = [(t.form, t.tag) for t in kiwi.tokenize(surf)]
            nodes = to_nodes(morphs, host, lemma)
            if nodes is None:
                cand_per_stem.append([])
                continue
            cand_per_stem.append(bundles(nodes, idx))
        r['cands'] = cand_per_stem
        for si, cands in enumerate(cand_per_stem):
            for ci, (ns, level) in enumerate(cands):
                for narr in (True, False):
                    kir2, reg = [], []
                    for k, m in enumerate(ns, 1):
                        m = dict(m)
                        m['id'] = k
                        if m.pop('_hon', False):
                            reg.append({'node': k, 'level': level, 'persona': 'none', 'hon': True})
                        kir2.append(m)
                    cases.append({'case': f'{n}:{si}:{ci}:{int(narr)}', 'kir2': kir2, 'reg': reg,
                                  'lev': [{'utt': 0, 'level': level, 'narration': narr}], 'out': r['surf'][si]})
        r['rt'] = 'fail'
    with tempfile.NamedTemporaryFile('w', suffix='.jsonl', delete=False, encoding='utf-8') as fh:
        for c in cases:
            fh.write(json.dumps(c, ensure_ascii=False) + '\n')
        path = fh.name
    res = subprocess.run([args.gen, '--data', args.gendata, 'gen', path], capture_output=True, text=True)
    os.unlink(path)
    got = {}
    for line in res.stdout.splitlines() + res.stderr.splitlines():
        p = line.split('\t')
        if len(p) >= 3 and p[0] in ('DIFF', 'SAME', 'OK'):
            got[p[1]] = p[2]
    bycase = {c['case']: c for c in cases}
    if os.environ.get('E075_DEBUG'):
        with open(os.environ['E075_DEBUG'], 'w', encoding='utf-8') as dbg:
            for cid, c in bycase.items():
                n = int(cid.split(':')[0])
                dbg.write(f"{rows[n]['key']}\t{rows[n]['ko']}\t{cid}\t{c['out']}\t{got.get(cid, '?')}\t"
                          f"{json.dumps(c['kir2'], ensure_ascii=False)}\n")
    same = set()
    for cid, c in bycase.items():
        g = got.get(cid)
        if g is None:
            continue
        if g == c['out']:
            same.add(cid)
        elif g.replace(' ', '') == c['out'].replace(' ', ''):
            same.add(cid + '~')
    for n, r in enumerate(rows):
        if r['rt'] == 'nosurface':
            continue
        best = None
        for si, cands in enumerate(r['cands']):
            for ci, (ns, level) in enumerate(cands):
                for narr in (1, 0):
                    cid = f'{n}:{si}:{ci}:{narr}'
                    if cid in same or cid + '~' in same:
                        exact = cid in same
                        if best is None or (exact and not best[3]):
                            best = (si, ns, level, exact, narr)
        # pass only when every stem has a bundle that reproduces its surface; keep stem 0's bundle
        ok_stems = set()
        for si, cands in enumerate(r['cands']):
            for ci in range(len(cands)):
                for narr in (1, 0):
                    cid = f'{n}:{si}:{ci}:{narr}'
                    if cid in same or cid + '~' in same:
                        ok_stems.add(si)
        if best and len(ok_stems) == len(r['cands']):
            r['rt'] = 'ok' if best[3] else 'spacing'
            ns = best[1]
            r['bundle'] = json.dumps({'nodes': ns, 'level': best[2], 'narration': bool(best[4])}, ensure_ascii=False)
        elif best:
            r['rt'] = 'partial'
            r['bundle'] = json.dumps({'nodes': best[1], 'level': best[2], 'narration': bool(best[4])},
                                     ensure_ascii=False)
        else:
            c0 = r['cands'][0] if r['cands'] and r['cands'][0] else []
            if c0:
                r['bundle'] = json.dumps({'nodes': c0[0][0], 'level': c0[0][1]}, ensure_ascii=False)
            r['rt'] = 'fail' if c0 else 'noparse'
        del r['cands']


def family(r):
    try:
        b = json.loads(r['bundle'])
    except Exception:
        return 'none'
    vs = [n for n in b['nodes'] if n.get('kind') == 'V']
    if not vs:
        return 'none'
    v = vs[-1]
    ct = v.get('ctype', 'final')
    if ct in ('conn', 'quote'):
        return f"{ct}:{v.get('eomi', '')}"
    if ct in ('adn', 'nom'):
        return f"{ct}:{v.get('rel', '')}"
    return f"final:{v.get('eomi', '')}"


def main():
    ap = argparse.ArgumentParser()
    for k in ('base', 'schema', 'decomp', 'struct', 'data', 'gen', 'gendata', 'out'):
        ap.add_argument('--' + k, required=True)
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--teacher-python', default='', help='a Python with fugashi and unidic, if not this one')
    args = ap.parse_args()
    cls = {r['code']: r['class'] for r in csv.DictReader(open(args.schema, encoding='utf-8'), delimiter='\t')}
    rows = []
    hdr = None
    for line in open(args.base, encoding='utf-8'):
        if line.startswith('#'):
            continue
        p = line.rstrip('\n').split('\t')
        if hdr is None:
            hdr = p
            continue
        d = dict(zip(hdr, p))
        if cls.get(d['pos']) == '5':
            rows.append({'id': d['id'], 'key': d['key'], 'pos': d['pos'], 'fam': d['pos'][0] if d['pos'][0] in 'STU' else 'S',
                         'ko': d['ko']})
    stage_features(args, rows)
    stage_korean(args, rows)
    with open(args.out, 'w', encoding='utf-8') as fh:
        fh.write('#! endings075\n# The 0.7 ending rows (base.tsv POS class 5): Japanese clause features from aux.tsv, and the '
                 'generator bundle that reproduces the 0.7 Korean (rt: ok, spacing, partial, fail, noparse, nosurface). st=u.\n')
        fh.write('id\tkey\tpos\tko\tjfeat\tbundle\trt\tfamily\tst\n')
        for r in rows:
            fh.write('\t'.join([r['id'], r['key'], r['pos'], r['ko'], r['jfeat'], r['bundle'], r['rt'], family(r), 'u'])
                     + '\n')
    if args.report:
        import collections
        c = collections.Counter(r['rt'] for r in rows)
        print('rows', len(rows), dict(c))
        ok = sum(c[k] for k in ('ok', 'spacing'))
        print(f'round trip ok+spacing {ok}/{len(rows)} = {ok / len(rows):.1%}')
        fam = collections.defaultdict(collections.Counter)
        for r in rows:
            fam[family(r)][r['rt'] in ('ok', 'spacing')] += 1
        for f, cc in sorted(fam.items(), key=lambda kv: -sum(kv[1].values()))[:40]:
            n = sum(cc.values())
            print(f'  {f:28s} {n:5d}  {cc[True] / n:.1%}')
        print('endings the generator table lacks (Kiwi tag, form, candidate count):')
        for (t, f), n in sorted(MISSING.items(), key=lambda kv: -kv[1])[:40]:
            print(f'  {t}\t{f}\t{n}')
        jf = sum(1 for r in rows if r['jfeat'])
        print(f'japanese features {jf}/{len(rows)}')


if __name__ == '__main__':
    main()
