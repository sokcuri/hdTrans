#!/usr/bin/env python3
"""Tail lexicon audit, steps 2-4: candidate rows, the blind second pass, the final 0.7 dictionary layer.

usage:
  audit_layer.py candidates --audit DIR [--audit DIR ...] --engine BIN --dicts DIR --work DIR [--size 60]
  audit_layer.py final      --work DIR --out DIR --engine BIN --dicts DIR
  audit_layer.py test       --layer DIR --engine BIN --dicts DIR

candidates: reads items.json and the pass-1 outputs (o1-*.jsonl) of each audit dir. For each proposed change
it copies every row of the key's class (codes and rules unchanged) and gives the row the engine chose the new
Korean, encoded with a marker family when it is a predicate. A key the dictionary lacks (or that only a
user-format row holds) gets a user-format row. The engine runs with and without the layer on the item's
example lines; rows that change nothing or that the engine rejects are dropped. Blind A/B packets (p2-*.txt,
order seeded per item) go to the second pass.
final: keeps the changes the second pass (o2-*.jsonl) preferred. It writes the layer as two files for a
dictionary folder (a column file and a user-format file, both base layer), a provenance table and the fix
cases (example lines with the expected output).
test: runs the cases with the layer and reports pass/fail.
"""
import argparse
import collections
import glob
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lexlib  # noqa: E402
from lexconv import read_tsv  # noqa: E402

COLS = ['key', 'ko', 'pos', 'sem', 'gram', 'dict', 'rule', 'ctx', 'note', 'flags', 'prio', 'reading', 'id']
COL_FILE, USER_FILE = 'zz-tail-audit-b.tsv', 'zz-tail-audit-a.tsv'
HEAD_COL = '#! layer=base\n# Audited glosses: copies of the rows of a key\'s class, the chosen row with new Korean.\n'
HEAD_USER = '#! layer=base\n# Audited glosses of keys without a column row.\n'
USER_POS = {'n': 'n', 'v1': 'v1', 'v5': 'v5', 'adj': 'adj', 'na': 'na', 'adv': 'adv', 'intj': 'adv', 'conj': 'adv'}
USER_CLASS = {'n': '1', 'pron': '1', 'name': '1', 'v': '4', 'v1': '4', 'v5': '4', 'vs': '4', 'adj': '7', 'na': '8',
              'adv': '3', 'ono': '3', 'intj': '3', 'conj': '3', 'adn': '0', 'phrase': '9'}
TAG = {'REG': '규칙', 'HA': '하다', 'D': 'ㄷ', 'B': 'ㅂ', 'BWA': 'ㅂ', 'S': 'ㅅ', 'H': 'ㅎ', 'REU': '르', 'REO': '러',
       'U': '우', 'EU': '으', 'L': 'ㄹ', 'COP': '이다'}
BAD_KO = re.compile(r'[぀-ヿ一-鿿()（）/\[\]{}<>@&$%,]')
ERR = re.compile(r'(zz-tail-audit-[ab]\.tsv):(\d+): (.*)')
ENGINE_POS = lexlib.ENGINE_POS


# ---------------------------------------------------------------- engine

def run(engine, dicts, lines, layer=None, json_out=False):
    """Engine output (or JSON) for lines, with an optional layer {file: text} in a copy of the folder."""
    env = dict(os.environ, HDTRANS_CACHE_DIR='off', HOME=tempfile.mkdtemp())
    d, tmp = dicts, None
    if layer is not None:
        tmp = tempfile.mkdtemp()
        d = os.path.join(tmp, 'dicts')
        shutil.copytree(dicts, d)
        for name, text in layer.items():
            open(os.path.join(d, name), 'w', encoding='utf-8').write(text)
    args = [engine, '--dicts', d, '--each'] + (['--json'] if json_out else [])
    p = subprocess.run(args, input='\n'.join(lines) + '\n', capture_output=True, text=True, env=env)
    if tmp:
        shutil.rmtree(tmp, ignore_errors=True)
    if json_out:
        return json.loads(p.stdout) if p.stdout.strip() else None, p.stderr
    out = p.stdout.split('\n')
    return out[:len(lines)] + [''] * max(0, len(lines) - len(out)), p.stderr


