#!/usr/bin/env python3
"""Needles of a change: text that every input line whose translation the change can move holds.

  tools/affected.py [--repo DIR] BASE_REF [HEAD_REF]
  tools/affected.py --dicts BASE_DICTS HEAD_DICTS
  tools/affected.py --self-test

Prints one needle per line, or `*` alone when the change can reach any line: a change outside
dicts/ (engine code, builtin tables, rule globals, build files), an ending row, or a plain row whose
key is a pattern (`end`, `pat`). Files that do not reach the engine (tests/, docs/, spec/, tools/,
skills/, examples/, web/, *.md, src/core/rules/program.hash) are left out. Without HEAD_REF the work
tree of the repository (default: this checkout) is compared, untracked dicts/ files included.

A row of dicts/ that was added, removed or edited gives its key and reading. A local rule that such
a row carries also gives the keys of every row that names the same rule number, since they share
one body. An ending row (pos S5.., T5.., U5..) is matched after a stem, also under a placeholder
key (V, I, D), so it reaches every line, and so does a rule that an ending row names. A verb or
adjective row of a plain file gives its stem (the key without its last kana, or without する or
来る). A kanji key also gives the kana spellings that the kana layer maps to it. Needles and lines
are compared folded (fold()); a kanji run of a needle that also holds kana matches a short run of
hiragana too (pattern()), since the engine reads hiragana as a kanji spelling. A needle of kanji
alone is matched as it stands: its kana spellings outside the kana layer are not covered.

Lines that hold none of the needles keep their translation in each-line mode (one session per
line): the engine looks a row up only where its key stands in the text. In document mode a name or
sense read in one line can reach other lines, so tools/sets.py translates whole documents again.
Exit 0, 2 on invalid input.
"""
import collections
import re
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

NO_EFFECT = re.compile(r"^(tests|docs|spec|tools|skills|examples|web|\.github|\.claude)/|\.md$|^LICENSE|"
                       r"^src/core/rules/program\.hash$|^\.gitignore$|^\.gitattributes$")
STRIP = str.maketrans("", "", "ーｰ～〜~・ \t　")
KANJI = "\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff々〆ヵヶ"
RUBY = re.compile(f"([{KANJI}])[(（《][\u3041-\u30ff]+[)）》]")
KANJI_RUN = re.compile(f"[{KANJI}]+")


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(2)


def fold(text):
    """Lowers what the engine may read as the same text to one form: no ruby reading in brackets
    after a kanji, NFKC, lower case, hiragana for katakana, no long-vowel or wave marks, middle dots
    or spaces, a doubled kanji for 々. Folding both sides keeps every substring a substring."""
    text = unicodedata.normalize("NFKC", RUBY.sub(r"\1", text)).lower()
    text = "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text)
    text = re.sub(r"(.)々", r"\1\1", text)
    return text.translate(STRIP)


def pattern(needle):
    """A regular expression for a folded needle. The engine reads a run of hiragana as a kanji
    spelling (どんなときも as どんな時も), so each kanji run of a needle that also holds kana matches
    up to two hiragana per kanji, plus two, in its place."""
    runs = list(KANJI_RUN.finditer(needle))
    if not runs or all(m.group() == needle for m in runs):
        return re.escape(needle)
    out, last = [], 0
    for m in runs:
        k = m.group()
        out += [re.escape(needle[last:m.start()]), f"(?:{re.escape(k)}|[\u3041-\u3096]{{1,{2 * len(k) + 2}}})"]
        last = m.end()
    return "".join(out + [re.escape(needle[last:])])


def kana(c):
    return "ぁ" <= c <= "ゟ" or "゠" <= c <= "ヿ"


def stem(key, pos):
    """The part of a plain row's key that every inflected form starts with."""
    if pos.split(":")[0] not in ("v", "v1", "v5", "adj"):
        return key
    for tail in ("する", "来る", "くる"):
        if key.endswith(tail) and len(key) > len(tail):
            return key[: -len(tail)]
    return key[:-1] if len(key) > 1 and kana(key[-1]) else key


