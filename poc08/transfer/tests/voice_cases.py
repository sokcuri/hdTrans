#!/usr/bin/env python3
"""Cases of the voice rule table (data/decide/voice/cases.tsv, spec VC-*).

  voice_cases.py HDTRANS08_IR HD8DECIDE DATA_DIR [--verbose]

Each case is analysed (N → A → S → C → L), its first voice point is decided with the case's base class as 0.7.x's
reading, and the decision must be the case's class (fix: by the row the case names; keep: the base stays).
Exit 0 when every case passes.
"""
import json
import os
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    ir_bin, dec_bin, data = sys.argv[1:4]
    verbose = '--verbose' in sys.argv
    cases = []
    with open(os.path.join(data, 'decide', 'voice', 'cases.tsv'), encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#') or line.startswith('id\t'):
                continue
            rid, kind, ja, base, cls = line.split('\t')[:5]
            cases.append((rid, kind, ja, base, cls))
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, 'in.txt'), 'w', encoding='utf-8') as f:
            f.write(''.join(c[2] + '\n' for c in cases))
        env = dict(os.environ, HOME=d, HDTRANS_CACHE_DIR='off')
        with open(os.path.join(d, 'in.txt'), encoding='utf-8') as fin:
            subprocess.run([ir_bin, '--each', '--stages', d], stdin=fin, stdout=subprocess.DEVNULL, env=env, check=True)
        ir = os.path.join(d, 'ir.jsonl')
        dump = subprocess.run([dec_bin, '--data', data, '--dump'], stdin=open(ir, encoding='utf-8'),
                              capture_output=True, text=True, check=True).stdout
        first = {}
        for l in dump.splitlines():
            case, _, fam, src, _ = l.split('\t', 4)
            if fam == 'voice' and case not in first:
                first[case] = src
        with open(os.path.join(d, 'base.tsv'), 'w', encoding='utf-8') as f:
            for i, c in enumerate(cases, 1):
                if str(i) in first:
                    f.write('%d\t%s\t%s\n' % (i, first[str(i)], c[3]))
        out = subprocess.run([dec_bin, '--data', data, '--decide', os.path.join(d, 'base.tsv')],
                             stdin=open(ir, encoding='utf-8'), capture_output=True, text=True, check=True)
        if out.stderr.strip():
            print(out.stderr.strip())
            return 1
        got = {}
        for l in out.stdout.splitlines():
            o = json.loads(l)
            if o['family'] == 'voice' and o['case'] not in got and str(o['src']) == first.get(o['case']):
                rule = next((c[0][5:] for c in o['contrib'] if c[0].startswith('rule:')), '-')
                got[o['case']] = (o['class'], rule)
    fails = 0
    for i, (rid, kind, ja, base, cls) in enumerate(cases, 1):
        g = got.get(str(i))
        ok = g is not None and g[0] == cls and (kind == 'keep' or g[1] == rid)
        if not ok or verbose:
            print('%s %s %s: %s base=%s want=%s got=%s' % ('ok  ' if ok else 'FAIL', kind, rid, ja, base, cls,
                                                           '%s by %s' % g if g else 'no voice point'))
        fails += not ok
    print('voice cases: %d/%d' % (len(cases) - fails, len(cases)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
