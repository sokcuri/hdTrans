#!/usr/bin/env python3
"""Counts the Japanese and Korean text written into the engine sources: an optional report for
release reviews, not a check.

  tools/surface-budget.py [--report] [--tree DIR]
                                       totals, the surface:code condition ratio and the files with
                                       the most literals; DIR (default src/core) is relative to the root
  tools/surface-budget.py --self-test

Sources are the .cpp and .h files under src/core (or --tree), except generated files (a first line
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
  cond_code                  comparisons against codes, attributes and classes
Exit: 0, or 2 on invalid arguments.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLUMNS = ["lit_ja", "lit_ko", "lit_punct", "lists", "list_items", "char_lit", "escape", "const_ref",
           "cond_surface", "cond_code"]

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


def sources(root, tree="src/core"):
    """Hand-written sources under tree: path relative to root -> text."""
    files = {}
    for p in sorted((Path(root) / tree).rglob("*")):
        if p.suffix in (".cpp", ".h"):
            text = p.read_text(encoding="utf-8")
            if not GENERATED.match(text):
                files[p.relative_to(root).as_posix()] = text
    return files


def measure_tree(root, tree="src/core"):
    files = sources(root, tree)
    defined = {f: surface_constants(lex(s)[1]) for f, s in files.items() if f.endswith(".h")}
    shared = set().union(*defined.values()) if defined else set()
    return {f: measure(s, shared, defined.get(f, frozenset())) for f, s in files.items()}


def report(root, tree="src/core"):
    counts = measure_tree(root, tree)
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
    print("self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    tree = "src/core"
    if "--tree" in argv:
        i = argv.index("--tree")
        if i + 1 >= len(argv):
            print(__doc__.strip(), file=sys.stderr)
            return 2
        tree = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
        if not (ROOT / tree).is_dir():
            print(f"surface-budget: no directory {tree}", file=sys.stderr)
            return 2
    if argv in ([], ["--report"]):
        return report(ROOT, tree)
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
