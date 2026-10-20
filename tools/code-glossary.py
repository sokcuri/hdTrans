#!/usr/bin/env python3
"""Generate the Korean code glossary from dictionaries, rules and edited meanings.

Usage: tools/code-glossary.py [--root REPO] [--output docs/dictionary-codes.md]
Edit tools/code-meanings.tsv, not the generated Markdown. Meaning patterns use
fnmatch syntax; exact codes take precedence, then the first matching pattern.
"""
import argparse
import csv
import fnmatch
import hashlib
from pathlib import Path
import re
import sys
from collections import Counter, defaultdict


CODE = {"sem": re.compile(r"[A-Z][A-Za-z]{2}"),
        "gram": re.compile(r"[A-Za-z][A-Za-z0-9]")}
FIELDS = {"pos": "PM", "sem": "AT", "gram": "GW"}
OPERAND = re.compile(r'(?<!\w)([PMATGW])[+-]\d+(?:==|!=|-=|\+=|=)\s*"([^"]*)"')


def meaning_for(meanings, kind, code):
    if (kind, code) in meanings:
        return meanings[kind, code]
    for (field, pattern), meaning in meanings.items():
        if field == kind and fnmatch.fnmatchcase(code, pattern):
            return meaning
    return "미분류(추정: 예시를 보고 뜻 보완 필요)"


def references(kind, code, rules):
    result = set()
    for rule, operands in rules:
        for field, value in operands:
            if field not in FIELDS[kind]:
                continue
            if kind == "pos":
                # P/M compare the three bytes after the part-code letter.
                matched = any(fnmatch.fnmatchcase(code[1:], value[i:i + 3])
                              for i in range(0, len(value), 3))
            else:
                # Literal references only, not the semantic family's closure.
                matched = code in value
            if matched:
                result.add(rule)
                break
    return result


