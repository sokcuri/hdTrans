#!/usr/bin/env python3
"""Lists dictionary rows that shadow the reading of ordinary words.

A row shadows when its key can be split end to end into keys of the other dictionary files
(and a few joining kana or spaces), and translating the key in a sentence frame with and without the
row's files gives different Korean. Such a row hides the grammar or the words it is made of;
it is right only for a real name, title or set phrase (skills/check-shadow-rows).

  tools/shadow-rows.py HDTRANS DICTS OUT.tsv [--files GLOB] [--frame TEXT] [--all]
  tools/shadow-rows.py HDTRANS DICTS OUT.tsv --proper [--files GLOB] [--text FILE]

  --files GLOB   the files whose rows are checked, matched against file names in DICTS
                 (default names.*.tsv, with --proper base.tsv); they are left out for the second
                 reading.
  --frame TEXT   the sentence around the key, {} standing for it (default {}だった。).
  --all          write every changed row; by default only the likely shadows: rows whose key
                 holds hiragana, or work titles (QWK) whose key holds no katakana (全然大丈夫),
                 and whose two readings share few Hangul syllables or, for a title, whose target
                 adds a comma that the key lacks (俺俺 나야, 나) or is a sentence (きっと大丈夫).
  --proper       check only the proper noun rows of the files (N130 with the Q code of a name, not
                 QA of an animal or plant); only those rows are left out for the second reading.
                 The likely shadows are then rows whose key holds hiragana, is a word of another
                 row (下品), is the stem of a word with okurigana (可愛 of 可愛い, 湯上 of 湯上がり) or
                 splits into words (学園前, 台の下).
  --text FILE    read the lines of FILE that hold the key of a likely shadow (of any checked row
                 with --all) instead of the frame, and charge each line whose reading changes to
                 the longest such key whose target leaves it; every row with such a line is
                 written, most lines first (judge them by the lines).

OUT.tsv: file, id, key, class, sem, target, with, without, similarity; with --text: file, id, key,
class, sem, target, lines, then the first line's text, with and without.
"""
import argparse
import collections
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


def proper(c):
    """Whether the columns of a row are a proper noun row: N130 with the Q code of a name (QPN, QCT, QID), not QA."""
    sem = c[3] if len(c) > 3 else ''
    return c[2] == 'N130' and any(sem[i] == 'Q' and sem[i + 1:i + 2] != 'A' for i in range(0, len(sem) - 2, 3))


def proper_words(dicts):
    """The keys of every row that is no proper noun row."""
    keys = set()
    for name in os.listdir(dicts):
        if not name.endswith('.tsv'):
            continue
        for line in open(os.path.join(dicts, name), encoding='utf-8'):
            c = line.rstrip('\n').split('\t')
            if len(c) > 2 and c[0] and not c[0].startswith('#') and c[0] != 'key' and not proper(c) and len(c[0]) <= 12:
                keys.add(c[0])
    return keys


def syllables(text):
    return {c for c in FRAME_END.sub('', text) if '가' <= c <= '힣'}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('hdtrans')
    ap.add_argument('dicts')
    ap.add_argument('out')
    ap.add_argument('--files')
    ap.add_argument('--frame', default='{}だった。')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--proper', action='store_true')
    ap.add_argument('--text')
    a = ap.parse_args()
    if a.files is None:
        a.files = 'base.tsv' if a.proper else 'names.*.tsv'
    if a.proper:
        return check_proper(a)

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


