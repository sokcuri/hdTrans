#!/usr/bin/env python3
"""Counts the Japanese and Korean text written into the engine sources and keeps each count of each
file at or below its ceiling in tools/surface-budget.tsv.

  tools/surface-budget.py [--check] [--base REF]   compare the sources with the ceilings
  tools/surface-budget.py --update [--base REF]    write the current counts as the ceilings
  tools/surface-budget.py --report                 totals, the surface:code condition ratio and
                                                   the files with the most literals
  tools/surface-budget.py --self-test

Sources are the .cpp and .h files under src/core, except generated files (a first line
`// Generated ...; do not edit`). Counts per file:
  lit_ja, lit_ko, lit_punct  string literals holding kana or kanji, else Hangul, else full-width
                             punctuation (any prefix, raw strings included)
  lists, list_items          brace initializers without statements that hold three or more such
                             literals or character literals, and the literals in them
  char_lit                   character literals holding such a character (U'の')
  escape                     literals spelling a non-ASCII character with \\u, \\U or \\x (not the
                             byte order mark), hex constants in the kana, CJK, Hangul and full-width
                             ranges (not bit masks), and decimal ones compared in those ranges
  const_ref                  uses of a k-named constant that a header initializes with such literals
  cond_surface               comparisons and helper calls (In, OneOf, Starts, Contains, HasRune,
                             InRunes, ...) against such text, its constants or lists named like word
                             lists, also across lines, and case labels of such characters
  cond_code                  comparisons against codes, attributes and classes (not a ceiling)
A file without a row has the ceiling 0, so a renamed or split file is a raise. --check fails when a
count exceeds its ceiling. With --base it also fails when the ceilings file raises a number or adds a
row since REF and no commit of REF..HEAD carries a `Surface-Budget: <reason>` trailer (one trailer
covers every file of the range), and when a file changed since REF counts less than its ceiling, so
that the slack is written down before it can be filled again. --update writes the counts as
ceilings and drops rows of removed files; it refuses to raise a number unless a commit of REF..HEAD
(REF defaults to the merge base with develop) carries that trailer. To raise a ceiling: commit the
code with the trailer, run --update, commit the table.
Exit: 0 within the ceilings, 1 over a ceiling or a raise without a trailer, 2 invalid arguments.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = "tools/surface-budget.tsv"
COLUMNS = ["lit_ja", "lit_ko", "lit_punct", "lists", "list_items", "char_lit", "escape", "const_ref",
           "cond_surface", "cond_code"]
CEILINGS = [c for c in COLUMNS if c != "cond_code"]
TRAILER = re.compile(r"^Surface-Budget:[ \t]*\S", re.M)

JA = re.compile(r"[぀-ヿ㐀-䶿一-鿿ｦ-ﾟ]")
KO = re.compile(r"[가-힣ᄀ-ᇿ㄰-㆏]")
PUNCT = re.compile(r"[　-〿！-･‥…〜]")
CP_RANGES = [(0x1100, 0x11FF), (0x3000, 0x30FF), (0x3130, 0x318F), (0x3400, 0x4DBF), (0x4E00, 0x9FFF),
             (0xAC00, 0xD7A3), (0xFF00, 0xFFEF)]
ESCAPE = re.compile(r"\\(?:\\|u([0-9A-Fa-f]{4})|U([0-9A-Fa-f]{8})|x([0-9A-Fa-f]+))")
TOKEN = re.compile(r"""
    (?P<comment>//[^\n]*|/\*.*?(?:\*/|\Z))
  | (?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\\s"]{0,16})\((?P<rawbody>.*?)\)(?P=delim)")
  | (?P<str>(?:u8|[uUL])?"(?:[^"\\\n]|\\.)*")
  | (?P<num>\b\d[\w']*)
  | (?P<chr>(?:u8|[uUL])?'(?:[^'\\\n]|\\.)+')
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<punct>[{};])
""", re.S | re.X)
PRED = re.compile(r"==|!=|\bstarts_with\(|\bends_with\(|\bfind\(|\bInSet\(|\bHas\(|\bRuneIn\(|\bcontains\(|"
                  r"\bcompare\(|\bIs[A-Z]\w*\(|\b(?:In|OneOf|AnyOf|Starts|Ends|Contains|HasRune|InRunes)\(")
SURF_NAME = re.compile(r"\bk(?:Ja|Ko|Korean|Japanese)[A-Z]\w*|\bk[A-Z]\w*(?:Verbs|Nouns|Forms|Lemmas|Words|Stems|"
                       r"Endings|Particles|Passives|Suffixes|Prefixes|Kanji|Readings|Surfaces|Pairs|Names|Titles|"
                       r"Honorifics|Markers|Counters|Sounds)\b")
CODE_ID = re.compile(r"\bCode\(|\bDirectCode\(|\bAttr\(|\bGAttr\(|\bpartCode\b|\bClassify\(|\bNameClassOf\(|"
                     r"\bWordClass\b|\bYougo\(|\bpos\b|\bkClass\w+|\bGrammarCode\(|\"[A-Z][a-z0-9]\"|"
                     r"\"[A-Z][A-Za-z][a-z]\"|\"[&$%][A-Za-z0-9:]{2}@\"|\"[0-9*:]{3,}\"")
ATOM_SPLIT = re.compile(r"&&|\|\||;|\?|(?<!:):(?!:)|\{|\}|\breturn\b|\bif\b")
SURF_MARK = "@SURF@"
CASE_LABEL = re.compile(r"\bcase\s+[\"']" + SURF_MARK)
GENERATED = re.compile(r"^//.*\bGenerated\b.*\bdo not edit", re.I)
BOM = re.compile(r"^(?:\\x[eE][fF]\\x[bB][bB]\\x[bB][fF]|\\u[fF][eE][fF][fF])$")
RELATIONAL_BEFORE = re.compile(r"(?:[<>]=?|==|!=)\s*$")
RELATIONAL_AFTER = re.compile(r"^\s*(?:[<>]=?|==|!=)")
BITWISE_BEFORE = re.compile(r"(?:(?<![&|])[&|^]|<<|>>)=?\s*$")
BITWISE_AFTER = re.compile(r"^\s*(?:[&|^](?![&|])|<<|>>)")
DEFINITION = re.compile(r"\b(k[A-Z]\w*)\s*(?:\[[^\]]*\]\s*)*(?:=(?!=)|\{)")


def text_kind(body):
    if JA.search(body):
        return "ja"
    if KO.search(body):
        return "ko"
    if PUNCT.search(body):
        return "punct"
    return None


def literal_body(tok, kind):
    if kind == "raw":
        return tok.group("rawbody")
    text = tok.group(kind)
    return text[text.index('"' if kind == "str" else "'") + 1:-1]


def escapes_non_ascii(body):
    if BOM.match(body):
        return False
    for m in ESCAPE.finditer(body):
        digits = next((g for g in m.groups() if g), None)
        if digits and int(digits, 16) >= 0x80:
            return True
    return False


def lex(src):
    """Tokens (kind, text, surface kind or None, start) and the code with comments removed and
    surface literals replaced by a marker, line breaks kept."""
    tokens, masked, last = [], [], 0
    for m in TOKEN.finditer(src):
        kind = m.lastgroup if m.lastgroup not in ("delim", "rawbody") else "raw"
        masked.append(src[last:m.start()])
        last = m.end()
        text = m.group(0)
        if kind == "comment":
            masked.append("\n" * text.count("\n"))
            continue
        surf = text_kind(literal_body(m, kind)) if kind in ("raw", "str", "chr") else None
        tokens.append((kind, text, surf, m.start(), m))
        if surf and kind != "chr":
            masked.append('"' + SURF_MARK + '"' + "\n" * text.count("\n"))
        elif surf:
            masked.append("'" + SURF_MARK + "'")
        else:
            masked.append(text)
    masked.append(src[last:])
    return tokens, "".join(masked)


def surface_constants(masked):
    """k-named constants whose initializer holds a surface literal."""
    names = set()
    for m in DEFINITION.finditer(masked):
        depth, j = 0, m.end() - 1
        while j < len(masked):
            c = masked[j]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            elif c == ";" and depth <= 0:
                break
            j += 1
        if SURF_MARK in masked[m.end() - 1:j]:
            names.add(m.group(1))
    return names


def count_lists(tokens):
    blocks, stack = [], []  # block: [parent, holds a statement at any depth, items]
    for kind, text, surf, _, _ in tokens:
        if kind == "punct" and text == "{":
            blocks.append([stack[-1] if stack else None, False, 0])
            stack.append(len(blocks) - 1)
        elif kind == "punct" and text == "}":
            if stack:
                stack.pop()
        elif kind == "punct" and text == ";":
            for b in stack:
                blocks[b][1] = True
        elif surf:
            for b in stack:
                blocks[b][2] += 1
    lists = items = 0
    for parent, statement, n in blocks:
        if not statement and n >= 3 and (parent is None or blocks[parent][1]):
            lists += 1
            items += n
    return lists, items


def count_conditions(masked, constants):
    surface, code = len(CASE_LABEL.findall(masked)), 0
    names = re.compile(r"\b(?:" + "|".join(sorted(map(re.escape, constants))) + r")\b") if constants else None
    for atom in ATOM_SPLIT.split(re.sub(r"^\s*#.*$", "", masked, flags=re.M)):
        n = len(PRED.findall(atom))
        if not n:
            continue
        if SURF_MARK in atom or SURF_NAME.search(atom) or (names and names.search(atom)):
            surface += n
        elif CODE_ID.search(atom):
            code += n
    return surface, code


def code_point_constant(src, tok):
    """A hex constant in the code point ranges that is not a bit mask, or a decimal range bound."""
    text, start = tok[1], tok[3]
    hexa = text[:2] in ("0x", "0X")
    try:
        v = int(text[2:].replace("'", "").rstrip("uUlL"), 16) if hexa else int(text.replace("'", "").rstrip("uUlL"))
    except ValueError:
        return False
    if not any(lo <= v <= hi for lo, hi in CP_RANGES):
        return False
    before, after = src[max(0, start - 8):start], src[start + len(text):start + len(text) + 8]
    if hexa:
        return not (BITWISE_BEFORE.search(before) or BITWISE_AFTER.match(after))
    if not (RELATIONAL_BEFORE.search(before) or RELATIONAL_AFTER.match(after)):
        return False
    # A decimal code point is one bound of a range: another such bound is near on the line.
    line_start, line_end = src.rfind("\n", 0, start) + 1, src.find("\n", start)
    end = start + len(text)
    near = src[max(line_start, start - 80):start] + " " + src[end:line_end if line_end >= 0 else None][:80]
    return any(any(lo <= int(d) <= hi for lo, hi in CP_RANGES) for d in re.findall(r"\b\d{4,5}\b", near))


def measure(src, shared, own=frozenset()):
    """Counts of one source. shared: constants that headers initialize with surface literals; own:
    those this file defines itself (not counted as uses)."""
    tokens, masked = lex(src)
    r = dict.fromkeys(COLUMNS, 0)
    for tok in tokens:
        kind, text, surf, _, m = tok
        if kind in ("raw", "str"):
            if surf:
                r["lit_" + surf] += 1
            if escapes_non_ascii(literal_body(m, kind)):
                r["escape"] += 1
        elif kind == "chr":
            if surf:
                r["char_lit"] += 1
            if escapes_non_ascii(literal_body(m, kind)):
                r["escape"] += 1
        elif kind == "num" and code_point_constant(src, tok):
            r["escape"] += 1
        elif kind == "ident" and text in shared and text not in own:
            r["const_ref"] += 1
    r["lists"], r["list_items"] = count_lists(tokens)
    local = surface_constants(masked)
    r["cond_surface"], r["cond_code"] = count_conditions(masked, local | shared)
    return r


def sources(root):
    """Hand-written sources under src/core: path relative to root -> text."""
    files = {}
    for p in sorted((Path(root) / "src" / "core").rglob("*")):
        if p.suffix in (".cpp", ".h"):
            text = p.read_text(encoding="utf-8")
            if not GENERATED.match(text):
                files[p.relative_to(root).as_posix()] = text
    return files


def measure_tree(root):
    files = sources(root)
    defined = {f: surface_constants(lex(s)[1]) for f, s in files.items() if f.endswith(".h")}
    shared = set().union(*defined.values()) if defined else set()
    return {f: measure(s, shared, defined.get(f, frozenset())) for f, s in files.items()}


def read_table(text):
    rows = {}
    header = None
    for line in text.split("\n"):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if header is None:
            header = cells
            continue
        rows[cells[0]] = {k: int(v) for k, v in zip(header[1:], cells[1:]) if k in CEILINGS}
    return rows


def write_table(rows):
    out = ["# Ceilings of tools/surface-budget.py per file under src/core; a missing file has 0.",
           "file\t" + "\t".join(CEILINGS)]
    for f in sorted(rows):
        if any(rows[f].get(k, 0) for k in CEILINGS):
            out.append(f + "\t" + "\t".join(str(rows[f].get(k, 0)) for k in CEILINGS))
    return "\n".join(out) + "\n"


def exceeded(counts, table):
    """(file, column, count, ceiling) for every count above its ceiling."""
    over = []
    for f, r in sorted(counts.items()):
        for k in CEILINGS:
            ceiling = table.get(f, {}).get(k, 0)
            if r[k] > ceiling:
                over.append((f, k, r[k], ceiling))
    return over


def raises(old, new):
    return [(f, k, old.get(f, {}).get(k, 0), v) for f in sorted(new) for k, v in sorted(new[f].items())
            if v > old.get(f, {}).get(k, 0)]


def git(root, *args, check=True):
    r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if check and r.returncode:
        sys.exit(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r


def default_base(root):
    refs = ["origin/develop"] + [r for r in git(root, "for-each-ref", "--format=%(refname:short)",
                                                 "refs/remotes/*/develop").stdout.split() if r != "origin/develop"]
    for ref in refs + ["develop"]:
        base = git(root, "merge-base", "HEAD", ref, check=False).stdout.strip()
        if base:
            return base
    sys.exit("no develop branch to take the merge base with; pass --base REF")


def has_trailer(root, base):
    return bool(TRAILER.search(git(root, "log", "--format=%B", f"{base}..HEAD").stdout))


def table_at(root, ref):
    r = git(root, "show", f"{ref}:{TABLE}", check=False)
    return read_table(r.stdout) if r.returncode == 0 else {}


def check(root, base):
    path = Path(root) / TABLE
    table = read_table(path.read_text(encoding="utf-8")) if path.exists() else {}
    counts = measure_tree(root)
    status = 0
    for f, k, n, ceiling in exceeded(counts, table):
        print(f"{f}: {k} {n} > {ceiling}")
        status = 1
    slack = [(f, k, counts.get(f, {}).get(k, 0), table[f][k]) for f in sorted(table) for k in CEILINGS
             if counts.get(f, {}).get(k, 0) < table[f].get(k, 0)]
    if base:
        up = raises(table_at(root, base), table)
        if up and not has_trailer(root, base):
            for f, k, old, new in up:
                print(f"{TABLE}: {f} {k} raised {old} -> {new} without a Surface-Budget trailer since {base}")
            status = 1
        changed = set(git(root, "diff", "--name-only", base, "--", "src/core").stdout.split())
        for f, k, n, ceiling in slack:
            if f in changed:
                print(f"{f}: {k} {n} < {ceiling}; lower it with: python3 tools/surface-budget.py --update")
                status = 1
    elif slack:
        print(f"surface-budget: {len(slack)} ceilings can be lowered: python3 tools/surface-budget.py --update")
    print("surface-budget: " + ("over" if status else "ok"))
    return status


def update(root, base):
    path = Path(root) / TABLE
    table = read_table(path.read_text(encoding="utf-8")) if path.exists() else {}
    counts = {f: {k: r[k] for k in CEILINGS} for f, r in measure_tree(root).items()}
    up = raises(table, counts)
    if up and not has_trailer(root, base or default_base(root)):
        for f, k, old, new in up:
            print(f"{f}: {k} {old} -> {new}")
        print("surface-budget: raising needs a commit with a `Surface-Budget: <reason>` trailer; nothing written")
        return 1
    path.write_text(write_table(counts), encoding="utf-8")
    print(f"surface-budget: wrote {TABLE}" + (f" ({len(up)} raised)" if up else ""))
    return 0


def report(root):
    counts = measure_tree(root)
    total = {k: sum(r[k] for r in counts.values()) for k in COLUMNS}
    print(f"files\t{len(counts)}")
    for k in COLUMNS:
        print(f"{k}\t{total[k]}")
    ratio = total["cond_surface"] / total["cond_code"] if total["cond_code"] else float("inf")
    print(f"surface:code\t{ratio:.2f}:1")
    print("\nfile\tliterals\tlist_items\tcond_surface\tcond_code")
    top = sorted(counts.items(), key=lambda x: -(x[1]["lit_ja"] + x[1]["lit_ko"] + x[1]["lit_punct"]))[:15]
    for f, r in top:
        print(f"{f}\t{r['lit_ja'] + r['lit_ko'] + r['lit_punct']}\t{r['list_items']}\t{r['cond_surface']}\t"
              f"{r['cond_code']}")
    return 0


def self_test():
    src = r'''
// "コメント" is not counted
constexpr std::string_view kWords[] = {"いく", "くる", "みる"};
constexpr std::pair<std::string_view, std::string_view> kPairs[] = {{"あ", "아"}, {"い", "이"}};
const char* raw = R"x(가"나)x";
const char* e = "\u3042", *f = "\\u3042";
bool F(std::string_view s, char32_t c) {
    if (s == "の" || Has(Code(0), "Nm")) return c == U'ん' || c == 0x3042;
    for (auto w : {"か", "が", "、"}) { if (s.ends_with(w)) return true; }
    return InSet(kWords, s) && s != "あ" && kJaNo == s;
}
'''
    r = measure(src, {"kJaNo"})
    want = {"lit_ja": 9, "lit_ko": 3, "lit_punct": 1, "lists": 3, "list_items": 10, "char_lit": 1, "escape": 2,
            "const_ref": 1, "cond_surface": 5, "cond_code": 1}
    assert r == want, r
    more = r'''
bool G(std::string_view s, char32_t c, unsigned m) {
    if (OneOf(s, kWords) || Starts(s, "お") || In(s, {"a", "b"}) || c >= 12353 && c <= 12438) return true;
    if (s ==
        "ね") return true;
    switch (c) { case U'ゃ': case U'a': return (m & 0xFF00) != 0 || c == 0x3000; }
    return s.starts_with("\xEF\xBB\xBF") || Count(s) > 20000;
}
'''
    r = measure(more, {"kWords"})
    assert (r["cond_surface"], r["escape"], r["char_lit"]) == (4, 3, 1), r
    assert GENERATED.match("// Generated table; do not edit.\n") and not GENERATED.match("// Kept by hand.\n")
    header = 'inline constexpr std::string_view kJaNo = "の";\nconstexpr int kN = 3;'
    assert surface_constants(lex(header)[1]) == {"kJaNo"}
    table = read_table(write_table({"a.cpp": {"lit_ja": 2}, "b.cpp": {}}))
    assert table == {"a.cpp": dict(dict.fromkeys(CEILINGS, 0), lit_ja=2)}, table
    assert exceeded({"a.cpp": dict.fromkeys(COLUMNS, 0) | {"lit_ja": 3}}, table) == [("a.cpp", "lit_ja", 3, 2)]
    assert raises(table, {"a.cpp": {"lit_ja": 1}, "c.cpp": {"lit_ko": 1}}) == [("c.cpp", "lit_ko", 0, 1)]

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "src" / "core").mkdir(parents=True)
        (root / "tools").mkdir()
        a = root / "src" / "core" / "a.cpp"
        a.write_text('auto x = "あ";\n', encoding="utf-8")

        def run(*args):
            subprocess.run(["git", "-C", tmp, "-c", "user.name=t", "-c", "user.email=t@t", *args],
                           check=True, capture_output=True)
        quiet = open(os.devnull, "w")
        stdout, sys.stdout = sys.stdout, quiet
        saved = {k: os.environ.get(k) for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM")}
        os.environ.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        try:
            run("init", "-q")
            run("commit", "-q", "--allow-empty", "-m", "base")
            base = git(root, "rev-parse", "HEAD").stdout.strip()
            assert check(root, None) == 1
            run("add", "-A")
            run("commit", "-q", "-m", "code")
            assert update(root, base) == 1 and not (root / TABLE).exists()
            run("commit", "-q", "--amend", "-m", "code\n\nSurface-Budget: a test literal")
            assert update(root, base) == 0 and check(root, base) == 0
            run("add", "-A")
            run("commit", "-q", "-m", "table")
            a.write_text('auto x = "あ";\nauto y = "い";\n', encoding="utf-8")
            assert check(root, None) == 1
            head = git(root, "rev-parse", "HEAD").stdout.strip()
            assert update(root, head) == 1
            a.write_text("auto x = 1;\n", encoding="utf-8")
            assert update(root, head) == 0 and read_table((root / TABLE).read_text(encoding="utf-8")) == {}
            (root / TABLE).write_text(write_table({"src/core/a.cpp": {"lit_ja": 5}}), encoding="utf-8")
            run("commit", "-q", "-am", "raise without reason")
            assert check(root, head) == 1
            run("reset", "-q", "--hard", head)
            a.write_text("auto x = 1;\n", encoding="utf-8")
            run("commit", "-q", "-am", "drop the literal")
            assert check(root, None) == 0 and check(root, head) == 1
            assert update(root, head) == 0 and check(root, head) == 0
        finally:
            sys.stdout = stdout
            quiet.close()
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
    print("self-test ok")
    return 0


def main(argv):
    base = None
    if "--base" in argv:
        i = argv.index("--base")
        if i + 1 >= len(argv):
            print(__doc__.strip(), file=sys.stderr)
            return 2
        base = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    if argv == ["--self-test"]:
        return self_test()
    if argv == ["--report"]:
        return report(ROOT)
    if argv == ["--update"]:
        return update(ROOT, base)
    if argv in ([], ["--check"]):
        return check(ROOT, base)
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
