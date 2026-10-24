#!/usr/bin/env python3
"""Translates variant sentences of construction families and checks the Korean shape of each.

  tools/variants.py CLI [-d DICTS] [--base CLI DICTS] [--fails] [--check] [TEMPLATE...] [-- CLI-OPTIONS...]
  tools/variants.py --list [-d DICTS] [TEMPLATE...]
  tools/variants.py --self-test

A template file (default: every tests/variants/*.tsv) holds families. Its lines, tab-separated:

  family NAME SPEC [MIN]      starts a family; SPEC is a spec/ rule id or -, MIN the pass rate
                              in percent that --check asks of it (default 0, no check)
  size N                      how many sentences the family generates (default 20)
  options OPT...              CLI options for the family's lines (--speech soft)
  slot NAME KIND WORD...      words for {NAME}; KIND says what each word must be in DICTS
  case TEMPLATE WANT [REJECT] a sentence pattern, the regex its output must match and one it must not

Slot kinds: `person` a noun whose row has a sem code of a person, `noun` a noun, `verb` a verb in
its dictionary form, `vp` an object and a verb joined by + (本を+読む), `adj` an i-adjective in
its dictionary form, `list` any word (not looked up). Words of the wrong kind are reported and
skipped. In TEMPLATE, {NAME} is the word and {NAME:FORM} a verb or adjective form (the keys of the
tables in inflect: te, ta, cp-ta for 読まされた, teireba, chatta, ...); for a vp the object comes
first. In WANT and REJECT, {NAME.ko} is any Korean gloss of the noun's rows (a regex
alternation) and {NAME} the word itself; the rest is a Python regex searched in the output line.
Each family draws its sentences from the cases in turn, every case taking a seeded sample of its
slot combinations, so a run is the same on every machine.
Prints the pass rate per family (with --base, before -> after) and with --fails every failing line.
--list prints the generated sentences only. --check exits 1 when a family is below its MIN.
Exit: 0 ok, 1 a family below its MIN with --check, 2 invalid arguments, templates or a failed CLI.
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PERSON_CODES = ("VHn", "VQw", "VQm", "VGn", "VAm", "QPN", "QNP")
GODAN = {  # dictionary ending: a, i, e, o rows, te, ta
    "う": ("わ", "い", "え", "お", "って", "った"), "く": ("か", "き", "け", "こ", "いて", "いた"),
    "ぐ": ("が", "ぎ", "げ", "ご", "いで", "いだ"), "す": ("さ", "し", "せ", "そ", "して", "した"),
    "つ": ("た", "ち", "て", "と", "って", "った"), "ぬ": ("な", "に", "ね", "の", "んで", "んだ"),
    "ぶ": ("ば", "び", "べ", "ぼ", "んで", "んだ"), "む": ("ま", "み", "め", "も", "んで", "んだ"),
    "る": ("ら", "り", "れ", "ろ", "って", "った"),
}
MARKER = re.compile(r"[&$%][^@]*@")


class TemplateError(Exception):
    pass


def load_dicts(folder):
    """{key: [(ko, pos, sem)]} of every row of the .tsv files in folder. A file without a `key`
    header line is in the short format (key, ko, kind; docs/dictionary-files.md): its nouns get
    pos N110, and its verbs and adjectives of a fixed kind their stem with an M4/H7 code."""
    rows = {}
    short_pos = {"n": "N110", "name": "N130", "surname": "N130", "v1": "M427", "v5": "M420", "adj": "H710"}
    for path in sorted(Path(folder).glob("*.tsv")):
        header = None
        for line in path.read_text(encoding="utf-8").split("\n"):
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if header is None:
                header = fields if fields[0] == "key" else []
                if header:
                    continue
            if header:
                row = dict(zip(header, fields))
                rows.setdefault(row.get("key", ""), []).append((row.get("ko", ""), row.get("pos", ""), row.get("sem", "")))
                continue
            key, ko = fields[0], fields[1] if len(fields) > 1 else ""
            kind = (fields[4] if len(fields) > 4 else fields[2] if len(fields) == 3 else "n").split(":")[0] or "n"
            pos = short_pos.get(kind)
            if pos is None:
                continue
            if pos[0] in "MH":
                key = key[:-1]
            rows.setdefault(key, []).append((ko, pos, ""))
    return rows


def verb_class(verb, rows):
    """'suru', 'kuru', 'ichidan', 'godan' or None when DICTS has no verb row for it."""
    if verb.endswith("する"):
        noun = verb[:-2]
        return "suru" if any(p.startswith("M") or p.startswith("N") for _, p, _ in rows.get(noun, [])) else None
    if verb in ("来る", "くる"):
        return "kuru"
    # The last character of a verb stem's part of speech (M4x7) tells a 1단 verb.
    if verb.endswith("る") and any(p[:2] == "M4" and p[3:] == "7" for _, p, _ in rows.get(verb[:-1], [])):
        return "ichidan"
    if verb[-1:] in GODAN and any(p[:2] == "M4" and p[3:] not in ("7", "9") for _, p, _ in rows.get(verb[:-1], [])):
        return "godan"
    return None


def inflect(verb, cls, form):
    """The form of a verb of class cls (verb_class), or of an i-adjective when cls is 'adj'."""
    if cls == "adj":
        s = verb[:-1]
        table = {"dict": verb, "ku": s + "く", "katta": s + "かった", "kunai": s + "くない", "kute": s + "くて",
                 "kereba": s + "ければ", "ta": s + "かった", "nai": s + "くない", "te": s + "くて", "ba": s + "ければ"}
        if form not in table:
            raise TemplateError(f"no form {form} of an adjective")
        return table[form]
    if cls == "suru":
        n = verb[:-2]
        a, i, e, o, te, ta, stem = n + "さ", n + "し", n + "すれ", n + "しよ", n + "して", n + "した", n + "し"
        nai, passive, causative = n + "しない", n + "される", n + "させる"
    elif cls == "kuru":
        k = verb[:-2] if verb.endswith("くる") else verb[:-1]
        a, i, e, o, te, ta, stem = k + "こ", k + "き", k + "くれ", k + "こよ", k + "きて", k + "きた", k + "き"
        if k == "来":
            a, i, e, o, te, ta, stem = "来", "来", "来れ", "来よ", "来て", "来た", "来"
        nai, passive, causative = a + "ない", a + "られる", a + "させる"
    elif cls == "ichidan":
        s = verb[:-1]
        a, i, e, o, te, ta, stem = s, s, s + "れ", s + "よ", s + "て", s + "た", s
        nai, passive, causative = s + "ない", s + "られる", s + "させる"
    else:
        s, end = verb[:-1], verb[-1]
        ra, ri, re_, ro, te_, ta_ = GODAN[end]
        if verb.endswith("行く") or verb.endswith("いく"):
            te_, ta_ = "って", "った"
        a, i, e, o, te, ta, stem = s + ra, s + ri, s + re_, s + ro, s + te_, s + ta_, s + ri
        nai, passive, causative = s + ra + "ない", s + ra + "れる", s + ra + "せる"
    if cls == "godan" and not verb.endswith("す"):
        cp = a + "される"
    else:
        cp = causative[:-1] + "られる"
    te_stem = te[:-1]
    table = {
        "dict": verb, "stem": stem, "te": te, "ta": ta, "nai": nai, "nakatta": nai[:-1] + "かった",
        "nakereba": nai[:-1] + "ければ", "pass-te": passive[:-1] + "て", "cp-mashita": cp[:-1] + "ました",
        "chatta": te_stem + ("ちゃった" if te[-1] == "て" else "じゃった"),
        "masu": stem + "ます", "mashita": stem + "ました", "masuka": stem + "ますか", "ba": e + "ば",
        "tara": ta + "ら", "tai": stem + "たい", "ou": o + "う", "pass": passive, "pass-ta": passive[:-1] + "た",
        "caus": causative, "caus-ta": causative[:-1] + "た", "cp": cp, "cp-ta": cp[:-1] + "た", "cp-te": cp[:-1] + "て",
        "teiru": te + "いる", "teita": te + "いた", "teireba": te + "いれば", "teru": te_stem + te[-1] + "る",
        "teta": te_stem + te[-1] + "た",
    }
    if cls == "suru":
        table["ou"] = verb[:-2] + "しよう"
    if form not in table:
        raise TemplateError(f"no form {form} of a verb")
    return table[form]


def glosses(word, rows):
    """A regex alternation of the Korean glosses of the noun rows of word (the word itself if none)."""
    ko = sorted({MARKER.sub("", k) for k, p, _ in rows.get(word, []) if p.startswith("N") and MARKER.sub("", k)},
                key=lambda k: (-len(k), k))
    return "(?:" + "|".join(re.escape(k) for k in ko) + ")" if ko else re.escape(word)


def check_word(kind, word, rows):
    """None when word is of kind in rows, else the reason it is not."""
    if kind == "list":
        return None
    if kind in ("noun", "person"):
        found = [s for _, p, s in rows.get(word, []) if p.startswith("N")]
        if not found:
            return "no noun row"
        if kind == "person" and not any(s[i:i + 3] in PERSON_CODES or "QAA" <= s[i:i + 3] <= "QAH"
                                        for s in found for i in range(0, len(s), 3)):
            return "no person sem code"
        return None
    if kind == "adj":
        return None if word.endswith("い") and any(p.startswith("H7") for _, p, _ in rows.get(word[:-1], [])) \
            else "no i-adjective row"
    verb = word.split("+")[-1]
    if kind == "vp" and "+" not in word:
        return "a vp is OBJECT+VERB"
    return None if verb_class(verb, rows) else "no verb row"


def parse(text, name="template"):
    """The families of a template text: dicts with name, spec, min, size, options, slots, cases."""
    families, fam = [], None
    for n, line in enumerate(text.split("\n"), 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        where = f"{name}:{n}"
        if f[0] == "family":
            if len(f) < 3:
                raise TemplateError(f"{where}: family NAME SPEC [MIN]")
            fam = {"name": f[1], "spec": f[2], "min": float(f[3]) if len(f) > 3 and f[3] else 0.0,
                   "size": 20, "options": [], "slots": {}, "cases": [], "where": where}
            families.append(fam)
            continue
        if fam is None:
            raise TemplateError(f"{where}: {f[0]} before a family")
        if f[0] == "size":
            fam["size"] = int(f[1])
        elif f[0] == "options":
            fam["options"] = " ".join(f[1:]).split()
        elif f[0] == "slot":
            if len(f) < 4 or f[2] not in ("person", "noun", "verb", "vp", "adj", "list"):
                raise TemplateError(f"{where}: slot NAME person|noun|verb|vp|adj|list WORD...")
            fam["slots"][f[1]] = (f[2], " ".join(f[3:]).split())
        elif f[0] == "case":
            if len(f) < 3:
                raise TemplateError(f"{where}: case TEMPLATE WANT [REJECT]")
            fam["cases"].append((f[1], f[2], f[3] if len(f) > 3 else "", where))
        else:
            raise TemplateError(f"{where}: unknown line {f[0]}")
    return families


SLOT = re.compile(r"\{([A-Za-z][A-Za-z0-9_]*)(?::([a-z-]+))?\}")
KO = re.compile(r"\{([A-Za-z][A-Za-z0-9_]*)(\.ko)?\}")


def expand(fam, rows, warn):
    """[(ja, want, reject)] of a family: its cases' seeded samples, taken in turn up to its size."""
    slots = {}
    for name, (kind, words) in fam["slots"].items():
        good = []
        for w in words:
            why = check_word(kind, w, rows)
            if why:
                warn(f"{fam['name']}: {w} ({kind}): {why}")
            else:
                good.append(w)
        slots[name] = (kind, good)
    pools = []
    for template, want, reject, where in fam["cases"]:
        names = list(dict.fromkeys(m.group(1) for m in SLOT.finditer(template)))
        for m in KO.finditer(want + reject):
            if m.group(1) not in names:
                raise TemplateError(f"{where}: {{{m.group(1)}}} in WANT or REJECT names no slot of the template")
        for nm in names:
            if nm not in slots:
                raise TemplateError(f"{where}: unknown slot {nm}")
        combos = list(itertools.product(*[slots[nm][1] for nm in names]))
        random.Random(f"{fam['name']}|{template}").shuffle(combos)
        pools.append((template, want, reject, names, combos, where))
    out, seen = [], set()
    while len(out) < fam["size"] and any(p[4] for p in pools):
        for template, want, reject, names, combos, where in pools:
            if not combos or len(out) >= fam["size"]:
                continue
            choice = dict(zip(names, combos.pop(0)))

            def fill(m):
                kind, word = slots[m.group(1)][0], choice[m.group(1)]
                form = m.group(2)
                if kind in ("verb", "vp"):
                    obj, verb = word.rsplit("+", 1) if "+" in word else ("", word)
                    return obj + (inflect(verb, verb_class(verb, rows), form or "dict"))
                if kind == "adj":
                    return inflect(word, "adj", form or "dict")
                if form:
                    raise TemplateError(f"{where}: {m.group(1)} is a {kind}, it has no form {form}")
                return word

            ja = SLOT.sub(fill, template)
            if ja in seen:
                continue
            seen.add(ja)
            def ko(m):
                word = choice[m.group(1)].split("+")[0]
                return glosses(word, rows) if m.group(2) else re.escape(word)

            out.append((ja, KO.sub(ko, want), KO.sub(ko, reject)))
    return out


