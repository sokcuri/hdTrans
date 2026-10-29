#!/usr/bin/env python3
"""Lists dictionary rows that shadow the reading of ordinary words.

A row shadows when its key can be split end to end into keys of the other dictionary files
(and a few joining kana or spaces), and translating the key in a sentence frame with and without the
row's files gives different Korean. Such a row hides the grammar or the words it is made of;
it is right only for a real name, title or set phrase (skills/check-shadow-rows).

  tools/shadow-rows.py HDTRANS DICTS OUT.tsv [--files GLOB] [--frame TEXT] [--all]

  --files GLOB   the files whose rows are checked, matched against file names in DICTS
                 (default names.*.tsv); they are left out for the second reading.
  --frame TEXT   the sentence around the key, {} standing for it (default {}だった。).
  --all          write every changed row; by default only the likely shadows: rows whose key
                 holds hiragana, or work titles (QWK) whose key holds no katakana (全然大丈夫),
                 and whose two readings share few Hangul syllables or, for a title, whose target
                 adds a comma that the key lacks (俺俺 나야, 나) or is a sentence (きっと大丈夫).

OUT.tsv: file, id, key, class, sem, target, with, without, similarity.
"""
import argparse
import fnmatch
import os
import re
import shutil
import subprocess
import sys
import tempfile

JOIN = set('のをにはがとでへもないきくしたや')
KANA = re.compile('[ぁ-ゖ]')
KATAKANA = re.compile('[ァ-ヺー]')
SPACE = set(' \u3000')
COMMA = re.compile('[,、，]')
SENTENCE = re.compile(r'(아|어|해|요|야)(였다|이었다)\.?$')
JAPANESE = re.compile('[ぁ-ゖァ-ヺ一-鿿]')
FRAME_END = re.compile(r'(이)?었다\.?$|였다\.?$')


def keys_of(dicts, skip):
    keys = set()
    for name in os.listdir(dicts):
        if not name.endswith('.tsv') or skip(name):
            continue
        for line in open(os.path.join(dicts, name), encoding='utf-8'):
            key = line.split('\t', 1)[0]
            if key and not key.startswith('#') and len(key) <= 12:
                keys.add(key)
    return keys


def splits(key, words):
    """Whether key is two or more words of words, joined by kana of JOIN or spaces. A
    one-character word counts only after a joining kana or a space (優しい男, 俺 俺) or right
    after the same word (俺俺)."""
    n = len(key)
    parts = [-1] * (n + 1)
    parts[0] = 0
    for i in range(n):
        if parts[i] < 0:
            continue
        if key[i] in SPACE:
            parts[i + 1] = max(parts[i + 1], parts[i])
            continue
        if key[i] in JOIN:
            parts[i + 1] = max(parts[i + 1], parts[i] + 1)
        for j in range(i + 1, min(n, i + 12) + 1):
            single = (i > 0 and (key[i - 1] in JOIN or key[i - 1] in SPACE or key[i - 1] == key[i])) or key[j:j + 1] == key[i]
            if (j - i >= 2 or single) and key[i:j] in words:
                parts[j] = max(parts[j], parts[i] + 1)
    return parts[n] >= 2


def syllables(text):
    return {c for c in FRAME_END.sub('', text) if '가' <= c <= '힣'}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('hdtrans')
    ap.add_argument('dicts')
    ap.add_argument('out')
    ap.add_argument('--files', default='names.*.tsv')
    ap.add_argument('--frame', default='{}だった。')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()

    checked = lambda name: fnmatch.fnmatch(name, a.files)
    words = keys_of(a.dicts, checked)
    rows = []
    for name in sorted(os.listdir(a.dicts)):
        if not checked(name):
            continue
        for line in open(os.path.join(a.dicts, name), encoding='utf-8'):
            c = line.rstrip('\n').split('\t')
            if len(c) < 3 or not c[0] or c[0].startswith('#'):
                continue
            key = c[0]
            if any(ch.isascii() and ch != ' ' for ch in key) or key in words or not splits(key, words):
                continue
            rows.append((name, c[-1], key, c[2], c[3] if len(c) > 3 else '', c[1]))
    print(f'{len(rows)} rows split into words', file=sys.stderr)

    work = tempfile.mkdtemp()
    without_dir = os.path.join(work, 'dicts')
    shutil.copytree(a.dicts, without_dir, ignore=lambda d, names: [n for n in names if checked(n)])
    text = '\n'.join(a.frame.replace('{}', r[2]) for r in rows) + '\n'
    env = dict(os.environ, HDTRANS_CACHE_DIR='off')

    def read(d):
        p = subprocess.run([a.hdtrans, '--dicts', d, '--each'], input=text, capture_output=True, text=True, env=env)
        if p.returncode != 0:
            sys.exit(p.stderr)
        return p.stdout.split('\n')

    with_rows, without_rows = read(a.dicts), read(without_dir)
    shutil.rmtree(work)
    n = 0
    with open(a.out, 'w', encoding='utf-8') as out:
        out.write('file\tid\tkey\tclass\tsem\ttarget\twith\twithout\tsimilarity\n')
        for r, w, wo in zip(rows, with_rows, without_rows):
            if w == wo:
                continue
            sw, swo = syllables(w), syllables(wo)
            sim = len(sw & swo) / len(sw | swo) if sw and swo else 0.0
            title = r[4].startswith('QWK') and not KATAKANA.search(r[2])
            # A title that adds a comma the key lacks (俺俺 나야, 나) or that is a whole sentence,
            # so that the frame's copula follows a predicate (きっと大丈夫 분명 괜찮아였다), is no
            # reading of the key.
            added = title and ((',' in r[5] and not COMMA.search(r[2])) or (SENTENCE.search(w) and not SENTENCE.search(wo)))
            if not a.all and not ((KANA.search(r[2]) or title) and (sim < 0.4 or added) and swo and not JAPANESE.search(wo)):
                continue
            out.write('\t'.join([*r, w, wo, f'{sim:.2f}']) + '\n')
            n += 1
    print(f'{n} rows written to {a.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
