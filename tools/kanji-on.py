#!/usr/bin/env python3
"""Writes src/core/dict/builtin/extra/kanji_on.tsv, the first on reading of each kanji of the
joyo and jinmeiyo lists, from the Unihan database (Unicode License v3, THIRD-PARTY-NOTICES.txt).

  tools/kanji-on.py UNIHAN_DIR [OUT]

UNIHAN_DIR holds Unihan_Readings.txt and Unihan_OtherMappings.txt. The reading is the first
katakana reading of kJapanese, written in hiragana. The table was made from Unihan of Unicode
18.0.0; the version of the files read is written into the table's first line.
"""
import re
import sys
from pathlib import Path


def fields(path, wanted):
    out = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line:
            continue
        code, field, value = line.split("\t", 2)
        if field in wanted:
            out.setdefault(chr(int(code[2:], 16)), {})[field] = value
    return out


def version(path):
    for line in Path(path).read_text(encoding="utf-8").splitlines()[:10]:
        m = re.match(r"# Unicode Version (\S+)", line)
        if m:
            return m.group(1)
    return "unknown"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "src/core/dict/builtin/extra/kanji_on.tsv"
    lists = fields(root / "Unihan_OtherMappings.txt", {"kJoyoKanji", "kJinmeiyoKanji"})
    readings = fields(root / "Unihan_Readings.txt", {"kJapanese"})
    lines = [f"# The first on reading of the joyo and jinmeiyo kanji, from Unihan of Unicode {version(root / 'Unihan_Readings.txt')} (tools/kanji-on.py)."]
    for ch in sorted(lists):
        for token in readings.get(ch, {}).get("kJapanese", "").split():
            if all("ァ" <= c <= "ヶ" for c in token):
                lines.append(f"{ch}\t{''.join(chr(ord(c) - 0x60) for c in token)}")
                break
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(lines) - 1} kanji", file=sys.stderr)


if __name__ == "__main__":
    main()