def check_proper(a):
    checked = lambda name: fnmatch.fnmatch(name, a.files)
    words = proper_words(a.dicts)
    # The stems of the words with okurigana: the kanji part before the hiragana that ends them.
    stems = {re.sub('[ぁ-ゖ]+$', '', w) for w in words if KANA.search(w[-1:]) and re.search('[一-鿿]', w)}
    rows = []
    for name in sorted(os.listdir(a.dicts)):
        if not checked(name):
            continue
        for line in open(os.path.join(a.dicts, name), encoding='utf-8'):
            c = line.rstrip('\n').split('\t')
            if len(c) < 4 or not c[0] or c[0].startswith('#') or not proper(c):
                continue
            key = c[0]
            rid = c[9] if name == 'base.tsv' and len(c) > 9 else c[-1]
            if any(ch.isascii() and ch != ' ' for ch in key):
                continue
            likely = KANA.search(key) or key in words or key in stems or splits(key, words)
            if a.all or likely:
                rows.append((name, rid, key, c[2], c[3], c[1]))
    print(f'{len(rows)} proper rows checked', file=sys.stderr)

    work = tempfile.mkdtemp()
    without_dir = os.path.join(work, 'dicts')
    shutil.copytree(a.dicts, without_dir)
    for name in os.listdir(without_dir):
        if checked(name):
            path = os.path.join(without_dir, name)
            kept = [l for l in open(path, encoding='utf-8').read().split('\n') if not proper(l.split('\t') + [''] * 3)]
            open(path, 'w', encoding='utf-8').write('\n'.join(kept))
    env = dict(os.environ, HDTRANS_CACHE_DIR='off')

    def read(d, text):
        p = subprocess.run([a.hdtrans, '--dicts', d, '--each'], input=text, capture_output=True, text=True, env=env)
        if p.returncode != 0:
            sys.exit(p.stderr)
        return p.stdout.split('\n')

    if a.text:
        by_key = collections.defaultdict(list)
        for r in rows:
            by_key[r[2]].append(r)
        lines = []
        for line in open(a.text, encoding='utf-8').read().split('\n'):
            if line and any(line[i:j] in by_key for i in range(len(line)) for j in range(i + 1, min(len(line), i + 12) + 1)):
                lines.append(line)
        text = '\n'.join(lines) + '\n'
        with_rows, without_rows = read(a.dicts, text), read(without_dir, text)
        shutil.rmtree(work)
        charged = collections.defaultdict(list)
        for line, w, wo in zip(lines, with_rows, without_rows):
            if w == wo:
                continue
            ws, wos = w.replace(' ', ''), wo.replace(' ', '')
            hits = [r for i in range(len(line)) for j in range(i + 1, min(len(line), i + 12) + 1) for r in by_key.get(line[i:j], ())
                    if r[5].replace(' ', '') and ws.count(r[5].replace(' ', '')) > wos.count(r[5].replace(' ', ''))]
            hits = [h for h in hits if not any(h[2] != g[2] and h[2] in g[2] for g in hits)]
            for h in dict.fromkeys(hits):
                charged[h].append((line, w, wo))
        with open(a.out, 'w', encoding='utf-8') as out:
            out.write('file\tid\tkey\tclass\tsem\ttarget\tlines\ttext\twith\twithout\n')
            for r, seen in sorted(charged.items(), key=lambda kv: -len(kv[1])):
                out.write('\t'.join([*r, str(len(seen)), *seen[0]]) + '\n')
        print(f'{len(charged)} rows written to {a.out}', file=sys.stderr)
        return

    text = '\n'.join(a.frame.replace('{}', r[2]) for r in rows) + '\n'
    with_rows, without_rows = read(a.dicts, text), read(without_dir, text)
    shutil.rmtree(work)
    n = 0
    with open(a.out, 'w', encoding='utf-8') as out:
        out.write('file\tid\tkey\tclass\tsem\ttarget\twith\twithout\tsimilarity\n')
        for r, w, wo in zip(rows, with_rows, without_rows):
            if w == wo:
                continue
            sw, swo = syllables(w), syllables(wo)
            sim = len(sw & swo) / len(sw | swo) if sw and swo else 0.0
            out.write('\t'.join([*r, w, wo, f'{sim:.2f}']) + '\n')
            n += 1
    print(f'{n} rows written to {a.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