class File:
    """One dictionary file: its layer, header and rows (row text, needles, rule number)."""

    def __init__(self, text):
        self.layer, self.columns, self.rows = "", None, []
        lines = text.split("\n")
        if lines and lines[0].startswith("#! layer="):
            self.layer = lines[0][len("#! layer="):].split()[0]
        for line in lines:
            line = line.rstrip("\r")
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            fields = line.split("\t")
            if self.columns is None and self.layer not in ("kana",) and fields[0] == "key":
                self.columns = {name: i for i, name in enumerate(fields)}
                continue
            self.rows.append((line, fields))

    def needles(self, fields):
        """(needles, rule number or '', pattern row) of one row."""
        def col(name):
            i = self.columns.get(name) if self.columns else None
            return fields[i] if i is not None and i < len(fields) else ""
        if self.layer == "kana":
            return [f for f in fields[:2] if f], "", False
        if self.columns is not None:
            # An ending row (pos S5.., T5.., U5..) is matched after a stem, also under a placeholder
            # key (V, I, D).
            ending = bool(re.match(r"[STU]5", col("pos")) or re.fullmatch(r"[A-Z]", col("key")))
            return [k for k in (col("key"), col("reading")) if k], col("rule"), ending
        pos = fields[2] if len(fields) > 2 else "n"
        if pos in ("end", "pat"):
            return [], "", True
        return [stem(fields[0], pos)] if fields[0] else [], "", False


def read_tree(files):
    """{relative path: File} from {relative path: text}."""
    return {path: File(text) for path, text in files.items() if path.endswith((".tsv", ".txt"))}


def dict_needles(base, head):
    """Needles, or None for every line, between two {path: File} trees of dicts/."""
    needles, rules, everything = set(), set(), False
    for path in sorted(set(base) | set(head)):
        old, new = base.get(path), head.get(path)
        before = collections.Counter(r[0] for r in old.rows) if old else collections.Counter()
        after = collections.Counter(r[0] for r in new.rows) if new else collections.Counter()
        changed = (before - after) + (after - before)
        if not changed:
            continue
        if old and new and (old.layer != new.layer or (old.columns is None) != (new.columns is None)):
            return None
        for f in (old, new):
            if not f:
                continue
            for line, fields in f.rows:
                if line not in changed:
                    continue
                keys, rule, pattern = f.needles(fields)
                everything |= pattern
                needles.update(keys)
                if rule:
                    rules.add(rule)
    if everything:
        return None
    readings = collections.defaultdict(set)
    for tree in (base, head):
        for f in tree.values():
            for line, fields in f.rows:
                if f.layer == "kana" and len(fields) >= 2:
                    readings[fields[1]].add(fields[0])
                elif rules:
                    keys, rule, pattern = f.needles(fields)
                    if rule in rules:
                        if pattern:
                            return None
                        needles.update(keys)
    for key in list(needles):
        needles |= readings.get(key, set())
    return {fold(n) for n in needles if fold(n)}


def git(repo, *args):
    run = subprocess.run(["git", "-C", repo, *args], capture_output=True)
    if run.returncode:
        fail(f"git {' '.join(args)}: {run.stderr.decode(errors='replace').strip()}")
    return run.stdout


def dicts_at(repo, ref):
    names = git(repo, "ls-tree", "-r", "--name-only", ref, "--", "dicts").decode().split("\n")
    return {n: git(repo, "show", f"{ref}:{n}").decode("utf-8") for n in names if n}


def dicts_in(folder):
    root = Path(folder)
    return {"dicts/" + p.relative_to(root).as_posix(): p.read_bytes().decode("utf-8")
            for p in sorted(root.rglob("*")) if p.is_file()}


def changed_files(repo, base, head):
    if head:
        out = git(repo, "diff", "--name-only", base, head)
    else:
        out = git(repo, "diff", "--name-only", base) + git(repo, "ls-files", "--others", "--exclude-standard")
    return sorted({n for n in out.decode().split("\n") if n})


def needles_between(repo, base, head):
    files = [f for f in changed_files(repo, base, head) if not NO_EFFECT.search(f)]
    if any(not f.startswith("dicts/") for f in files):
        return None
    if not files:
        return set()
    old = read_tree(dicts_at(repo, base))
    new = read_tree(dicts_at(repo, head) if head else dicts_in(Path(repo) / "dicts"))
    return dict_needles(old, new)


def emit(needles):
    if needles is None:
        print("*")
    else:
        for n in sorted(needles):
            print(n)


HEADER = "#! layer=words\n# test\nkey\tko\tpos\tsem\tgram\tdict\trule\tctx\tnote\tflags\tprio\treading\tid\n"