def translate(cli, dicts, lines, options):
    """The CLI's output for each line (one call, one output line per input line)."""
    with tempfile.TemporaryDirectory() as d:
        src, dst = Path(d) / "in.txt", Path(d) / "out.txt"
        src.write_bytes("".join(line + "\n" for line in lines).encode("utf-8"))
        env = dict(os.environ, HOME=d, HDTRANS_CACHE_DIR="off")
        r = subprocess.run([cli, "-dicts", dicts, "-each", *options, "-in", str(src), "-out", str(dst)],
                           env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if r.returncode != 0:
            raise RuntimeError(f"{cli} failed: {r.stderr.strip()[:500]}")
        out = dst.read_bytes().decode("utf-8").replace("\r\n", "\n").split("\n")
    if len(out) < len(lines):
        raise RuntimeError(f"{cli} wrote {len(out)} lines for {len(lines)}")
    return out[:len(lines)]


def passes(line, want, reject):
    return bool(re.search(want, line)) and not (reject and re.search(reject, line))


def run(cli, dicts, families, rows, extra):
    """{family name: [(ja, out, ok, want, reject)]}, translating each family's lines with its options."""
    results = {}
    for fam in families:
        items = fam["items"]
        outs = translate(cli, dicts, [ja for ja, _, _ in items], fam["options"] + extra) if items else []
        results[fam["name"]] = [(ja, o, passes(o, w, r), w, r) for (ja, w, r), o in zip(items, outs)]
    return results


def rate(res):
    ok = sum(1 for r in res if r[2])
    return ok, len(res), (100.0 * ok / len(res) if res else 0.0)


def self_test():
    rows = {"本": [("책", "N110", "")], "読": [("&HK@", "M435", "")], "食べ": [("&HU@", "M437", "")],
            "勉強": [("공부", "N110", "")], "母": [("어머니", "N110", "VQw")], "石": [("돌", "N110", "XXX")],
            "話": [("&HK@", "M423", "")], "重": [("$AA@", "H710", "")], "帰": [("&AA@", "M428", "")],
            "変え": [("&AA@", "M457", "")], "書": [("&AA@", "M421", "")], "待": [("&AA@", "M424", "")]}
    assert verb_class("読む", rows) == "godan" and verb_class("食べる", rows) == "ichidan"
    assert verb_class("帰る", rows) == "godan" and verb_class("変える", rows) == "ichidan"
    assert verb_class("勉強する", rows) == "suru" and verb_class("飛ぶ", rows) is None
    cases = [("読む", "cp-ta", "読まされた"), ("読む", "chatta", "読んじゃった"), ("食べる", "chatta", "食べちゃった"),
             ("書く", "pass-te", "書かれて"), ("来る", "nakereba", "来なければ"), ("待つ", "cp-mashita", "待たされました"),
             ("食べる", "cp-ta", "食べさせられた"), ("勉強する", "cp-ta", "勉強させられた"),
             ("話す", "cp-ta", "話させられた"), ("読む", "te", "読んで"), ("帰る", "teireba", "帰っていれば"),
             ("食べる", "teru", "食べてる"), ("読む", "teta", "読んでた"), ("勉強する", "ba", "勉強すれば"),
             ("来る", "teireba", "来ていれば"), ("話す", "masuka", "話しますか"), ("帰る", "pass-ta", "帰られた"),
             ("勉強する", "ou", "勉強しよう"), ("読む", "nakatta", "読まなかった"), ("食べる", "ba", "食べれば")]
    for v, form, want in cases:
        got = inflect(v, verb_class(v, rows), form)
        assert got == want, (v, form, got)
    assert inflect("重い", "adj", "kute") == "重くて"
    assert inflect("くる", "kuru", "te") == "きて" and inflect("来る", "kuru", "nai") == "来ない"
    with tempfile.TemporaryDirectory() as d:
        Path(d, "a.tsv").write_text("#! layer=base\n# doc\n走り回る\t뛰어다니다\tv5\n冷たい\t차갑다\tadj\n猫\t고양이\n",
                                    encoding="utf-8")
        short = load_dicts(d)
        assert verb_class("走り回る", short) == "godan" and check_word("adj", "冷たい", short) is None, short
        assert check_word("noun", "猫", short) is None
    assert expand(parse("family\ty\t-\nslot\tP\tperson\t母\ncase\t{P}\t^{P.ko}.{2}$\n")[0], rows, print)[0][1] == \
        "^(?:어머니).{2}$"
    assert check_word("person", "母", rows) is None and check_word("person", "石", rows) == "no person sem code"
    assert check_word("vp", "本を+読む", rows) is None and check_word("vp", "読む", rows)
    text = ("family\tx\tGRM-1\t90\nsize\t3\nslot\tP\tperson\t母 石\nslot\tV\tvp\t本を+読む 本を+食べる\n"
            "case\t{P}に{V:cp-ta}。\t{P.ko} 때문에\t에게\n")
    fams = parse(text)
    warnings = []
    items = expand(fams[0], rows, warnings.append)
    assert warnings == ["x: 石 (person): no person sem code"], warnings
    assert sorted(ja for ja, _, _ in items) == ["母に本を読まされた。", "母に本を食べさせられた。"], items
    assert items[0][1] == "(?:어머니) 때문에" and passes("어머니 때문에 책을 읽었다.", items[0][1], items[0][2])
    assert not passes("어머니에게 때문에", "(?:어머니) 때문에", "에게")
    assert expand(fams[0], rows, lambda _: None) == items
    for bad in ("slot\tP\tperson\t母\n", "family\tx\t-\nslot\tP\tthing\t母\n", "family\tx\t-\nfoo\t1\n"):
        try:
            parse(bad)
        except TemplateError:
            continue
        raise AssertionError(bad)
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    extra = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    dicts, base, fails, listing, check, files, cli = str(ROOT / "dicts"), None, False, False, False, [], None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "-d":
            dicts, i = argv[i + 1], i + 1
        elif a == "--base":
            base, i = (argv[i + 1], argv[i + 2]), i + 2
        elif a == "--fails":
            fails = True
        elif a == "--list":
            listing = True
        elif a == "--check":
            check = True
        elif cli is None and not listing:
            cli = a
        else:
            files.append(a)
        i += 1
    if cli is None and not listing:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    files = files or sorted(str(p) for p in (ROOT / "tests" / "variants").glob("*.tsv"))
    rows = load_dicts(dicts)
    families = []
    try:
        for f in files:
            for fam in parse(Path(f).read_text(encoding="utf-8"), Path(f).name):
                fam["items"] = expand(fam, rows, lambda m: print(f"warning: {m}", file=sys.stderr))
                families.append(fam)
    except TemplateError as e:
        print(f"variants: {e}", file=sys.stderr)
        return 2
    names = [f["name"] for f in families]
    if len(set(names)) != len(names):
        print("variants: a family name is used twice", file=sys.stderr)
        return 2
    if listing:
        for fam in families:
            for ja, _, _ in fam["items"]:
                print(f"{fam['name']}\t{ja}")
        return 0
    try:
        after = run(cli, dicts, families, rows, extra)
        before = run(base[0], base[1], families, rows, extra) if base else None
    except RuntimeError as e:
        print(f"variants: {e}", file=sys.stderr)
        return 2
    low, total_ok, total = [], 0, 0
    for fam in families:
        ok, n, pct = rate(after[fam["name"]])
        total_ok, total = total_ok + ok, total + n
        head = f"{fam['name']}\t{fam['spec']}\t"
        if before:
            bok, bn, bpct = rate(before[fam["name"]])
            print(f"{head}{bok}/{bn} {bpct:.0f}% -> {ok}/{n} {pct:.0f}%")
        else:
            print(f"{head}{ok}/{n} {pct:.0f}%")
        if pct < fam["min"]:
            low.append(f"{fam['name']} {pct:.0f}% < {fam['min']:.0f}%")
        if fails:
            for idx, (ja, out, good, want, reject) in enumerate(after[fam["name"]]):
                if not good:
                    was = f"\tbefore: {before[fam['name']][idx][1]}" if before else ""
                    print(f"  FAIL\t{ja}\t{out}\twant /{want}/" + (f" not /{reject}/" if reject else "") + was)
    print(f"total\t{total_ok}/{total} {100.0 * total_ok / total if total else 0:.0f}%")
    if check and low:
        for m in low:
            print(f"below minimum: {m}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
