#!/usr/bin/env python3
"""Teacher ensemble: UniDic ∪ Sudachi, with adjudicated disagreement patterns.

  ensemble.py patterns UNIDIC SUDACHI OUT.tsv          disagreement patterns with counts and examples
  ensemble.py packet PATTERNS.tsv N K TAIL OUT_DIR      blind packets: K examples of the N most frequent
                                                        patterns, one example of TAIL patterns drawn by count
                                                        from the rest
  ensemble.py apply UNIDIC SUDACHI VERDICTS LEX OUT     silver where adjudicated patterns take Sudachi's reading;
                                                        a pattern not adjudicated where UniDic splits content
                                                        words that Sudachi reads as one content word takes it
                                                        too (adjudicated tail sample: Sudachi 67%, UniDic 2%)

UNIDIC and SUDACHI are tag.py-format files over the same lines. A disagreement region is a minimal span
where the two token boundary sets differ, or a token of the same span with another pos1. Its pattern is
the pair of readings (surface/pos1 of each token). VERDICTS: id, pattern, winner (U or S).
LEX: the UniDic lex.csv, used to give Sudachi's tokens UniDic lemma and reading fields.
"""
import collections, csv, json, os, random, sys


def toks(line):
    out, pos = [], 0
    for t in line.rstrip('\n').split('\x1f'):
        f = t.split('\x1e')
        if len(f) < 12:
            continue
        pos += len(f[0])
        b = pos
        pos += len(f[1])
        if f[2] == '空白':
            continue
        out.append((b, pos, f))
    return out


def regions(u, s):
    """Yield (ui, uj, si, sj) index ranges of disagreeing regions."""
    i = j = 0
    while i < len(u) and j < len(s):
        if u[i][0] == s[j][0] and u[i][1] == s[j][1]:
            if u[i][2][2] != s[j][2][2]:
                yield (i, i + 1, j, j + 1)
            i += 1
            j += 1
            continue
        # grow until both ends meet
        i0, j0 = i, j
        ue, se = u[i][1], s[j][1]
        while ue != se:
            if ue < se:
                i += 1
                if i >= len(u):
                    return
                ue = u[i][1]
            else:
                j += 1
                if j >= len(s):
                    return
                se = s[j][1]
        i += 1
        j += 1
        yield (i0, i, j0, j)


def reading(ts):
    return ' '.join(f'{t[2][1]}/{t[2][2]}' for t in ts)


def detail(ts):
    return ' '.join(f"{t[2][1]}[{t[2][9]}/{'-'.join(x for x in t[2][2:6] if x != '*')}]" for t in ts)


def cmd_patterns(a):
    cnt = collections.Counter()
    ex = collections.defaultdict(list)
    lines = 0
    with open(a[0], encoding='utf-8') as fu, open(a[1], encoding='utf-8') as fs:
        for n, (lu, ls) in enumerate(zip(fu, fs)):
            u, s = toks(lu), toks(ls)
            src = ''.join(t.split('\x1e')[0] + t.split('\x1e')[1] for t in lu.rstrip('\n').split('\x1f')
                          if len(t.split('\x1e')) >= 12)
            hit = False
            for (i0, i1, j0, j1) in regions(u, s):
                key = reading(u[i0:i1]) + '\t' + reading(s[j0:j1])
                cnt[key] += 1
                hit = True
                if len(ex[key]) < 20:
                    ex[key].append((n, src, u[i0][0], u[i1 - 1][1], detail(u[i0:i1]), detail(s[j0:j1])))
            lines += hit
    with open(a[2], 'w', encoding='utf-8') as o:
        o.write('n\tunidic\tsudachi\texamples\n')
        for k, v in cnt.most_common():
            o.write(f'{v}\t{k}\t{json.dumps(ex[k], ensure_ascii=False)}\n')
    total = sum(cnt.values())
    print('lines with a disagreement', lines, 'regions', total, 'patterns', len(cnt), file=sys.stderr)
    acc = 0
    for r, (k, v) in enumerate(cnt.most_common(), 1):
        acc += v
        if r in (100, 300, 1000, 3000, 10000):
            print(f'top {r}: {acc / total:.3f} of regions', file=sys.stderr)


