#!/usr/bin/env python3
"""Checks that the noun rows that do not read as a bare noun carry the gram code Nn (spec LAY-15).

  tools/nn-rows.py --check [ROOT]   rows of the base and words layers of ROOT/dicts (default: the
                                    repository) that need Nn and lack it, against
                                    tools/nn-allowance.tsv
  tools/nn-rows.py --list [ROOT]    every such row, allowed or not
  tools/nn-rows.py --self-test

A row needs Nn when its pos is a noun other than a proper noun (x1yy, y not 3) and either its key
ends in one of PARTICLES after at least one character and its Korean ends in one of KOREAN (答えは
대답은), or its Korean ends in one of ADVERBS (当然 당연히). Nn also goes by hand on rows whose Korean
is an interjection, an adnominal or a conjunction (いや 아니, いわゆる 이른바, あと 그리고), which no
shape finds. The allowance lists the rows of either shape that read as a bare noun as
file<TAB>key<TAB>ko; --check fails on a row that needs Nn and is not listed, on a listed row that
no longer has the shape or now has Nn, and on a line listed twice, so the allowance only shrinks.
Exit: 0 clean, 1 findings, 2 invalid arguments.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARTICLES = ("が", "を", "に", "へ", "と", "で", "から", "より", "まで", "の", "は", "も", "には", "では", "とは",
             "にも", "でも", "とも", "ので", "て")
KOREAN = ("이", "가", "을", "를", "에", "에서", "에게", "한테", "로", "으로", "와", "과", "랑", "하고", "의", "은",
          "는", "도", "까지", "부터", "보다", "만", "니까", "서", "고", "며", "지만", "게", "듯이", "처럼", "대로", "께")
ADVERBS = ("히", "하게", "스럽게", "롭게", "없이", "같이")


def needs_nn(key, ko, pos, gram):
    if len(pos) < 4 or pos[1] != "1" or pos[2] == "3" or "Nn" in [gram[i:i + 2] for i in range(0, len(gram), 2)]:
        return False
    ko = ko.strip()
    particle = any(key.endswith(p) and len(key) > len(p) for p in PARTICLES) and any(ko.endswith(k) for k in KOREAN)
    return particle or ko.endswith(ADVERBS)


def rows(text):
    lines = text.split("\n")
    if not lines or lines[0].split("=")[-1].strip() not in ("base", "words"):
        return
    header = None
    for line in lines:
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if header is None:
            header = cells
            continue
        row = dict(zip(header, cells))
        yield row.get("key", ""), row.get("ko", ""), row.get("pos", ""), row.get("gram", "")


def found(root):
    out = set()
    for path in sorted((root / "dicts").glob("*.tsv")):
        for key, ko, pos, gram in rows(path.read_text(encoding="utf-8")):
            if needs_nn(key, ko, pos, gram):
                out.add((path.name, key, ko.strip()))
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
    allowed, twice = allowance((ROOT / "tools" / "nn-allowance.tsv").read_text(encoding="utf-8"))
    bad = 0
    for f, key, ko in twice:
        print(f"tools/nn-allowance.tsv: {f} {key} {ko}: listed twice")
        bad += 1
    for f, key, ko in sorted(rows_found - allowed):
        print(f"{f}: {key} {ko}: a noun row that does not read as a bare noun needs gram Nn")
        bad += 1
    for f, key, ko in sorted(allowed - rows_found):
        print(f"tools/nn-allowance.tsv: {f} {key} {ko}: no longer needs an allowance; remove the row")
        bad += 1
    return 1 if bad else 0


def self_test():
    cases = [
        (("答えは", "대답은", "K110", ""), True), (("答えは", "대답은", "K110", "Nn"), False),
        (("当然", "당연히", "N110", ""), True), (("当然", "당연히", "N110", "BhNn"), False),
        (("当然", "당연히", "N110", "ANnB"), True), (("ペニヒ", "페니히", "N130", ""), False),
        (("心から", "진심으로", "N110", ""), True), (("かが", "가가", "N130", ""), False),
        (("答え", "대답", "N110", ""), False), (("は", "은", "N110", ""), False),
        (("用に", "용으로", "P200", ""), False),
    ]
    for args, want in cases:
        if needs_nn(*args) != want:
            print(f"needs_pt{args} != {want}")
            return 1
    text = "#! layer=base\n# t\nkey\tko\tpos\tsem\tgram\n用に\t용으로\tN110\t\t\n葉\t잎\tN110\t\t\n"
    if list(rows(text)) != [("用に", "용으로", "N110", ""), ("葉", "잎", "N110", "")]:
        print("rows")
        return 1
    if list(rows("#! layer=names\nkey\tko\tpos\tgram\nかが\t가가\tN110\t\n")):
        print("names layer")
        return 1
    if allowance("# c\nfile\tkey\tko\na.tsv\tかに\t게\na.tsv\tかに\t게\n")[1] != [("a.tsv", "かに", "게")]:
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