def markdown(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace("|", "&#124;").replace("`", "&#96;")
            .replace("\t", "␉").replace("\r", "␍").replace("\n", "␤"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=Path("docs/dictionary-codes.md"))
    args = parser.parse_args()
    root = args.root.resolve()
    meanings = {}
    with (root / "tools/code-meanings.tsv").open(encoding="utf-8", newline="") as source:
        for row in csv.DictReader(source, delimiter="\t", quoting=csv.QUOTE_NONE):
            key = row["kind"], row["code"]
            if key in meanings:
                parser.error(f"duplicate meaning: {key}")
            meanings[key] = row["meaning"]
    counts = defaultdict(Counter)
    examples = defaultdict(dict)
    irregular = defaultdict(Counter)
    rows_read = 0

    def add(kind, code, key, ko, rank):
        counts[kind][code] += 1
        sample = examples[kind, code]
        if key in sample:
            return
        sample[key] = (rank, ko)
        if len(sample) > 8:
            del sample[max(sample, key=lambda k: sample[k][0])]

    rules = []
    # A local rule's body lives in the ctx column of the rows that name it; the same
    # number repeats on the rows of one rule, so distinct bodies are the rules.
    bodies = set()
    for path in sorted((root / "dicts").glob("*.tsv")):
        with path.open(encoding="utf-8", newline="") as source:
            reader = csv.DictReader((line for line in source if not line.startswith(("#", "//"))),
                                    delimiter="\t", quoting=csv.QUOTE_NONE)
            # kana.tsv is a reading map, not a sense dictionary.
            if not {"key", "ko", "pos", "sem", "gram"} <= set(reader.fieldnames or []):
                continue
            for row in reader:
                if row.get("ctx") and row["ctx"] not in bodies:
                    bodies.add(row["ctx"])
                    rules.append(((path.name, reader.line_num), OPERAND.findall(row["ctx"])))
                rows_read += 1
                key, ko = row["key"], row["ko"]
                rank = (len(key) > 12, hashlib.sha256(key.encode()).digest(), key)
                pos = row["pos"]
                if pos:
                    add("pos", pos, key, ko, rank)
                    if len(pos) >= 2 and pos[1] in "0123456789":
                        add("class", pos[1], key, ko, rank)
                for kind, pattern in CODE.items():
                    value = row[kind]
                    for code in set(pattern.findall(value)):
                        add(kind, code, key, ko, rank)
                    if pattern.sub("", value) or any(c[0].islower() for c in pattern.findall(value)):
                        irregular[kind][value] += 1
                        add("irregular-" + kind, value, key, ko, rank)
    for path in sorted((root / "src/core/rules").glob("*.txt")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if re.match(r"^G\s+\d+", line):
                rules.append(((path.name, number), OPERAND.findall(line)))
    # These runtime conditions need documenting even when no dictionary row has them.
    for kind, code in meanings:
        if kind == "gram" and CODE[kind].fullmatch(code):
            counts[kind].setdefault(code, 0)
    total = sum(len(counts[k]) for k in ("pos", "sem", "gram"))
    inferred = sum("추정" in meaning_for(meanings, kind, code)
                   for kind in ("pos", "sem", "gram") for code in counts[kind])
    lines = [
        "# 품사, 의미, 문법 코드 일람", "",
        "이 문서는 `tools/code-glossary.py`가 생성합니다. 뜻을 고칠 때는 `tools/code-meanings.tsv`를 편집하고 실행합니다.", "",
        "```sh", "python3 tools/code-glossary.py", "```", "",
        "의미 표의 열은 `kind code meaning`입니다. `kind`는 `class`, `pos`, `sem`, `gram`이며 "
        "`code`는 정확한 코드 또는 `?`(한 글자), `*`(여러 글자) 패턴입니다. 정확한 코드가 먼저이고, "
        "패턴끼리는 위에 적힌 행이 먼저입니다. 모호한 뜻은 `추정` 표시를 남깁니다.", "",
        f"사전 {rows_read:,}행에서 전체 품사 코드 {len(counts['pos'])}개, 의미 코드 {len(counts['sem'])}개, "
        f"문법 코드 {len(counts['gram'])}개를 정리했습니다(합계 {total}개, 뜻에 **추정** 표시 {inferred}개). "
        "사전에 없지만 실행 시 검사하는 문법 코드도 포함합니다. 품사 첫 숫자 부류 10개는 별도 표입니다.", "",
        "## 읽는 법", "",
        "- **행 수**는 `dicts/*.tsv`에서 그 코드가 적힌 행 수입니다. 같은 행에 같은 코드가 두 번 있어도 한 번 셉니다. "
        "빈 열의 기본값, `flags=xxx`로 더하는 `XXX`, 내장 표는 합산하지 않습니다. `kana.tsv`는 읽기 표라 제외합니다.",
        "- **규칙 수**는 서로 다른 규칙 본문의 수입니다(사전 행의 `ctx` 열과 `src/core/rules/globals.txt`의 G 줄). 주석, 예시를 빼고 P/M(품사), "
        "A/T(의미), G/W(문법)의 따옴표 인자를 셉니다. 품사는 `1**` 같은 와일드카드를 포함하며, "
        "의미, 문법은 리터럴 참조만 셉니다(상위 의미 계열 확장은 제외). 실행 횟수나 발동 보장은 아닙니다.",
        "- 예시는 키가 다른 최대 8개를 재현 가능한 순서로 뽑았습니다. 키가 적으면 전부이며, `—`는 사전 예시가 없음입니다. "
        "한국어의 `&AV@` 등은 원래 사전의 활용 표지입니다. 예시는 올바른 태그나 역어를 보장하지 않습니다.",
        "- 뜻은 기존 문서, 품사 대응 표, 예시를 바탕으로 정리했습니다. **추정**은 세부 범위가 확정되지 않았다는 뜻입니다. "
        "형태소 품사 대응은 `src/core/dict/pos_tags_gen.cpp`, 의미 계열은 `src/core/rules/attr_families_gen.cpp`를 봅니다.",
        "- 의미는 영문 대문자로 시작하는 영문 3자, 문법은 영문자로 시작하는 영문, 숫자 2자 단위를 찾아 셉니다. "
        "공백, 오타, 다른 열의 값이 섞인 셀은 아래 **비정형 값**에도 원문 그대로 남깁니다. "
        "엔진은 공백을 제거하지 않고 바이트를 검사하므로 이 일람의 코드 수와 실제 조건 일치가 다를 수 있습니다.",
        "- 조건, 대입의 뜻은 [rule-language.md](rule-language.md#코드-체계), 열과 계층은 [dictionary-files.md](dictionary-files.md)에 있습니다.", "",
    ]

    def table(kind, codes):
        lines.extend(["| 코드 | 뜻 | 행 수 | 규칙 수 | 키 → 한국어 예시 |",
                      "|---|---|---:|---:|---|"])
        for code in codes:
            if kind == "class":
                refs = set()
                for pos in counts["pos"]:
                    if pos[1:2] == code:
                        refs.update(references("pos", pos, rules))
            else:
                refs = references(kind, code, rules)
            sample = examples[kind, code]
            rendered = "; ".join(f"{markdown(key)} → {markdown(ko)}"
                                 for key, (_, ko) in sorted(sample.items())) or "—"
            label = code + "**" if kind == "class" else code
            lines.append(f"| `{markdown(label)}` | {markdown(meaning_for(meanings, kind, code))} "
                         f"| {counts[kind][code]:,} | {len(refs):,} | {rendered} |")
        lines.append("")

    lines += ["## 품사 부류 (P, M)", "",
              "전체 품사 `N110`은 태그 문자 `N`과 품사키 `110`입니다. 규칙 P/M은 태그 문자를 뺀 "
              "세 바이트를 비교하므로 `1**`가 명사, `4**`가 동사입니다. 첫 숫자는 전체 코드의 두 번째 문자입니다.", ""]
    table("class", sorted(counts["class"]))
    lines += ["## 전체 품사 코드", ""]
    for digit in sorted(counts["class"]):
        lines += [f"### {digit}: {meaning_for(meanings, 'class', digit)}", ""]
        table("pos", sorted(c for c in counts["pos"] if c[1:2] == digit))
    for kind, title in (("sem", "의미 코드 (A, T)"), ("gram", "문법 코드 (G, W)")):
        lines += [f"## {title}", ""]
        table(kind, sorted(counts[kind]))
    lines += ["## 비정형 값", "",
              "열 너비와 맞지 않거나 공백 등이 섞인 셀입니다. 의도한 코드로 자동 교정하지 않습니다. "
              "공백은 `␠`로 표시했습니다. 뜻을 단정하지 말고 해당 행과 `-why`를 확인합니다.", "",
              "| 열 | 원문 값 | 행 수 | 키 → 한국어 예시 |", "|---|---|---:|---|"]
    for kind in ("sem", "gram"):
        for value, count in sorted(irregular[kind].items()):
            sample = examples["irregular-" + kind, value]
            rendered = "; ".join(f"{markdown(key)} → {markdown(ko)}"
                                 for key, (_, ko) in sorted(sample.items()))
            lines.append(f"| {kind} | `{markdown(value).replace(' ', '␠')}` | {count:,} | {rendered} |")
    output = args.output if args.output.is_absolute() else root / args.output
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{total} codes ({len(counts['pos'])} pos, {len(counts['sem'])} sem, "
          f"{len(counts['gram'])} gram), {inferred} inferred meanings", file=sys.stderr)


if __name__ == "__main__":
    main()
