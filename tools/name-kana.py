#!/usr/bin/env python3
"""Writes src/core/dict/builtin/extra/name_kana.tsv, the kana that the Japanese names of the name
dictionaries give a kanji at the head and at the tail of a two-kanji name.

  tools/name-kana.py [DICTS] [OUT]
  tools/name-kana.py --check [DICTS]

DICTS (default dicts/) holds names.*.tsv. The person rows whose key is two kanji and whose reading
column is hiragana are split between their two kanji: first every split counts alike, then each name
takes the split that the head readings of its first kanji and the tail readings of its second make
most likely, six rounds over. A kanji's head (tail) is the reading it takes most at the head (tail),
when three names or more and 40% of its names take it (千 ち, 莉 り: 千莉 ちり). Run it again when the
name dictionaries change; --check exits 1 when the table differs (ctest dicts.name-kana).
"""
import collections
import re
import sys
from pathlib import Path

# Kana that go on with the mora before them: no split starts with one.
SMALL = set("ぁぃぅぇぉゃゅょゎっんー")
ROUNDS = 6
LEAST = 3
SHARE = 0.4


def splits(r):
    for i in range(1, len(r)):
        if r[i] not in SMALL:
            yield r[:i], r[i:]


def main():
    here = Path(__file__).resolve().parent.parent
    args = sys.argv[1:]
    check = args[:1] == ["--check"]
    if check:
        args = args[1:]
    dicts = Path(args[0]) if args else here / "dicts"
    out = Path(args[1]) if len(args) > 1 else here / "src/core/dict/builtin/extra/name_kana.tsv"
    rows = []
    for path in sorted(dicts.glob("names.*.tsv")):
        for line in path.read_text(encoding="utf-8").splitlines():
            cols = line.split("\t")
            if len(cols) < 12 or "QPN" not in cols[3]:
                continue
            key, reading = cols[0], cols[11].replace(" ", "")
            if re.fullmatch(r"[一-鿿]{2}", key) and re.fullmatch(r"[ぁ-ゖ]{2,8}", reading):
                rows.append((key, reading))
    rows.sort()
    head = collections.defaultdict(collections.Counter)
    tail = collections.defaultdict(collections.Counter)
    for key, r in rows:
        ss = list(splits(r))
        for a, b in ss:
            head[key[0]][a] += 1 / len(ss)
            tail[key[1]][b] += 1 / len(ss)
    for _ in range(ROUNDS):
        h = collections.defaultdict(collections.Counter)
        t = collections.defaultdict(collections.Counter)
        totals_h = {k: sum(v.values()) for k, v in head.items()}
        totals_t = {k: sum(v.values()) for k, v in tail.items()}
        for key, r in rows:
            best, score = None, -1.0
            for a, b in splits(r):
                s = (head[key[0]][a] / (totals_h.get(key[0]) or 1)) * (tail[key[1]][b] / (totals_t.get(key[1]) or 1))
                if s > score:
                    best, score = (a, b), s
            if best:
                h[key[0]][best[0]] += 1
                t[key[1]][best[1]] += 1
        head, tail = h, t

    def pick(c):
        if not c:
            return ""
        best = sorted(c.items(), key=lambda kv: (-kv[1], kv[0]))[0]
        return best[0] if best[1] >= LEAST and best[1] / sum(c.values()) >= SHARE else ""

    lines = ["# The kana that the Japanese names of the name dictionaries give a kanji at the head and the tail of a two-kanji name (tools/name-kana.py): KANJI <TAB> HEAD <TAB> TAIL."]
    for kanji in sorted(set(head) | set(tail)):
        a, b = pick(head.get(kanji, {})), pick(tail.get(kanji, {}))
        if a or b:
            lines.append(f"{kanji}\t{a}\t{b}")
    text = "\n".join(lines) + "\n"
    if check:
        if out.read_text(encoding="utf-8") != text:
            print(f"{out} is stale: run tools/name-kana.py", file=sys.stderr)
            return 1
        return 0
    out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
