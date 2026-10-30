"""Shared paths and helpers for the TM channel tools (build-time only; nothing here ships)."""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile

HOME = '/Users/sokcuri/hdwork'
V08 = HOME + '/v08'
WORK = V08 + '/tm/work'
MEAS = V08 + '/tm/meas'
ENGINE = HOME + '/wt/fix-hare/build/mac/hdtrans'
DICTS = HOME + '/wt/fix-hare/dicts'
EVAL = V08 + '/eval'
LEXTOOLS = HOME + '/wt/poc08-lexicon/poc08/lexicon/tools'
EVALTOOLS = HOME + '/wt/poc08-eval/poc08/eval/tools'

# development corpora (private paths, kept outside the repo): TM_PATHS or V08/tm/paths.json
_P = json.load(open(os.environ.get('TM_PATHS', V08 + '/tm/paths.json'), encoding='utf-8'))
FICTION_DIR = _P['fiction_dir']
NOVEL_REVIEW = _P['fiction_review']
CORPORA = _P['corpora']


def fiction_files():
    works = [w.strip() for w in open(NOVEL_REVIEW + '/works.txt', encoding='utf-8') if w.strip()]
    return [os.path.join(FICTION_DIR, w) for w in works]


def eval_lines():
    """Every line of the frozen held-out sets (exclusion only; nothing is read for tuning)."""
    sys.path.insert(0, EVALTOOLS)
    spec = importlib.util.spec_from_file_location('e3_fresh', os.path.join(EVALTOOLS, 'e3_fresh.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    lines, _, _, _ = m.eval_pool()
    return {s.strip() for s in lines}


def study_pool():
    spec = importlib.util.spec_from_file_location('excluded', os.path.join(EVAL, 'scripts', 'excluded.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m.load()[0]


def run_engine(lines, layer_dir=None, dicts=DICTS, json_out=False, extra=None):
    """0.7.x output for lines (one per line, --each), with the zz-*.tsv files of layer_dir copied into the dicts."""
    env = dict(os.environ, HDTRANS_CACHE_DIR='off', HOME=tempfile.mkdtemp())
    d, tmp = dicts, None
    if layer_dir:
        tmp = tempfile.mkdtemp()
        d = os.path.join(tmp, 'dicts')
        shutil.copytree(dicts, d)
        for n in os.listdir(layer_dir):
            if n.startswith('zz-') and n.endswith('.tsv'):
                shutil.copy(os.path.join(layer_dir, n), d)
    args = [ENGINE, '--dicts', d, '--each'] + (['--json'] if json_out else []) + (extra or [])
    clean = [s.replace('\n', ' ') for s in lines]
    p = subprocess.run(args, input='\n'.join(clean) + '\n', capture_output=True, text=True, env=env)
    if tmp:
        shutil.rmtree(tmp, ignore_errors=True)
    if json_out:
        return json.loads(p.stdout), p.stderr
    out = p.stdout.split('\n')
    return out[:len(lines)] + [''] * max(0, len(lines) - len(out)), p.stderr


def read_tsv(path):
    with open(path, encoding='utf-8') as f:
        hdr = f.readline().rstrip('\n').split('\t')
        for line in f:
            p = line.rstrip('\n').split('\t')
            yield dict(zip(hdr, p + [''] * (len(hdr) - len(p))))
