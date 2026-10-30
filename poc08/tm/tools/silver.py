#!/usr/bin/env python3
"""TM channel, step 3: LLM silver Korean for each aligned candidate, two independent passes, keep agreement.

  silver.py run --pass A|B [--size 60] [--jobs 24] [--limit N]
  silver.py agree                      -> WORK/silver.json

Each pass sends batches of candidates; an item shows the expression marked 【】 in 2-3 corpus lines (pass A the
first examples, pass B the next ones, or the same lines in another order when there are no more). The model
gives one Korean fragment (dictionary form) that fits every line, or says the Korean varies with the context,
or skips. Agreement: both passes give a fragment and one's fragment is the other's fragment or alternative
(spaces ignored). Outputs are cached per batch (WORK/silver-<pass>/b-NNNN.json) so a run can resume.
"""
import argparse
import concurrent.futures as cf
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

sys.path.insert(0, C.EVALTOOLS)
import api  # noqa: E402

MODEL = 'claude-sonnet-5-5'
PROMPT = """You translate Japanese into natural modern Korean. Each item below is one Japanese expression, marked
【like this】 in two or three real lines. For each item give the Korean a skilled translator would use for the
marked expression, as one reusable fragment that fits every line:
- If the expression ends in a verb or adjective, give a Korean predicate in dictionary form ending in 다, with the
  Korean of the words before it and their particle (戸をたたく → 문을 두드리다, 照射を浴びる → 빛을 쬐다,
  ピンと来る → 와닿다, 膝を折る → 무릎을 꿇다).
- If it ends in a noun, give a Korean noun phrase (パジャマのパンツ → 파자마 바지, 目玉商品 → 간판 상품).
- Translate only the marked words; no trailing particle or ending, no brackets, no slashes, no Japanese.
  Katakana loanwords stay loanwords in standard Korean spelling unless Korean uses a native word.
- Give `alt`: up to two other renderings that are just as good (may be empty).
Answer `varies` when no single Korean fragment is right in all the lines (the meaning or the right word changes
with the context), and `skip` when the marked text is not one expression (it cuts across phrases, contains a
name, or the line is noise or not Japanese).

Output one object per item, in order: {"i": 12, "v": "ok", "ko": "문을 두드리다", "alt": ["문을 노크하다"]},
{"i": 13, "v": "varies"}, {"i": 14, "v": "skip"}.

"""
SCHEMA = {'type': 'object', 'properties': {'items': {'type': 'array', 'items': {
    'type': 'object', 'properties': {'i': {'type': 'integer'}, 'v': {'type': 'string', 'enum': ['ok', 'varies', 'skip']},
                                     'ko': {'type': 'string'}, 'alt': {'type': 'array', 'items': {'type': 'string'}}},
    'required': ['i', 'v'], 'additionalProperties': False}}}, 'required': ['items'], 'additionalProperties': False}


def mark(e):
    s, a, b = e[2], e[3], e[4]
    return s[:a] + '【' + s[a:b] + '】' + s[b:]


def lines_for(x, p):
    ex = x['ex']
    if p == 'A':
        return [mark(e) for e in ex[:3]]
    rest = ex[3:6]
    if len(rest) >= 2:
        return [mark(e) for e in rest]
    return [mark(e) for e in reversed(ex[:3])]


def batch_text(items, p):
    out = []
    for i, x in items:
        out.append(f'### {i}\n' + '\n'.join('- ' + s for s in lines_for(x, p)))
    return PROMPT + '\n\n'.join(out) + '\n'


def cmd_run(a):
    cands = json.load(open(C.WORK + '/aligned.json', encoding='utf-8'))
    if a.limit:
        cands = cands[:a.limit]
    idx = list(enumerate(cands, 1))
    random.Random(7 if a.pass_ == 'A' else 11).shuffle(idx)
    d = os.path.join(C.WORK, 'silver-' + a.pass_)
    os.makedirs(d, exist_ok=True)
    batches = [idx[k:k + a.size] for k in range(0, len(idx), a.size)]

    def one(bi):
        path = os.path.join(d, f'b-{bi:04d}.json')
        if os.path.exists(path):
            return 0
        items = batches[bi]
        want = {i for i, _ in items}

        def val(o):
            got = {x['i'] for x in o['items']}
            if len(want - got) > 2:
                raise ValueError('missing items')
        o = api.call(MODEL, batch_text(items, a.pass_), SCHEMA, effort='low', max_tokens=16000, validate=val)
        json.dump(o['items'], open(path, 'w', encoding='utf-8'), ensure_ascii=False)
        return 1

    done = 0
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(one, bi) for bi in range(len(batches))]
        for f in cf.as_completed(futs):
            try:
                done += f.result()
            except Exception as e:  # noqa: BLE001
                print('batch failed', e, file=sys.stderr)
    print('pass', a.pass_, 'batches', len(batches), 'new', done)


def load_pass(p):
    out = {}
    d = os.path.join(C.WORK, 'silver-' + p)
    for f in sorted(os.listdir(d)):
        for x in json.load(open(os.path.join(d, f), encoding='utf-8')):
            out[x['i']] = x
    return out


def norm(s):
    return ''.join((s or '').split())


def cmd_agree(a):
    cands = json.load(open(C.WORK + '/aligned.json', encoding='utf-8'))
    A, B = load_pass('A'), load_pass('B')
    import collections
    st = collections.Counter()
    out = []
    for i, x in enumerate(cands, 1):
        pa, pb = A.get(i), B.get(i)
        if not pa or not pb:
            st['missing'] += 1
            continue
        if pa['v'] != 'ok' or pb['v'] != 'ok' or not pa.get('ko') or not pb.get('ko'):
            st[f"{pa['v']}/{pb['v']}"] += 1
            continue
        sa = {norm(pa['ko'])} | {norm(s) for s in pa.get('alt', [])}
        sb = {norm(pb['ko'])} | {norm(s) for s in pb.get('alt', [])}
        if norm(pa['ko']) in sb:
            ko = pa['ko']
        elif norm(pb['ko']) in sa:
            ko = pb['ko']
        else:
            st['disagree'] += 1
            continue
        st['agree'] += 1
        x['ko'] = ' '.join(ko.split())
        x['silver'] = {'A': pa['ko'], 'B': pb['ko']}
        out.append(x)
    json.dump(out, open(C.WORK + '/silver.json', 'w', encoding='utf-8'), ensure_ascii=False)
    print(dict(st))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--pass', dest='pass_', required=True, choices=['A', 'B'])
    r.add_argument('--size', type=int, default=60)
    r.add_argument('--jobs', type=int, default=24)
    r.add_argument('--limit', type=int, default=0)
    sub.add_parser('agree')
    a = ap.parse_args()
    {'run': cmd_run, 'agree': cmd_agree}[a.cmd](a)


if __name__ == '__main__':
    main()
