#!/usr/bin/env python3
"""Tag lines with Sudachi (split mode A, SudachiDict core) into the tag.py silver format.

usage: sudachi_tag.py IN_LINES OUT

Fields per token: ws surface pos1 pos2 pos3 pos4 cType cForm lForm lemma kana unk. lemma is Sudachi's
normalized form, kana its reading, lForm is '*'; whitespace tokens become the ws of the next token.
Sudachi is a second teacher for the ensemble (build/ensemble.py); nothing of it is shipped.
"""
import multiprocessing as mp, os, sys

TOK = None


def _init():
    global TOK
    from sudachipy import dictionary
    TOK = dictionary.Dictionary(dict='core').create()


def _f(x):
    return x if x not in (None, '') else '*'


def tag_line(line):
    from sudachipy import tokenizer
    A = tokenizer.Tokenizer.SplitMode.A
    out, ws = [], ''
    for m in TOK.tokenize(line, A):
        s = m.surface()
        if s == '':  # Sudachi splits … into … and empty tokens
            continue
        p = m.part_of_speech()
        if p[0] == '空白' and s.strip() == '':
            ws += s
            continue
        out.append('\x1e'.join([ws, s, _f(p[0]), _f(p[1]), _f(p[2]), _f(p[3]), _f(p[4]), _f(p[5]), '*',
                                _f(m.normalized_form()), _f(m.reading_form()), '1' if m.is_oov() else '']))
        ws = ''
    return '\x1f'.join(out)


def main():
    lines = [l.rstrip('\n') for l in open(sys.argv[1], encoding='utf-8')]
    with mp.Pool(min(24, os.cpu_count() or 1), initializer=_init) as pool, open(sys.argv[2], 'w', encoding='utf-8') as o:
        for t in pool.imap(tag_line, lines, chunksize=2000):
            o.write(t + '\n')


if __name__ == '__main__':
    main()