def layer_text(changes):
    col, user = [HEAD_COL, '\t'.join(COLS) + '\n'], [HEAD_USER]
    for ch in changes:
        if ch['kind'] == 'col':
            for r in ch['rows']:
                col.append('\t'.join(r.get(c, '') for c in COLS) + '\n')
        else:
            user.append('\t'.join(ch['user']) + '\n')
    return {COL_FILE: ''.join(col), USER_FILE: ''.join(user)}


def line_owner(changes):
    """(file, line number) → change index, for the engine's load errors."""
    own = {}
    n = 4
    for i, ch in enumerate(changes):
        if ch['kind'] == 'col':
            for _ in ch['rows']:
                own[(COL_FILE, n)] = i
                n += 1
    n = 3
    for i, ch in enumerate(changes):
        if ch['kind'] == 'user':
            own[(USER_FILE, n)] = i
            n += 1
    return own


def validate(engine, dicts, changes):
    """Drop changes whose rows the engine rejects; tag ambiguous user-format classes. Returns (kept, dropped)."""
    dropped = []
    for _ in range(5):
        _, err = run(engine, dicts, ['テスト'], layer_text(changes))
        own = line_owner(changes)
        bad = {}
        for m in ERR.finditer(err):
            i = own.get((m.group(1), int(m.group(2))))
            if i is not None:
                bad.setdefault(i, m.group(3))
        if not bad:
            return changes, dropped
        keep = []
        for i, ch in enumerate(changes):
            msg = bad.get(i)
            if msg is None:
                keep.append(ch)
            elif ch['kind'] == 'user' and 'name the class' in msg and ':' not in ch['user'][2]:
                cls, _ = lexlib.guess_class(ch['user'][1], 'A' if ch['user'][2] in ('adj', 'na') else 'V')
                if cls in TAG:
                    ch['user'][2] += ':' + TAG[cls]
                    keep.append(ch)
                else:
                    dropped.append((ch, msg))
            else:
                dropped.append((ch, msg))
        changes = keep
    return changes, dropped


# ---------------------------------------------------------------- Korean markers

def family_index(rows):
    """(marker, stem, V/A, class, uses in the dictionary) for every marker family."""
    uses = collections.Counter()
    for r in rows:
        for m in lexlib.MARK.findall(r.get('ko', '')):
            uses[m] += 1
    src = open(os.path.join(lexlib.REPO, 'src/core/output/ko_families_gen.cpp'), encoding='utf-8').read()
    out = []
    for m in re.finditer(r'\{"([&$%].{2}@)", "([^"]*)", Pos::(\w+), Class::(\w+), ([^,]+),', src):
        cls = lexlib.FAM_CLASS[m.group(4)]
        if cls == 'B' and 'kBieupWa' in m.group(5):
            cls = 'BWA'
        out.append((m.group(1), m.group(2), 'V' if m.group(3) == 'Verb' else 'A', cls, uses[m.group(1)]))
    return out


def encode(lemma, kpos, fams):
    """Korean predicate lemma → stem + marker of a family with the same final stem, part of speech and class."""
    words = lemma.split(' ')
    last = words[-1]
    if not last.endswith('다') or len(last) < 2:
        return None
    stem = last[:-1]
    cls, _ = lexlib.guess_class(last, kpos)
    best = None
    for mark, fstem, fpos, fcls, uses in fams:
        if fpos != kpos or fcls != cls or not stem.endswith(fstem) or not uses:
            continue
        rank = (len(fstem), uses)
        if best is None or rank > best[0]:
            best = (rank, mark, fstem)
    if not best:
        return None
    _, mark, fstem = best
    return ' '.join(words[:-1] + [stem[:len(stem) - len(fstem)] + mark])


# ---------------------------------------------------------------- candidates

