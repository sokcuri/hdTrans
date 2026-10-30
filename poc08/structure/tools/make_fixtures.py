#!/usr/bin/env python3
"""Regenerate tests/fixtures/*.toks.jsonl from the ja column of tests/cases/*.cases.tsv (teacher tokens).

usage: make_fixtures.py   (run with a Python that has fugashi and unidic)
"""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def main():
    os.makedirs(os.path.join(ROOT, 'tests', 'fixtures'), exist_ok=True)
    for path in sorted(glob.glob(os.path.join(ROOT, 'tests', 'cases', '*.cases.tsv'))):
        seen = []
        hdr = None
        for line in open(path, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            p = line.rstrip('\n').split('\t')
            if hdr is None:
                hdr = p
                continue
            ja = p[hdr.index('ja')]
            if ja not in seen:
                seen.append(ja)
        res = subprocess.run([sys.executable, os.path.join(HERE, 'teacher_toks.py'), '--doc', 'case'],
                             input='\n'.join(seen) + '\n', capture_output=True, text=True, check=True)
        name = os.path.basename(path).replace('.cases.tsv', '.toks.jsonl')
        with open(os.path.join(ROOT, 'tests', 'fixtures', name), 'w', encoding='utf-8') as fh:
            fh.write(res.stdout)
        print(name, len(seen))


if __name__ == '__main__':
    main()
