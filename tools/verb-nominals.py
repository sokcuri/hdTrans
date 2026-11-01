#!/usr/bin/env python3
"""Writes src/core/dict/builtin/extra/verb_nominals.tsv, the Korean verbs made nouns (버림 버리다, 듦 들다,
꼬집기 꼬집다, 앉음 앉다) that end the Korean of the noun rows of the dictionaries (LEX-40).

  tools/verb-nominals.py [DICTS] [OUT]
  tools/verb-nominals.py --check [DICTS]

The verbs are those the verb rows of DICTS write: a conjugating Korean ending in a verb marker family
of src/core/output/ko_families_gen.cpp (버&AA@ 버리다), or a plain row of part v, v1 or v5 (잊다); the
existentials 있다, 없다 and 계시다 are left out. A verb stem makes its nouns with 기 and with ㅁ after a vowel
(버림; 듦 after ㄹ) or 음 after a consonant (앉음; 걸음 for 걷다, 지음 for 짓다, 도움 for 돕다). A noun enters
the table when it is the last word of the Korean of a noun row of two words or more (잠들어 버림) whose word
before it is no attributive (흐르는 땀, 수줍은 웃음), or the whole Korean of a noun row of two syllables or
more (삼킴); and when it is not itself the Korean of a noun row of kanji read in Sino-Korean (録音 녹음, 再起
재기; src/core/dict/char_readings_gen.cpp) or a word of its own (WORDS: 고함, 그림), and comes from one verb
only. The name dictionaries are not read. Run it again when the dictionaries change; --check exits 1 when the
table differs (ctest dicts.verb-nominals).
"""
import collections
import re
import sys
from pathlib import Path

FAMILY = re.compile(r'\{"(&..@)", "([^"]*)", Pos::Verb, Class::(\w+)')
READING = re.compile(r"\{U'(.)', (?:U'.'|0), U'(.)'\}")


def syllable(c):
    n = ord(c) - 0xAC00
    return (n // 588, n // 28 % 21, n % 28) if 0 <= n < 11172 else None


def with_final(c, final):
    return chr(ord(c) - syllable(c)[2] + final)


# Korean nouns spelled as a verb made a noun that are words of their own (고함 a shout, not 고하다; 그림 a
# picture, not 그리다): a する after them is the noun's 하다.
WORDS = {"고함", "이름", "구름", "그림", "바람", "기름", "고름", "거름", "마름", "어림", "힘", "김", "슬기", "움",
         "춤", "꿈", "잠", "삶", "앎", "땀", "셈", "짐", "보름", "얼음", "다름", "아픔", "기쁨", "슬픔"}


def nominals(stem, cls):
    """The nouns of a verb stem: its 기 noun and its ㅁ or 음 noun."""
    last = syllable(stem[-1])
    out = [stem + "기"]
    if cls == "Digeut" and last[2] == 7:
        out.append(stem[:-1] + with_final(stem[-1], 8) + "음")
    elif cls == "Siot" and last[2] == 19:
        out.append(stem[:-1] + with_final(stem[-1], 0) + "음")
    elif cls == "Bieup" and last[2] == 17:
        out.append(stem[:-1] + with_final(stem[-1], 0) + "움")
    elif last[2] == 8:
        out.append(stem[:-1] + with_final(stem[-1], 10))
    elif last[2] == 0:
        out.append(stem[:-1] + with_final(stem[-1], 16))
    else:
        out.append(stem + "음")
    return out


def attributive(word):
    """Whether word ends as an attributive (흐르는, 수줍은, 잰, 갈): a final ㄴ, or ㄹ but in 을 and 를."""
    s = syllable(word[-1])
    return s is not None and (s[2] == 4 or (s[2] == 8 and word[-1] not in "을를"))


def sino(key, ko, readings):
    """Whether ko is the Sino-Korean reading of the kanji word key (録音 녹음), an initial ㄹ or ㄴ read ㄴ or ㅇ."""
    if len(key) != len(ko) or not all(c in readings for c in key):
        return False
    for k, c in zip(key, ko):
        a, b = syllable(readings[k]), syllable(c)
        if a is None or b is None or a[1:] != b[1:] or (a[0] != b[0] and not (a[0] in (2, 5) and b[0] in (2, 11))):
            return False
    return True


def rows(dicts):
    """(key, Korean, part) of every row of the plain and the column dictionaries but the name dictionaries."""
    for path in sorted(dicts.glob("*.tsv")):
        if path.name.startswith("names"):
            continue
        header = None
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line:
                continue
            cols = line.split("\t")
            if header is None:
                header = cols if cols[0] in ("key", "id") else ["key", "ko", "pos"]
                if cols[0] in ("key", "id"):
                    continue
            if "ko" not in header or "pos" not in header:
                break
            row = dict(zip(header, cols))
            yield row.get("key", ""), row.get("ko", "").strip(), row.get("pos", "")


def table(dicts, families, chars):
    readings = dict(READING.findall(chars.read_text(encoding="utf-8")))
    stems = {}
    for m in FAMILY.finditer(families.read_text(encoding="utf-8")):
        stems[m.group(1)] = (m.group(2), m.group(3))
    verbs = {}
    nouns = []
    sinos = set()
    for key, ko, pos in rows(dicts):
        word = ko.split(" ")[-1] if ko else ""
        if len(pos) == 4 and pos[1] == "4" and len(word) >= 4 and word[-4:] in stems:
            stem, cls = stems[word[-4:]]
            whole = word[:-4] + stem
            if whole and all(syllable(c) for c in whole):
                verbs.setdefault(whole, cls)
        elif pos in ("v", "v1", "v5") and len(word) > 1 and word.endswith("다"):
            whole = word[:-1]
            if all(syllable(c) for c in whole):
                verbs.setdefault(whole, "Digeut" if whole.endswith(("걷", "듣", "묻", "싣", "깨닫")) else "")
        elif (len(pos) == 4 and pos[1] == "1") or pos == "n":
            nouns.append(ko)
            if sino(key, ko, readings):
                sinos.add(ko)
    made = collections.defaultdict(set)
    for stem, cls in verbs.items():
        if stem in ("있", "없", "계시"):
            continue
        for noun in nominals(stem, cls):
            made[noun].add(stem + "다")
    used = set()
    for ko in nouns:
        words = ko.split(" ")
        if len(words) >= 2 and words[-1] in made and words[-2] and not attributive(words[-2]):
            used.add(words[-1])
        elif len(words) == 1 and len(ko) >= 2 and ko in made:
            used.add(ko)
    lines = ["# The Korean verbs made nouns that end the Korean of noun rows, and their verbs (tools/verb-nominals.py)."]
    for noun in sorted(used - sinos - WORDS):
        if len(made[noun]) == 1:
            lines.append(f"{noun}\t{next(iter(made[noun]))}")
    return "\n".join(lines) + "\n"


def main(args):
    here = Path(__file__).resolve().parent.parent
    check = bool(args) and args[0] == "--check"
    if check:
        args = args[1:]
    dicts = Path(args[0]) if args else here / "dicts"
    out = Path(args[1]) if len(args) > 1 else here / "src/core/dict/builtin/extra/verb_nominals.tsv"
    text = table(dicts, here / "src/core/output/ko_families_gen.cpp", here / "src/core/dict/char_readings_gen.cpp")
    if check:
        if out.read_text(encoding="utf-8") != text:
            print(f"{out} is stale: run tools/verb-nominals.py", file=sys.stderr)
            return 1
        return 0
    out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
