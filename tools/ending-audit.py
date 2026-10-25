#!/usr/bin/env python3
"""Counts the places that decide a predicate's Korean ending on its finished string: the ratchet of
the conjugation restructure (docs/testing.md). None of the counts may rise in a pull request.

  tools/ending-audit.py [--base REV] [--tsv]
  tools/ending-audit.py --self-test

Counts over the working tree:
  construction-ending-rewrites  rows of src/core/dict/builtin/constructions.tsv whose actions rewrite a
                                made ending (set-ending, replace-ending-tail, map-ending-tail,
                                map-target-tail)
  rule-ending-writes            rule bodies (the ctx column of dicts/ and the builtin tables, the G
                                lines of src/core/rules/globals.txt) that assign an ending
                                translation (Y+n= or Y-n=)
  rule-ending-left-writes       those that assign the ending of a word to the left (Y-n=)
  rule-ending-tests             rule bodies that compare an ending translation (Y+n==, Y+n!=, ...)
  output-hangul-literals        string literals holding Hangul in src/core/output/*.cpp, comments
                                left out
  word-hangul-literals          the same in src/core/word/*.cpp
With --base REV the same counts are taken at revision REV (git show) and a count that rose is a
FAIL; a count that fell is reported. --tsv prints `count TAB value` rows (TAB base) without the
headings. Exit: 0, 1 when a count rose, 2 on invalid arguments or an unreadable revision.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RULE_DIRS = ("dicts", "src/core/dict/builtin")
GLOBALS = "src/core/rules/globals.txt"
CONSTRUCTIONS = "src/core/dict/builtin/constructions.tsv"
SOURCE_DIRS = {"output-hangul-literals": "src/core/output", "word-hangul-literals": "src/core/word"}
COUNTS = ("construction-ending-rewrites", "rule-ending-writes", "rule-ending-left-writes", "rule-ending-tests",
          "output-hangul-literals", "word-hangul-literals")

REWRITE = re.compile(r"\b(?:set-ending|replace-ending-tail|map-ending-tail|map-target-tail)\b")
WRITE = re.compile(r"\bY[+-]\d+=(?!=)")
LEFT_WRITE = re.compile(r"\bY-\d+=(?!=)")
TEST = re.compile(r"\bY[+-]\d+[!=]=")
HANGUL = re.compile(r"[가-힣ᄀ-ᇿ㄰-㆏]")


class Tree:
    """The files of the working tree, or of a revision through git."""

    def __init__(self, rev=None):
        self.rev = rev

    def read(self, path):
        if self.rev is None:
            return (ROOT / path).read_text(encoding="utf-8")
        done = subprocess.run(["git", "-C", str(ROOT), "show", f"{self.rev}:{path}"], capture_output=True)
        if done.returncode != 0:
            raise FileNotFoundError(path)
        return done.stdout.decode("utf-8")

    def files(self, folder, suffix):
        if self.rev is None:
            return sorted(str(p.relative_to(ROOT)).replace("\\", "/") for p in (ROOT / folder).glob("*" + suffix))
        out = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "--name-only", self.rev, folder + "/"],
                             capture_output=True, text=True, check=True).stdout
        return sorted(p for p in out.split("\n") if p.endswith(suffix))


def rule_bodies(tree):
    """The rule bodies of the dictionaries (ctx column) and the global rules."""
    bodies = []
    for folder in RULE_DIRS:
        for path in tree.files(folder, ".tsv"):
            ctx = None
            for line in tree.read(path).split("\n"):
                if not line or line.startswith("#") or line.startswith("//"):
                    continue
                fields = line.split("\t")
                if ctx is None:
                    if fields[0] != "key":
                        break  # no header: the user-dictionary format, no rules
                    ctx = fields.index("ctx") if "ctx" in fields else -1
                    continue
                if 0 <= ctx < len(fields) and fields[ctx]:
                    bodies.append(fields[ctx])
    for line in tree.read(GLOBALS).split("\n"):
        if line.startswith("G "):
            bodies.append(line)
    return bodies


def strings(code):
    """The string literals of C++ code, comments and character literals left out."""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]
        if code.startswith("//", i):
            end = code.find("\n", i)
            i = n if end < 0 else end + 1
        elif code.startswith("/*", i):
            end = code.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif c == "'":
            i += 1
            while i < n and code[i] != "'":
                i += 2 if code[i] == "\\" else 1
            i += 1
        elif c == '"':
            if code.startswith('R"', i - 1) and i > 0 and (i < 2 or not code[i - 2].isalnum()):
                delim = code[i + 1:code.index("(", i)]
                end = code.index(")" + delim + '"', i)
                out.append(code[i + 2 + len(delim):end])
                i = end + len(delim) + 2
                continue
            j = i + 1
            while j < n and code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            out.append(code[i + 1:j])
            i = j + 1
        else:
            i += 1
    return out


def hangul_literals(tree, folder):
    return sum(1 for path in tree.files(folder, ".cpp") for s in strings(tree.read(path)) if HANGUL.search(s))


def construction_rewrites(tree):
    count = 0
    for line in tree.read(CONSTRUCTIONS).split("\n"):
        fields = line.split("\t")
        if line.startswith("cx:") and len(fields) >= 4 and REWRITE.search(fields[3]):
            count += 1
    return count


def counts(tree):
    bodies = rule_bodies(tree)
    return {
        "construction-ending-rewrites": construction_rewrites(tree),
        "rule-ending-writes": sum(1 for b in bodies if WRITE.search(b)),
        "rule-ending-left-writes": sum(1 for b in bodies if LEFT_WRITE.search(b)),
        "rule-ending-tests": sum(1 for b in bodies if TEST.search(b)),
        **{name: hangul_literals(tree, folder) for name, folder in SOURCE_DIRS.items()},
    }


def report(head, base, tsv):
    """The report lines and the number of counts that rose."""
    lines, bad = [], 0
    if not tsv:
        lines.append("count\thead" + ("\tbase" if base else ""))
    for name in COUNTS:
        line = f"{name}\t{head[name]}"
        if base is not None:
            line += f"\t{base[name]}"
            if head[name] > base[name]:
                line += "\tFAIL: rose"
                bad += 1
            elif head[name] < base[name]:
                line += "\tfell"
        lines.append(line)
    return lines, bad


def self_test():
    assert strings('a("가") // "나"\n/* "다" */ b(\'"\', "\\"라")') == ["가", '\\"라']
    assert strings('R"(마)" x "바"') == ["마", "바"]
    assert [bool(WRITE.search(s)) for s in ('Y+0= "6"', 'Y-1= "5다"', 'Y+0== "6"', 'Y+0!= "6"', 'U+0= "x"')] == \
        [True, True, False, False, False]
    assert [bool(LEFT_WRITE.search(s)) for s in ('Y+0= "6"', 'Y-1= "5다"')] == [False, True]
    assert [bool(TEST.search(s)) for s in ('Y+0== "6"', 'Y-2!= "6"', 'Y+0= "6"', 'U+0== "x"')] == [True, True, False, False]
    assert REWRITE.search('set-ending "3게요"') and REWRITE.search("map-target-tail @m") and not REWRITE.search("set-lemma x")
    head = dict.fromkeys(COUNTS, 2)
    base = dict(head, **{"rule-ending-tests": 1, "word-hangul-literals": 3})
    lines, bad = report(head, base, True)
    assert bad == 1 and lines[3].endswith("FAIL: rose") and lines[5].endswith("fell"), lines
    assert report(head, None, False)[0][0] == "count\thead"
    print("PASS ending-audit self-test")


def main(argv):
    if argv == ["--self-test"]:
        self_test()
        return 0
    base, tsv = None, False
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--base" and args:
            base = args.pop(0)
        elif a == "--tsv":
            tsv = True
        else:
            print(__doc__.strip(), file=sys.stderr)
            return 2
    head = counts(Tree())
    try:
        at_base = counts(Tree(base)) if base else None
    except (FileNotFoundError, subprocess.CalledProcessError, UnicodeDecodeError) as e:
        print(f"ending-audit: cannot read {base}: {e}", file=sys.stderr)
        return 2
    lines, bad = report(head, at_base, tsv)
    print("\n".join(lines))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