PRED_POS = ('v1', 'v5', 'adj', 'na')


def key_problem(it, target_pos):
    """Why an item's key may not take its lemma's new gloss: the engine key is only part of the word (a noun
    key 手 under the lemma 手下), or the key's row is of another class (the noun 愛 under the verb 愛す)."""
    e = it['examples'][0]
    if it['pos'] not in PRED_POS and it['key'] != e['surf']:
        return 'partial-key'
    if it['pos'] in PRED_POS and not (e['surf'].startswith(it['key']) and len(it['key']) >= len(e['surf']) - 2):
        return 'partial-key'
    if target_pos and ENGINE_POS.match(target_pos):
        cls = target_pos[1]
        want = {'v1': '4', 'v5': '4', 'adj': '7', 'na': '8'}.get(it['pos'])
        if want and cls != want:
            return 'class-mismatch'
        if not want and cls in ('4', '7'):
            return 'class-mismatch'
    return None

def cmd_candidates(a):
    rows = lexlib.load_rows(a.dicts)
    by_key = collections.defaultdict(list)
    for r in rows:
        if r['layer'] in ('base', 'words'):
            by_key[r['key']].append(r)
    fams = family_index(rows)
    grammar_keys = {r['key'] for r in rows if lexlib.row_kind(r) in ('particle', 'ending')}
    props = []
    skipped = collections.Counter()
    for adir in a.audit:
        items = json.load(open(os.path.join(adir, 'items.json'), encoding='utf-8'))
        for f in sorted(glob.glob(os.path.join(adir, 'o1-*.jsonl'))):
            for line in open(f, encoding='utf-8'):
                if not line.strip():
                    continue
                x = json.loads(line)
                if x.get('v') != 'change':
                    continue
                it = items[x['i'] - 1]
                ko = re.sub(r'\s+', ' ', (x.get('ko') or '').strip())
                if not ko or ko == it['ko'] or BAD_KO.search(ko):
                    skipped['same-or-bad-ko'] += 1
                    continue
                pred = it['pos'] in ('v1', 'v5', 'adj', 'na')
                if pred != ko.endswith('다') and not (it['pos'] == 'na' and not ko.endswith('다')):
                    skipped['part-of-speech'] += 1
                    continue
                if it['pos'] == 'na' and not ko.endswith('다'):
                    skipped['part-of-speech'] += 1
                    continue
                props.append((it, ko, x.get('why', ''), f'{os.path.basename(adir)}:{x["i"]}'))

    # the raw Korean the engine chose for each key, from its JSON on the first example line
    first = sorted({it['examples'][0]['ja'] for it, *_ in props})
    d, _ = run(a.engine, a.dicts, first, json_out=True)
    raw = {}
    for c in d['choices']:
        raw.setdefault((first[c['line']], c['surface']), c['choice']['text'])
    changes = []
    seen = set()
    for it, ko, why, src in props:
        key = it['key']
        if key in seen:
            skipped['duplicate-key'] += 1
            continue
        seen.add(key)
        if key in grammar_keys:
            skipped['grammar-homograph'] += 1
            continue
        text = raw.get((it['examples'][0]['ja'], key))
        cand = by_key.get(key, [])
        target = [r for r in cand if r['ko'] == text]
        base = {'old': it['ko'], 'new_ko': ko, 'lid': it['lid'], 'pos': it['pos'], 'why': why, 'src': src,
                'fixed': it['fixed'], 'key': key, 'lines': [e['ja'] for e in it['examples']],
                'base': [e['out'] for e in it['examples']]}
        col_target = [r for r in target if not r['userfmt'] and ENGINE_POS.match(r['pos'])]
        why_not = key_problem(it, col_target[0]['pos'] if col_target else '')
        if why_not:
            skipped[why_not] += 1
            continue
        if col_target:
            t = col_target[0]
            digit = t['pos'][1]
            same = [r for r in cand if (not r['userfmt'] and ENGINE_POS.match(r['pos']) and r['pos'][1] == digit)]
            if any(r['userfmt'] and USER_CLASS.get(r['pos'].split(':')[0]) == digit for r in cand):
                skipped['mixed-formats'] += 1
                continue
            if t['pos'][1] in ('4', '7', '8'):
                kpos = 'V' if t['pos'][1] == '4' else 'A'
                new_raw = encode(ko, kpos, fams)
                if not new_raw:
                    skipped['no-marker-family'] += 1
                    continue
            else:
                new_raw = ko
            out = []
            for r in same:
                c = {k: r.get(k, '') for k in COLS}
                c['id'] = 'audit:' + (r.get('id') or f'{r["file"]}:{r["line"]}')
                if r is t:
                    c['ko'] = new_raw
                    c['flags'] = ' '.join(x for x in c['flags'].split() if x != 'llm')
                out.append(c)
            changes.append({**base, 'kind': 'col', 'rows': out})
        else:
            pos = USER_POS.get(it['pos'])
            if not pos:
                skipped['no-user-pos'] += 1
                continue
            jkey = it['examples'][0]['dict'] if it['pos'] in ('v1', 'v5', 'adj') else key
            if any(r['userfmt'] for r in cand) or not cand or it['fixed']:
                changes.append({**base, 'kind': 'user', 'user': [jkey, ko, pos]})
            else:
                skipped['row-not-found'] += 1
    changes, dropped = validate(a.engine, a.dicts, changes)
    skipped['engine-rejects'] += len(dropped)
    lines = sorted({s for ch in changes for s in ch['lines']})
    new, _ = run(a.engine, a.dicts, lines, layer_text(changes))
    if not any(new):
        sys.exit('engine produced no output with the layer')
    new = dict(zip(lines, new))
    kept = []
    for ch in changes:
        ch['new'] = [new[s] for s in ch['lines']]
        if ch['new'] == ch['base']:
            skipped['no-effect'] += 1
            continue
        kept.append(ch)
    os.makedirs(a.work, exist_ok=True)
    rnd = random.Random(808)
    for pi in range(0, len(kept), a.size):
        with open(os.path.join(a.work, f'p2-{pi // a.size + 1:03d}.txt'), 'w', encoding='utf-8') as o:
            for j, ch in enumerate(kept[pi:pi + a.size], pi + 1):
                ch['flip'] = rnd.random() < 0.5
                o.write(f'### {j} | word: {ch["key"]}\n')
                for s, b, n in zip(ch['lines'], ch['base'], ch['new']):
                    A, B = (n, b) if ch['flip'] else (b, n)
                    o.write(f'- {s}\n  A: {A}\n  B: {B}\n')
                o.write('\n')
    json.dump(kept, open(os.path.join(a.work, 'candidates.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print('candidates', len(kept), 'col', sum(1 for c in kept if c['kind'] == 'col'), 'user',
          sum(1 for c in kept if c['kind'] == 'user'), 'skipped', dict(skipped),
          'packets', (len(kept) + a.size - 1) // a.size)


def cmd_filter(a):
    """Mark candidates of an existing list that key_problem rejects (the list order, and so the second-pass
    item numbers, stay the same)."""
    kept = json.load(open(os.path.join(a.work, 'candidates.json'), encoding='utf-8'))
    items = {}
    for adir in a.audit:
        items[os.path.basename(adir)] = json.load(open(os.path.join(adir, 'items.json'), encoding='utf-8'))
    stats = collections.Counter()
    for ch in kept:
        d, i = ch['src'].split(':')
        it = items[d][int(i) - 1]
        tpos = ch['rows'][0]['pos'] if ch['kind'] == 'col' and ch.get('rows') else ''
        why = key_problem(it, tpos)
        ch['dropped'] = why or ''
        stats[why or 'kept'] += 1
    json.dump(kept, open(os.path.join(a.work, 'candidates.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print(dict(stats))


# ---------------------------------------------------------------- final and test

def cmd_final(a):
    kept = json.load(open(os.path.join(a.work, 'candidates.json'), encoding='utf-8'))
    verdict = {}
    for f in sorted(glob.glob(os.path.join(a.work, 'o2-*.jsonl'))):
        for line in open(f, encoding='utf-8'):
            if line.strip():
                x = json.loads(line)
                verdict[x['i']] = x.get('v', '')
    final, stats = [], collections.Counter()
    for j, ch in enumerate(kept, 1):
        v = verdict.get(j, 'missing')
        new_side = 'A' if ch['flip'] else 'B'
        res = 'new' if v == new_side else 'old' if v in ('A', 'B') else v
        stats[res] += 1
        ch['p2'] = f'p2:{j}:{res}'
        if ch.get('dropped'):
            stats['dropped:' + ch['dropped']] += 1
            continue
        if res == 'new':
            final.append(ch)
    # expected outputs with the final layer only (rejected changes no longer interact)
    lines = sorted({s for ch in final for s in ch['lines']})
    out, _ = run(a.engine, a.dicts, lines, layer_text(final))
    out = dict(zip(lines, out))
    for ch in final:
        ch['new'] = [out[s] for s in ch['lines']]
    os.makedirs(a.out, exist_ok=True)
    for name, text in layer_text(final).items():
        open(os.path.join(a.out, name), 'w', encoding='utf-8').write(text)
    with open(os.path.join(a.out, 'tail-audit.prov.tsv'), 'w', encoding='utf-8') as o:
        o.write('key\tpos\told\tnew\tlemma\twhy\tlayer\taudit\n')
        for ch in final:
            o.write('\t'.join([ch['key'], ch['pos'], ch['old'], ch['new_ko'], ch['lid'], ch['why'],
                               'names' if ch['fixed'] else 'dict', ch['src'] + ' ' + ch['p2']]) + '\n')
    with open(os.path.join(a.out, 'tail-audit.cases.tsv'), 'w', encoding='utf-8') as o:
        o.write('id\tkind\tja\tko\n')
        for ch in final:
            for k, (s, n) in enumerate(zip(ch['lines'], ch['new']), 1):
                o.write('\t'.join([f'{ch["key"]}#{k}', 'fix', s.replace('\t', ' '), n.replace('\t', ' ')]) + '\n')
    print('changes', len(final), 'second pass', dict(stats))


def cmd_test(a):
    cases = list(read_tsv(os.path.join(a.layer, 'tail-audit.cases.tsv')))
    layer = {n: open(os.path.join(a.layer, n), encoding='utf-8').read() for n in (COL_FILE, USER_FILE)}
    got, _ = run(a.engine, a.dicts, [c['ja'] for c in cases], layer)
    bad = [(c, g) for c, g in zip(cases, got) if g.strip() != c['ko'].strip()]
    for c, g in bad[:20]:
        print('FAIL', c['id'], c['ja'], '| want', c['ko'], '| got', g)
    print(f'{len(cases) - len(bad)}/{len(cases)} cases pass')
    sys.exit(1 if bad else 0)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('candidates')
    c.add_argument('--audit', action='append', required=True)
    c.add_argument('--engine', required=True)
    c.add_argument('--dicts', required=True)
    c.add_argument('--work', required=True)
    c.add_argument('--size', type=int, default=60)
    f = sub.add_parser('final')
    f.add_argument('--work', required=True)
    f.add_argument('--out', required=True)
    f.add_argument('--engine', required=True)
    f.add_argument('--dicts', required=True)
    fl = sub.add_parser('filter')
    fl.add_argument('--work', required=True)
    fl.add_argument('--audit', action='append', required=True)
    t = sub.add_parser('test')
    t.add_argument('--layer', required=True)
    t.add_argument('--engine', required=True)
    t.add_argument('--dicts', required=True)
    a = ap.parse_args()
    {'candidates': cmd_candidates, 'filter': cmd_filter, 'final': cmd_final, 'test': cmd_test}[a.cmd](a)


if __name__ == '__main__':
    main()
