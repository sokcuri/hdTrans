"""Shared helpers for the phrase-based layer's build tools (build time only, never runtime).

Korean side: Kiwi morphemes cut back onto the surface string, so every piece is a substring of the
original text. A piece that continues a word starts with '##'; detok joins it to the previous piece.
Japanese side: the W1 analyzer (hd8ana run CORE --format tsv), so the runtime decoder sees the same tokens.
"""
import os
import re
import subprocess
import unicodedata

JOIN = "##"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HD8ANA = os.environ.get("HD8ANA", os.path.join(ROOT, "build", "mac", "poc08", "analyzer", "hd8ana"))
CORE = os.environ.get("HD8_ANALYZER", "/Users/sokcuri/hdwork/v08/analyzer/core.bin")

_WS = re.compile(r"\s+")


def norm_key(s):
    """Key for overlap checks: NFKC, lower case, no whitespace, no punctuation or symbols."""
    s = unicodedata.normalize("NFKC", s).lower()
    return "".join(c for c in s if not c.isspace() and unicodedata.category(c)[0] not in "PSZC")


def sentence_keys(s, min_len=8):
    """The whole-line key plus the key of every sentence piece of at least min_len characters."""
    keys = set()
    k = norm_key(s)
    if k:
        keys.add(k)
    for part in re.split(r"[。！？!?\n「」『』]", s):
        k = norm_key(part)
        if len(k) >= min_len:
            keys.add(k)
    return keys


# ---------------------------------------------------------------- Korean

def ko_pieces_from_tokens(text, toks):
    """Cut text at morpheme starts that do not overlap the previous morpheme; spaces end words."""
    words = []
    for m in re.finditer(r"\S+", text):
        words.append((m.start(), m.end()))
    cuts = set()
    prev_end = -1
    for t in toks:
        s, e = t.start, t.start + t.len
        if prev_end >= 0 and s >= prev_end:
            cuts.add(s)
        prev_end = max(prev_end, e)
    out = []
    for ws, we in words:
        pos = [ws] + sorted(c for c in cuts if ws < c < we) + [we]
        for i in range(len(pos) - 1):
            piece = text[pos[i]:pos[i + 1]]
            out.append(piece if i == 0 else JOIN + piece)
    return out


class KoTok:
    def __init__(self, workers=8):
        from kiwipiepy import Kiwi
        self.kiwi = Kiwi(num_workers=workers)

    def pieces(self, lines):
        lines = [_WS.sub(" ", unicodedata.normalize("NFC", l)).strip() for l in lines]
        res = self.kiwi.tokenize(lines)
        return [ko_pieces_from_tokens(t, toks) for t, toks in zip(lines, res)]


def ko_detok(pieces):
    s = " ".join(pieces)
    return s.replace(" " + JOIN, "").replace(JOIN, "")


# ---------------------------------------------------------------- Japanese

def ja_tokens(lines, core=CORE, binary=HD8ANA):
    """[(surfs, lemmas)] per line from the W1 analyzer, core units only (no overlay)."""
    clean = [l.replace("\t", " ").replace("\r", "") for l in lines]
    p = subprocess.run([binary, "run", core, "--format", "tsv"], input="\n".join(clean) + "\n",
                       capture_output=True, text=True, check=True)
    out, surf, lem = [], [], []
    for row in p.stdout.split("\n"):
        if row == "EOS":
            out.append((surf, lem))
            surf, lem = [], []
            continue
        if not row:
            continue
        f = row.split("\t")
        s = f[0].replace(" ", "　")
        if not s.strip():
            continue
        surf.append(s)
        l = f[1] if len(f) > 1 and f[1] and f[1] != "*" else s
        lem.append(l.replace(" ", "　"))
    assert len(out) == len(lines), (len(out), len(lines))
    return out
