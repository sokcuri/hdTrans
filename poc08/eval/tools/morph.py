#!/usr/bin/env python3
"""Korean morphemes for morpheme-level chrF (evaluation only; needs kiwipiepy in the eval venv).

  venv/bin/python morph.py < lines.txt > morphs.txt
One line in, one line out: the Kiwi morpheme forms (punctuation and symbols dropped), space-separated.
"""
import sys

from kiwipiepy import Kiwi

kiwi = Kiwi()
lines = sys.stdin.read().split("\n")
if lines and lines[-1] == "":
    lines.pop()
for toks in kiwi.tokenize(lines):
    sys.stdout.write(" ".join(t.form for t in toks if not t.tag.startswith("S") or t.tag in ("SL", "SN", "SH"))
                     + "\n")
