#!/usr/bin/env python3
"""Checks that dictionary targets spelling a Japanese reading write ツ as 츠 and plain kana without
tense consonants (spec LEX-3950).

  tools/kana-spelling.py --check [ROOT]   rows of the base, words and names layers of ROOT/dicts
                                          (default: the repository) that break the rule, against
                                          tools/kana-spelling-allowance.tsv
  tools/kana-spelling.py --list [ROOT]    every such row, allowed or not
  tools/kana-spelling.py --self-test

A row whose key, or else whose reading column, is kana is aligned with its Korean by mora and
syllable. It breaks the rule when つ or ツ is written 쓰 or 쯔, or when a hiragana か, き, く, け, こ,
た, ち, て, と or ぱ-row kana after the first syllable of a word is written with ㄲ, ㄸ, ㅃ or ㅉ
(はなこ 하나꼬). ちゃん and たん after a name keep 짱 and 땅 (LEX-30), a word whose first syllable is
already tense imitates a sound (ぷよぷよ 뿌요뿌요), and katakana written with tense consonants is a
loanword (LEX-1). A row whose kana does not align as a whole breaks the rule when a kana run of its key or
reading is spelled by two or more syllables in a row of one Hangul word with 쓰 or 쯔 for a つ (つくば万博
쓰쿠바 세계 박람회, ゴーストオブツシマ 고스트 오브 쓰시마). A proper noun row with a kanji key and no reading that aligns breaks the rule when
a Hangul word of its target that kana can spell holds 쓰 or 쯔, or ends in 꼬 for a key ending in 子
(夏月 나쓰키, 花子 하나꼬). The allowance lists the rows of that shape that are not Japanese readings
(四平市 쓰핑시, クズ二号 쓰레기 2호) as file<TAB>key<TAB>ko; --check fails on a row that breaks the
rule and is not listed, on a listed row that no longer does and on a line listed twice, so the
allowance only shrinks.
Exit: 0 clean, 1 findings, 2 invalid arguments.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWANCE = "kana-spelling-allowance.tsv"

ROWS = {"": "あいうえお", "k": "かきくけこ", "g": "がぎぐげご", "s": "さしすせそ", "z": "ざじずぜぞ", "t": "たちつてと",
        "d": "だぢづでど", "n": "なにぬねの", "h": "はひふへほ", "b": "ばびぶべぼ", "p": "ぱぴぷぺぽ",
        "m": "まみむめも", "r": "らりるれろ"}
MORA = {k: (c, "aiueo"[i]) for c, ks in ROWS.items() for i, k in enumerate(ks)}
MORA.update({"し": ("sh", "i"), "じ": ("j", "i"), "ち": ("ch", "i"), "つ": ("ts", "u"), "ぢ": ("j", "i"),
             "づ": ("z", "u"), "ふ": ("f", "u"), "や": ("", "ya"), "ゆ": ("", "yu"), "よ": ("", "yo"),
             "わ": ("", "wa"), "を": ("", "o"), "ゐ": ("", "i"), "ゑ": ("", "e"), "ゔ": ("v", "u")})
SMALL_Y = {"ゃ": "ya", "ゅ": "yu", "ょ": "yo"}
SMALL_V = {"ぁ": "a", "ぃ": "i", "ぅ": "u", "ぇ": "e", "ぉ": "o"}
# Initial consonant indexes (ㄱ 0 … ㅎ 18) a consonant is written with, tense ones included.
INITIALS = {"": {11}, "k": {0, 15, 1}, "g": {0}, "s": {9, 10}, "sh": {9, 10}, "z": {12}, "j": {12},
            "t": {3, 16, 4}, "ch": {12, 14, 13}, "ts": {10, 14, 13}, "d": {3}, "n": {2}, "h": {18}, "f": {18},
            "fv": {17, 18}, "p": {17, 8}, "b": {7}, "v": {7}, "m": {6}, "r": {5}}
VOWELS = {"a": {0}, "i": {20}, "u": {13}, "e": {5}, "o": {8}, "ya": {2}, "yu": {17}, "yo": {12}, "wa": {9},
          "wi": {16}, "we": {15}, "wo": {14, 8}, "ye": {7}}
LONG = {("o", "u"), ("o", "o"), ("u", "u"), ("yo", "u"), ("yo", "o"), ("yu", "u"), ("e", "i"), ("i", "i"),
        ("a", "a"), ("e", "e")}
FINAL_Q, FINAL_N = {19, 1, 17, 7}, {4, 21, 16}
TENSE = {1, 4, 8, 13}  # ㄲ ㄸ ㅃ ㅉ
PLAIN = {"k", "t", "p", "ch"}
TSU = {"쓰", "쯔"}
KANA_RUN = re.compile("[ぁ-ゖァ-ヺー]+")
HANGUL_RUN = re.compile("[가-힣]+")


def split(c):
    n = ord(c) - 0xAC00
    return (n // 588, n % 588 // 28, n % 28) if 0 <= n < 11172 else None


def vowels(cons, v):
    out = set(VOWELS.get(v, ()))
    if v == "u" and cons in ("s", "sh", "z", "j", "ts", "ch"):
        out.add(18)
    if v in ("ya", "yu", "yo") and cons in ("j", "ch", "z", "sh", "ts"):
        out |= VOWELS[v[1:]]
    if cons == "sh" and v == "e":
        out.add(7)
    return out


def morae(s):
    """(consonant, vowel, kana) of each mora of the kana s; consonant Q for っ, N for ん, L for ー."""
    out, i = [], 0
    while i < len(s):
        c = s[i]
        katakana = "ァ" <= c <= "ヶ"
        h = chr(ord(c) - 0x60) if katakana else c
        nxt = s[i + 1] if i + 1 < len(s) else ""
        nxt = chr(ord(nxt) - 0x60) if "ァ" <= nxt <= "ヶ" else nxt
        if h in "っんー":
            out.append(({"っ": "Q", "ん": "N", "ー": "L"}[h], "", c))
            i += 1
            continue
        if h not in MORA:
            return None
        cons, v = MORA[h]
        if nxt in SMALL_Y:
            v, i = SMALL_Y[nxt], i + 1
        elif nxt in SMALL_V:
            if cons == "" and v == "u" and SMALL_V[nxt] != "u":
                v = "w" + SMALL_V[nxt]
            else:
                cons, v = ("fv" if cons == "f" else cons), SMALL_V[nxt]
            i += 1
        out.append((cons, v, c))
        i += 1
    return out


def align(ms, sy):
    """The mora index of each syllable of sy when sy spells the morae ms, else None."""
    parts = [split(c) for c in sy]
    if any(p is None for p in parts):
        return None
    memo = {}

    def go(i, j, prev):
        if (i, j, prev) in memo:
            return memo[i, j, prev]
        memo[i, j, prev] = None
        if i == len(ms):
            memo[i, j, prev] = () if j == len(sy) else None
            return memo[i, j, prev]
        cons, v, _ = ms[i]
        r = None
        if cons in ("Q", "L") or (cons == "" and (prev, v) in LONG):
            r = go(i + 1, j, prev)
        if r is None and cons not in ("Q", "N", "L") and j < len(sy):
            ini, vo, fin = parts[j]
            nxt = ms[i + 1][0] if i + 1 < len(ms) else ""
            if ini in INITIALS.get(cons, ()) and vo in vowels(cons, v) and not (ini == 10 and vo == 18 and cons != "ts"):
                step = 1 if fin == 0 else 2 if (nxt == "Q" and fin in FINAL_Q) or (nxt == "N" and fin in FINAL_N) else 0
                rest = go(i + step, j + 1, v) if step else None
                if rest is not None:
                    r = (i,) + rest
        memo[i, j, prev] = r
        return r

    return go(0, 0, "")


def tsu_in_part(ms, sy):
    """Whether two or more syllables in a row of sy spell morae of ms in a row, one of them 쓰 or 쯔 for a つ."""
    parts = [split(c) for c in sy]
    none = -len(sy) - 1
    memo = {}

    def go(i, j, prev):
        # (most syllables spelled from sy[j] on against ms[i] on, most of them on a path through a つ written 쓰 or 쯔)
        if (i, j, prev) in memo:
            return memo[i, j, prev]
        best, tsu = 0, none
        if i < len(ms) and j < len(sy):
            cons, v, _ = ms[i]
            if cons in ("Q", "L") or (cons == "" and (prev, v) in LONG):
                n, t = go(i + 1, j, prev)
                best, tsu = max(best, n), max(tsu, t)
            if cons not in ("Q", "N", "L") and parts[j] is not None:
                ini, vo, fin = parts[j]
                nxt = ms[i + 1][0] if i + 1 < len(ms) else ""
                if ini in INITIALS.get(cons, ()) and vo in vowels(cons, v) and not (ini == 10 and vo == 18 and cons != "ts"):
                    step = 1 if fin == 0 else 2 if (nxt == "Q" and fin in FINAL_Q) or (nxt == "N" and fin in FINAL_N) else 0
                    if step:
                        n, t = go(i + step, j + 1, v)
                        best, tsu = max(best, n + 1), max(tsu, t + 1, n + 1 if cons == "ts" and sy[j] in TSU else none)
        memo[i, j, prev] = best, tsu
        return best, tsu

    return any(go(i, j, ms[i - 1][1] if i else "")[1] >= 2 for i in range(len(ms)) for j in range(len(sy)))


def kana_only(s):
    return bool(s) and all("ぁ" <= c <= "ゖ" or "ァ" <= c <= "ヺ" or c in "ー・ 　" for c in s)


def spellable(c):
    """Whether kana can spell the syllable c (tense consonants and the finals ㄴ ㅁ ㅅ ㅇ included)."""
    p = split(c)
    if p is None or p[2] not in (0, 4, 16, 19, 21):
        return False
    return any(p[0] in inis and any(p[1] in vowels(cons, v) for v in VOWELS if not v.startswith("w") or cons == "")
               for cons, inis in INITIALS.items())


def breaks(key, ko, reading, pos):
    """Whether the row writes a Japanese reading with 쓰, 쯔 or a tense consonant for plain kana."""
    sy = [c for c in ko if split(c)]
    starts = {i for i, c in enumerate(ko) if split(c) and (i == 0 or not split(ko[i - 1]))}
    first = [i in starts for i, c in enumerate(ko) if split(c)]
    source = key if kana_only(key) else reading if kana_only(reading) else ""
    ms = morae("".join(c for c in source if c not in "・ 　")) if source else None
    at = align(ms, sy) if ms else None
    if at is None and any("쓰" in w or "쯔" in w for w in ko.split()):
        # A kana part of the key or reading spelled by part of a Hangul word (つくば万博 쓰쿠바 세계 박람회).
        pieces = [p for s in (key, reading if kana_only(reading) else "") for p in KANA_RUN.findall(s)]
        words = [w for w in HANGUL_RUN.findall(ko) if "쓰" in w or "쯔" in w]
        if any(ms_ and tsu_in_part(ms_, w) for p in pieces for ms_ in [morae(p)] for w in words):
            return True
    if at is None and kana_only(key):
        return False
    if at is not None:
        # A word that starts with a tense consonant for plain kana imitates the sound throughout
        # (ぷよぷよ 뿌요뿌요, ちゃんぽん 짬뽕); the 짱 of a final ちゃん is no such word (せつなちゃん 세츠나 짱).
        def final_honorific(i):
            return ms[i][2] in "ちチたタ" and i + 2 == len(ms) and ms[i + 1][0] == "N"

        if any(first[j] and ms[i][0] in PLAIN and split(sy[j])[0] in TENSE and not final_honorific(i)
               for j, i in enumerate(at)):
            return False
        for j, i in enumerate(at):
            cons, v, kana = ms[i]
            if cons == "ts" and sy[j] in TSU:
                return True
            honorific = kana in "ちチたタ" and i + 1 < len(ms) and ms[i + 1][0] == "N"
            if (cons in PLAIN and split(sy[j])[0] in TENSE and not first[j] and not honorific
                    and not "ァ" <= kana <= "ヶ"):
                return True
        return False
    if len(pos) < 3 or pos[1:3] != "13" or not any("一" <= c <= "鿿" or c == "々" for c in key):
        return False
    # Runs of two or more syllables that kana can spell, a Korean word after them left out (쓰루미선).
    run = []
    for c in ko + " ":
        if split(c) and spellable(c):
            run.append(c)
            continue
        if len(run) > 1 and (any(x in TSU for x in run) or (key.endswith("子") and run[-1] == "꼬" and not split(c))):
            return True
        run = []
    return False


def rows(text):
    lines = text.split("\n")
    if not lines or lines[0].split("=")[-1].strip() not in ("base", "words", "names"):
        return
    header = None
    for line in lines:
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        cells = line.split("\t")
        if header is None and cells[0] == "key":
            header = cells
            continue
        row = dict(zip(header, cells)) if header else {"key": cells[0], "ko": cells[1] if len(cells) > 1 else ""}
        yield row.get("key", ""), row.get("ko", ""), row.get("reading", ""), row.get("pos", "")


def found(root):
    out = set()
    for path in sorted((root / "dicts").glob("*.tsv")):
        for key, ko, reading, pos in rows(path.read_text(encoding="utf-8")):
            if breaks(key, ko, reading, pos):
                out.add((path.name, key, ko))
    return out


def allowance(text):
    out, twice = set(), []
    for line in text.split("\n"):
        if not line or line.startswith("#") or line.startswith("file\t"):
            continue
        row = tuple(line.split("\t")[:3])
        if row in out:
            twice.append(row)
        out.add(row)
    return out, twice


def check(root):
    rows_found = found(root)
    allowed, twice = allowance((ROOT / "tools" / ALLOWANCE).read_text(encoding="utf-8"))
    bad = 0
    for f, key, ko in twice:
        print(f"tools/{ALLOWANCE}: {f} {key} {ko}: listed twice")
        bad += 1
    for f, key, ko in sorted(rows_found - allowed):
        print(f"{f}: {key} {ko}: a Japanese reading writes ツ as 츠 and plain kana without tense consonants")
        bad += 1
    for f, key, ko in sorted(allowed - rows_found):
        print(f"tools/{ALLOWANCE}: {f} {key} {ko}: no longer needs an allowance; remove the row")
        bad += 1
    return 1 if bad else 0


def self_test():
    cases = [
        (("はなこ", "하나꼬", "", "N130"), True), (("はなこ", "하나코", "", "N130"), False),
        (("つかさ", "쓰카사", "", "N130"), True), (("ツカサ", "쯔카사", "", "N130"), True),
        (("司", "쓰카사", "つかさ", "N130"), True), (("司", "츠카사", "つかさ", "N130"), False),
        (("花子", "하나꼬", "", "N130"), True), (("夏月", "나쓰키", "", "N130"), True),
        (("夏月", "나츠키", "", "N130"), False), (("ゴミ", "쓰레기", "", "N110"), False),
        (("ウェザースリー", "웨더쓰리", "", "N130"), False), (("ジュゼッペ", "쥬제뻬", "", "N130"), False),
        (("ふたばちゃん", "후타바짱", "", "N130"), False), (("ぷよぷよ", "뿌요뿌요", "", "N130"), False),
        (("ぴよぴよ", "삐요피요", "", "N130"), False), (("書く", "쓰다", "", "M448"), False),
        (("満喫", "만끽", "", "N110"), False), (("四川", "쓰촨", "", "N130"), False),
        (("内海", "우쓰미", "", "N130"), True), (("リツコ", "리츠꼬", "", "N130"), False),
        (("いっぽ", "잇뽀", "", "N130"), True), (("いっぽ", "잇포", "", "N130"), False),
        (("鶴見線", "쓰루미선", "つるみせん", "N130"), True), (("鶴見線", "츠루미선", "つるみせん", "N130"), False),
        (("せつ菜ちゃん", "세쓰나 짱", "せつなちゃん", "N110"), True), (("せつ菜ちゃん", "세츠나 짱", "せつなちゃん", "N110"), False),
        (("ちゃんぽん", "짬뽕", "", "I110"), False), (("つくば万博", "쓰쿠바 세계 박람회", "", "N110"), True),
        (("つくば万博", "츠쿠바 세계 박람회", "", "N110"), False), (("ゴーストオブツシマ", "고스트 오브 쓰시마", "", "N130"), True),
        (("ツナミブースト", "쓰나미 부스트", "", "N130"), True), (("ゴミ箱", "쓰레기통", "", "N110"), False),
    ]
    for args, want in cases:
        if breaks(*args) != want:
            print(f"breaks{args} != {want}")
            return 1
    text = "#! layer=names\n# t\nkey\tko\tpos\treading\n花子\t하나꼬\tN130\t\n"
    if list(rows(text)) != [("花子", "하나꼬", "", "N130")]:
        print("rows")
        return 1
    if list(rows("#! layer=pack\nkey\tko\n花子\t하나꼬\n")):
        print("pack layer")
        return 1
    if allowance("# c\nfile\tkey\tko\na.tsv\t四平\t쓰핑\na.tsv\t四平\t쓰핑\n")[1] != [("a.tsv", "四平", "쓰핑")]:
        print("twice")
        return 1
    print("ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv and argv[0] in ("--check", "--list") and len(argv) <= 2:
        root = Path(argv[1]) if len(argv) == 2 else ROOT
        if argv[0] == "--list":
            for row in sorted(found(root)):
                print("\t".join(row))
            return 0
        return check(root)
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
