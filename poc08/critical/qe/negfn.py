"""The negation-function classifier's table (data/decide/negfn.tsv, written by tools/train_negfn.py), scored in
Python the same way the C++ deciders score it (decide.cpp Decide): softmax over intercept + feature weights."""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, '..', 'data', 'decide', 'negfn.tsv')
REAL = os.path.join(HERE, '..', 'data', 'decide', 'negreal.tsv')
VOICE = os.path.join(HERE, '..', 'data', 'decide', 'voicegate.tsv')


class Table:
    def __init__(self, path=DEFAULT):
        self.cls, self.bias, self.w = [], {}, {}
        for l in open(path, encoding='utf-8'):
            if l.startswith('#') or not l.strip():
                continue
            c = l.rstrip('\n').split('\t')
            if c[0] == 'classes':
                self.cls = c[1:]
            elif c[0] == 'bias':
                self.bias[c[1]] = float(c[2])
            else:
                self.w.setdefault(c[0], {})[c[1]] = float(c[2])

    def probs(self, feats):
        z = {k: self.bias.get(k, 0.0) for k in self.cls}
        for f in feats:
            for k, v in self.w.get(f, {}).items():
                z[k] += v
        m = max(z.values())
        e = {k: math.exp(v - m) for k, v in z.items()}
        s = sum(e.values())
        return {k: v / s for k, v in e.items()}
