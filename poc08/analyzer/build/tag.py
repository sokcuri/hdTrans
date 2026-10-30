#!/usr/bin/env python3
"""Tag training corpora with MeCab + UniDic (the teacher) into the silver format.

usage: tag.py [--norm HD8ANA] [--lead] OUT EXCLUDE_LIST... -- CORPUS...
  --lead: tag 。 + line and drop that first token, so the teacher reads every line as a sentence that
          follows another one (MeCab with UniDic misreads some line-initial words: ずっと as ず + っと).
  --norm: pass every line through `HD8ANA norm` (layout normalization) before tagging, so the teacher
          sees the same text as the analyzer.
  CORPUS: a text file (one line per sentence) or a .jsonl.gz file whose rows have `source_ja`.
  EXCLUDE_LIST: files whose stripped lines (or `ja` column of a TSV with a header) are left out.

Output: one sentence per line; tokens joined by \\x1f, fields by \\x1e:
  ws surface pos1 pos2 pos3 pos4 cType cForm lForm lemma kana unk
`ws` is the whitespace MeCab skipped before the token, so ''.join(ws+surface) is the input line.
"""
import csv, gzip, json, multiprocessing as mp, os, subprocess, sys

TAGGER = None


def _init():
    global TAGGER
    import fugashi, unidic
    TAGGER = fugashi.Tagger('-d ' + unidic.DICDIR + ' -r ' + unidic.DICDIR + '/mecabrc')


def _f(x):
    return x if x not in (None, '') else '*'


LEAD = '。'


def tag_line(line, lead=False):
    toks = []
    words = list(TAGGER(LEAD + line)) if lead else list(TAGGER(line))
    if lead:
        if not words or words[0].surface != LEAD or words[0].white_space:
            return None
        words = words[1:]
    for w in words:
        f = w.feature
        toks.append('\x1e'.join([w.white_space, w.surface, _f(f.pos1), _f(f.pos2), _f(f.pos3), _f(f.pos4),
                                 _f(f.cType), _f(f.cForm), _f(f.lForm), _f(f.lemma), _f(f.kana),
                                 '1' if w.is_unk else '']))
    return '\x1f'.join(toks)


def _tag_lead(line):
    return tag_line(line, True)


def read_corpus(path):
    if path.endswith('.jsonl.gz'):
        with gzip.open(path, 'rt', encoding='utf-8') as fh:
            for row in fh:
                s = json.loads(row).get('source_ja') or ''
                for piece in s.split('\n'):
                    yield piece
    else:
        with open(path, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                yield line.rstrip('\n')


def read_exclude(path):
    out = set()
    with open(path, encoding='utf-8', errors='replace') as fh:
        first = fh.readline()
        if path.endswith('.tsv') and '\tja\t' in '\t' + first:
            cols = first.rstrip('\n').split('\t')
            j = cols.index('ja')
            for line in fh:
                p = line.rstrip('\n').split('\t')
                if len(p) > j:
                    out.add(p[j].strip())
        else:
            out.add(first.strip())
            for line in fh:
                out.add(line.strip())
    return out


def main():
    argv = sys.argv[1:]
    norm = None
    lead = False
    while argv and argv[0].startswith('--'):
        if argv[0] == '--norm':
            norm = argv[1]
            argv = argv[2:]
        elif argv[0] == '--lead':
            lead = True
            argv = argv[1:]
        else:
            break
    out = argv[0]
    sep = argv.index('--')
    excl = set()
    for p in argv[1:sep]:
        excl |= read_exclude(p)
    lines = []
    seen = set()
    for p in argv[sep + 1:]:
        for l in read_corpus(p):
            l = l.replace('\r', '').replace('\t', ' ')
            k = l.strip()
            if not k or k in excl or l in seen:
                continue
            seen.add(l)
            lines.append(l)
    if norm:
        res = subprocess.run([norm, 'norm'], input='\n'.join(lines) + '\n', capture_output=True, text=True, check=True)
        normed = res.stdout.split('\n')[:len(lines)]
        assert len(normed) == len(lines)
        dedup, seen = [], set()
        for l in normed:
            if l not in seen:
                seen.add(l)
                dedup.append(l)
        lines = dedup
    with mp.Pool(min(24, os.cpu_count() or 1), initializer=_init) as pool, open(out, 'w', encoding='utf-8') as o:
        dropped = 0
        for t in pool.imap(_tag_lead if lead else tag_line, lines, chunksize=2000):
            if t is None:
                dropped += 1
                continue
            o.write(t + '\n')
        if dropped:
            print(dropped, 'lines dropped: the lead merged with them', file=sys.stderr)
    print(len(lines), 'lines;', len(excl), 'excluded keys', file=sys.stderr)


if __name__ == '__main__':
    main()