def cmd_packet(a):
    path, n, k, tail, out = a[0], int(a[1]), int(a[2]), int(a[3]), a[4]
    os.makedirs(out, exist_ok=True)
    rows, rest = [], []
    with open(path, encoding='utf-8') as fh:
        next(fh)
        for r, line in enumerate(fh):
            c, ku, ks, exs = line.rstrip('\n').split('\t')
            (rows if r < n else rest).append((r, ku + '\t' + ks, json.loads(exs), int(c)))
    random.seed(17)
    picked = set()
    weights = [x[3] for x in rest]
    while len(picked) < min(tail, len(rest)):
        picked.add(random.choices(range(len(rest)), weights=weights)[0])
    rows = [(r, p, e) for r, p, e, c in rows] + [(rest[i][0], rest[i][1], rest[i][2][:1]) for i in sorted(picked)]
    with open(os.path.join(out, 'packet.tsv'), 'w', encoding='utf-8') as o, \
         open(os.path.join(out, 'key.tsv'), 'w', encoding='utf-8') as kk:
        o.write('id\tja\tspan\tA\tB\n')
        for r, pat, exs in rows:
            for e, (line, src, b, en, du, ds) in enumerate(random.sample(exs, min(k, len(exs)))):
                span = src[b:en]
                rid = f'{r}.{e}'
                if random.random() < 0.5:
                    o.write(f'{rid}\t{src}\t{span}\t{du}\t{ds}\n')
                    kk.write(f'{rid}\t{r}\tUA\t{pat}\n')
                else:
                    o.write(f'{rid}\t{src}\t{span}\t{ds}\t{du}\n')
                    kk.write(f'{rid}\t{r}\tSA\t{pat}\n')


def load_lex(path):
    idx = collections.defaultdict(list)
    with open(path, encoding='utf-8') as fh:
        for r in csv.reader(fh):
            if r[0] != r[12]:
                continue
            idx[(r[0], r[4], r[5], r[6], r[7], r[8], r[9])].append((r[11], r[10], r[24]))
    return idx


def cmd_apply(a):
    fu_path, fs_path, verdicts, lexp, outp = a
    win, judged = set(), set()
    with open(verdicts, encoding='utf-8') as fh:
        for line in fh:
            f = line.rstrip('\n').split('\t')
            if len(f) >= 4:
                judged.add(f[1] + '\t' + f[2])
                if f[3] == 'S':
                    win.add(f[1] + '\t' + f[2])
    function = ('助詞', '助動詞', '補助記号', '記号', '空白')

    def merge_content(ut, st):
        return len(st) == 1 and len(ut) > 1 and st[0][2][2] not in function and \
            all(t[2][2] not in function for t in ut)
    lex = load_lex(lexp)
    applied = unmapped = 0
    with open(fu_path, encoding='utf-8') as fu, open(fs_path, encoding='utf-8') as fs, \
         open(outp, 'w', encoding='utf-8') as o:
        for lu, ls in zip(fu, fs):
            u, s = toks(lu), toks(ls)
            raw = [t.split('\x1e') for t in lu.rstrip('\n').split('\x1f') if len(t.split('\x1e')) >= 12]
            # map token index in u to index in raw (raw keeps whitespace tokens)
            uidx = [i for i, f in enumerate(raw) if f[2] != '空白']
            repl = {}
            for (i0, i1, j0, j1) in regions(u, s):
                pat = reading(u[i0:i1]) + '\t' + reading(s[j0:j1])
                if pat not in win and (pat in judged or not merge_content(u[i0:i1], s[j0:j1])):
                    continue
                new = []
                ok = True
                for b, e, f in s[j0:j1]:
                    cands = lex.get((f[1], f[2], f[3], f[4], f[5], f[6], f[7]))
                    if not cands:
                        ok = False
                        break
                    pick = next((c for c in cands if c[0] == f[9]), None) or \
                        next((c for c in cands if c[2] == f[10]), None) or cands[0]
                    new.append([f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], pick[1], pick[0], pick[2], ''])
                if not ok:
                    unmapped += 1
                    continue
                new[0][0] = raw[uidx[i0]][0]
                repl[uidx[i0]] = (uidx[i1 - 1], new)
                applied += 1
            out, i = [], 0
            while i < len(raw):
                if i in repl:
                    last, new = repl[i]
                    out.extend(new)
                    i = last + 1
                else:
                    out.append(raw[i])
                    i += 1
            o.write('\x1f'.join('\x1e'.join(f) for f in out) + '\n')
    print('regions replaced', applied, 'unmapped', unmapped, file=sys.stderr)


if __name__ == '__main__':
    {'patterns': cmd_patterns, 'packet': cmd_packet, 'apply': cmd_apply}[sys.argv[1]](sys.argv[2:])