def self_test():
    assert fold("アテツケ") == "あてつけ" and fold("すごーい") == "すごい" and fold("時々") == "時時"
    assert fold("ｶﾞｷ") == "がき" and fold("ＡＢＣ") == "abc" and fold("逢(あ)いたくて") == "逢いたくて"
    assert re.search(pattern(fold("どんな時も")), fold("でも どんなときも")) and re.search(pattern("聖"), "聖さん")
    assert not re.search(pattern("聖"), "せい") and not re.search(pattern("どんな時も"), "どんなとき")
    assert stem("這い上がる", "v5") == "這い上が" and stem("コケにする", "v") == "コケに"
    assert stem("冷たい", "adj") == "冷た" and stem("静か", "na") == "静か" and stem("書く", "v5") == "書"
    base = {"dicts/w.tsv": HEADER + "当て\t맞힘\tN110\t\t\tGN\t\t\t\t\t\t\tw:1\n"
                                    "あ\t&AM@\tM435\t\t\tGN\t7\tS+1== \"x\"\t\t\t\t\tw:2\n"
                                    "あい\t&AM@\tM435\t\t\tGN\t7\tS+1== \"x\"\t\t\t\t\tw:3\n",
            "dicts/k.tsv": "#! layer=kana\n# k\nあてつけ\t当てつけ\t\n",
            "dicts/p.tsv": "#! layer=base\n# p\n這い上がる\t기어오르다\tv5\n"}
    head = dict(base)
    head["dicts/w.tsv"] = base["dicts/w.tsv"] + "当てつけ\t빈정거림\tN110\t\t\tGN\t\t\t\t\t\t\tw:4\n"
    assert dict_needles(read_tree(base), read_tree(head)) == {"当てつけ", "あてつけ"}
    head["dicts/w.tsv"] = base["dicts/w.tsv"].replace("S+1== \"x\"\t\t\t\t\tw:2", "S+1== \"y\"\t\t\t\t\tw:2")
    assert dict_needles(read_tree(base), read_tree(head)) == {"あ", "あい"}
    head["dicts/w.tsv"] = base["dicts/w.tsv"]
    head["dicts/p.tsv"] = base["dicts/p.tsv"].replace("기어오르다", "기어 올라가다")
    assert dict_needles(read_tree(base), read_tree(head)) == {"這い上が"}
    head["dicts/p.tsv"] = base["dicts/p.tsv"] + "書いたじゃん\t썼잖아\tend\n"
    assert dict_needles(read_tree(base), read_tree(head)) is None
    head["dicts/p.tsv"] = base["dicts/p.tsv"]
    head["dicts/w.tsv"] = base["dicts/w.tsv"] + "V\t8\tS560\t\t\tGN\t\t\t\t\t\t\tw:6\n"
    assert dict_needles(read_tree(base), read_tree(head)) is None
    ending = base["dicts/w.tsv"] + "のよ\t6\tS500\t\t\tGN\t7\tS+1== \"x\"\t\t\t\t\tw:7\n"
    head["dicts/w.tsv"] = ending.replace("S+1== \"x\"\t\t\t\t\tw:2", "S+1== \"z\"\t\t\t\t\tw:2")
    assert dict_needles(read_tree(dict(base, **{"dicts/w.tsv": ending})), read_tree(head)) is None
    head["dicts/w.tsv"] = base["dicts/w.tsv"]
    assert dict_needles(read_tree(base), read_tree(base)) == set()
    with tempfile.TemporaryDirectory() as tmp:
        def g(*args):
            subprocess.run(["git", "-C", tmp, *args], check=True, capture_output=True)
        g("init", "-q")
        g("config", "user.email", "t@t")
        g("config", "user.name", "t")
        for path, text in base.items():
            Path(tmp, path).parent.mkdir(parents=True, exist_ok=True)
            Path(tmp, path).write_text(text, encoding="utf-8")
        Path(tmp, "docs").mkdir()
        Path(tmp, "docs/a.md").write_text("a\n")
        g("add", ".")
        g("commit", "-qm", "base")
        Path(tmp, "docs/a.md").write_text("b\n")
        assert needles_between(tmp, "HEAD", None) == set()
        Path(tmp, "dicts/w.tsv").write_text(head["dicts/w.tsv"] + "聖\t히지리\tN130\t\t\tGN\t\t\t\t\t\t\tw:5\n",
                                            encoding="utf-8")
        assert needles_between(tmp, "HEAD", None) == {"聖"}
        g("commit", "-qam", "row")
        assert needles_between(tmp, "HEAD~1", "HEAD") == {"聖"}
        Path(tmp, "src").mkdir()
        Path(tmp, "src/a.cpp").write_text("int a;\n")
        assert needles_between(tmp, "HEAD", None) is None
    print("affected self-test OK")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) == 3 and argv[0] == "--dicts":
        emit(dict_needles(read_tree(dicts_in(argv[1])), read_tree(dicts_in(argv[2]))))
        return 0
    repo = str(Path(__file__).resolve().parent.parent)
    if len(argv) >= 2 and argv[0] == "--repo":
        repo, argv = argv[1], argv[2:]
    if len(argv) not in (1, 2) or argv[0].startswith("-"):
        fail(__doc__.strip().split("\n\n")[1])
    emit(needles_between(repo, argv[0], argv[1] if len(argv) == 2 else None))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
