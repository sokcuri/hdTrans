#!/usr/bin/env python3
"""GiNZA bunsetsu dependencies as silver (measurement and head-model training only, never shipped).

usage: ginza_silver.py < toks.jsonl > silver.jsonl
Reads the lines of a toks.jsonl and writes {doc, line, sents: [{buns: [[b, e, hb, he]...], heads: [...]}]}
with UTF-8 byte offsets: bunsetsu span [b, e) and its head token [hb, he); heads index the bunsetsu of the
same sentence, -1 for the root.
"""
import json
import sys

import ginza
import spacy


def main():
    nlp = spacy.load('ja_ginza')
    rows = [json.loads(l) for l in sys.stdin]
    srcs = [r['src'] for r in rows]
    for r, doc in zip(rows, nlp.pipe(srcs, batch_size=64)):
        src = r['src']
        cb = [0]
        for ch in src:
            cb.append(cb[-1] + len(ch.encode()))
        out = []
        for sent in doc.sents:
            spans = ginza.bunsetu_spans(sent)
            idx = {}
            for k, s in enumerate(spans):
                for t in s:
                    idx[t.i] = k
            buns, heads = [], []
            for k, s in enumerate(spans):
                root = None
                for t in s:
                    if t.dep_ == 'ROOT' or t.head.i not in idx or idx[t.head.i] != k:
                        root = t
                        break
                if root is None:
                    root = s[0]
                hk = -1 if root.dep_ == 'ROOT' or root.head.i not in idx else idx[root.head.i]
                if hk == k:
                    hk = -1
                buns.append([cb[s.start_char], cb[s.end_char], cb[root.idx], cb[root.idx + len(root.text)]])
                heads.append(hk)
            out.append({'buns': buns, 'heads': heads})
        sys.stdout.write(json.dumps({'doc': r['doc'], 'line': r['line'], 'sents': out}, ensure_ascii=False) + '\n')


if __name__ == '__main__':
    main()
