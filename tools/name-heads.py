#!/usr/bin/env python3
"""Writes src/core/dict/builtin/extra/name_heads.tsv, the kana reading that the Japanese names of
the name dictionaries give a kanji they start with.

  tools/name-heads.py [DICTS] [OUT]

DICTS (default dicts/) holds names.*.tsv. For each kanji, the person rows whose key is two kanji
or more starting with it and whose reading column is hiragana are read; the head is the longest
reading prefix, ending on a whole mora, that at least 70% of three or more such readings share
(塩 しお: 塩田 しおた, 塩見 しおみ). Run it again when the name dictionaries change.
"""
import collections
import re
import sys
from pathlib import Path

# Kana that go on with the mora before them: a head does not end before one.
SMALL = set("ぁぃぅぇぉゃゅょゎっんー")
SHARE = 0.7
LEAST = 3


def main():
    here = Path(__file__).resolve().parent.parent
    dicts = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "dicts"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "src/core/dict/builtin/extra/name_heads.tsv"
    readings = collections.defaultdict(list)
    for path in sorted(dicts.glob("names.*.tsv")):
        for line in path.read_text(encoding="utf-8").splitlines():
            cols = line.split("\t")
            if len(cols) < 12 or "QPN" not in cols[3]:
                continue
            key, reading = cols[0], cols[11].replace(" ", "")
            if re.fullmatch(r"[一-鿿々]{2,}", key) and re.fullmatch(r"[ぁ-ゖ]+", reading):
                readings[key[0]].append(reading)
    lines = ["# The kana reading that the Japanese names of the name dictionaries give the kanji they start with (tools/name-heads.py)."]
    for kanji in sorted(readings):
        rs = readings[kanji]
        if len(rs) < LEAST:
            continue
        head = ""
        for n in range(1, 6):
            counts = collections.Counter(r[:n] for r in rs if len(r) > n and r[n] not in SMALL)
            if not counts:
                break
            prefix, count = counts.most_common(1)[0]
            if count / len(rs) >= SHARE:
                head = prefix
        if head:
            lines.append(f"{kanji}\t{head}")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(lines) - 1} kanji", file=sys.stderr)


if __name__ == "__main__":
    main()
