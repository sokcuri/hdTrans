#!/usr/bin/env python3
"""Counts the lines and distinct words that share a failure shape, to size a class before a fix.

  tools/count-class.py REGEX [FILE...] [--show N]
  tools/count-class.py --self-test

REGEX is a Python regular expression over the Japanese source lines; its group `key` (or the
whole match without one) names the word, e.g. `(?P<key>\\w+た)[？?]` for a past form before a
question mark. FILE defaults to $HDTRANS_CORPUS and $HDTRANS_REF_SAMPLE/ja.txt. Per file it
prints the matching lines and the distinct keys, then the keys by count and N example lines
(default 5) with their line numbers. Lines are split at LF only.
"""
import collections
import os
import re
import sys
import tempfile
from pathlib import Path


def count(pattern, lines):
    keys, examples, matched = collections.Counter(), [], 0
    for n, line in enumerate(lines, 1):
        found = [m.group("key") if "key" in pattern.groupindex else m.group(0) for m in pattern.finditer(line)]
        if found:
            matched += 1
            keys.update(found)
            examples.append((n, line))
    return matched, keys, examples


def lines_of(path):
    data = Path(path).read_bytes().decode("utf-8")
    return (data[:-1] if data.endswith("\n") else data).split("\n")


def self_test():
    pattern = re.compile(r"(?P<key>\w+た)[？?]")
    matched, keys, examples = count(pattern, ["できた？", "やった?やった？", "食べた。", "見た？"])
    assert (matched, len(keys), keys["やった"], examples[0][0]) == (3, 3, 2, 1), (matched, keys)
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "a.txt"
        p.write_bytes("a\rb\nc".encode())
        assert lines_of(p) == ["a\rb", "c"]
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    show = 5
    if "--show" in argv:
        i = argv.index("--show")
        show = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    if not argv:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    pattern = re.compile(argv[0])
    files = argv[1:] or [p for p in (os.environ.get("HDTRANS_CORPUS"),
                                     os.environ.get("HDTRANS_REF_SAMPLE") and
                                     os.path.join(os.environ["HDTRANS_REF_SAMPLE"], "ja.txt")) if p]
    if not files:
        print("no files: pass them or set HDTRANS_CORPUS / HDTRANS_REF_SAMPLE", file=sys.stderr)
        return 2
    for path in files:
        matched, keys, examples = count(pattern, lines_of(path))
        print(f"{path}: {matched} lines, {len(keys)} distinct keys")
        print("  " + "  ".join(f"{k} {n}" for k, n in keys.most_common(30)))
        for n, line in examples[:show]:
            print(f"  {n}: {line[:100]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
