#!/usr/bin/env python3
"""List the dictionary and ending decisions that a judged corpus marks as wrong.

Usage: tools/dict-gaps.py CLI DICTS VERDICTS_DIR OUT.tsv [--min-count N]
VERDICTS_DIR holds <set>.ja.txt and <set>.verdicts.tsv files. A verdict row is
line, ja, out, verdict, cause, span, fix; a row other than ok is bad and names the
Japanese span that should have read differently.

Each bad line is translated again and every word decision of that translation is
joined to its candidate rows through the -trace dump. A decision counts when the
word is part of the span, or holds it, and its part code is not a particle, an
ending or a symbol: fix is a corrected span, so a particle that differs from it
says nothing about the dictionary. Comparing words with the span as text is what
lets the tool work without offsets, which the dumps count per sentence.

OUT.tsv columns: word, korean, class, candidates, source, cause, defect, count, ja,
fix. class is the part class of the chosen row, candidates is how many rows read
the word there, and defect is what to change:
- assembly: the chosen text keeps a marker (&AV@, $FA@, %A1@) in the output, so the
  ending did not expand it.
- missing: a kanji row with no Korean at all (a marker or nothing).
- choice: several rows read the word and the chosen Korean is absent from fix.
- content: one row reads the word and its Korean is absent from fix; the row is
  wrong or lacks the sense.
A verb or adjective stem is only checked for a marker left in the output: its Korean
is a fragment that the ending completes (留め &C7@ with the ending 6 reads 두다), so
it cannot be compared with fix. A row of markers alone and a single kana (ら, あ, め)
are pieces of the same kind. A decision whose Korean occurs in fix agrees with the
judgement and is not written. count is how often the same word is wrong this way, so
the largest counts are worth fixing first.
"""
import argparse
import collections
import csv
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

MARKER = re.compile(r"[&$%][A-Za-z][0-9A-Za-z]@")
KANJI = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]")
PART_CLASSES = {"0": "determiner", "1": "noun", "2": "particle", "3": "adverb",
                "4": "verb-stem", "5": "ending", "6": "symbol", "7": "adjective-stem",
                "8": "adjectival-noun", "9": "phrase"}
CONTENT = {"determiner", "noun", "adverb", "verb-stem", "adjective-stem", "adjectival-noun", "phrase"}
STEM = {"verb-stem", "adjective-stem"}


def read_verdicts(directory):
    rows = []
    for path in sorted(Path(directory).glob("*.verdicts.tsv")):
        chapter = path.name[: -len(".verdicts.tsv")]
        source = Path(directory) / f"{chapter}.ja.txt"
        if not source.is_file():
            continue
        lines = source.read_text(encoding="utf-8").splitlines()
        with path.open(encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            if not {"line", "verdict", "span", "fix"} <= set(reader.fieldnames or ()):
                continue
            for row in reader:
                if not row["line"].isdigit():
                    continue
                number = int(row["line"])
                if not 1 <= number <= len(lines):
                    continue
                rows.append({"chapter": chapter, "line": number, "ja": lines[number - 1],
                             "verdict": row["verdict"], "cause": row.get("cause", ""),
                             "span": row.get("span", ""), "fix": row.get("fix", "")})
    return rows


def translate(cli, dicts, texts, tracePath):
    result = subprocess.run([str(cli), "-dicts", str(dicts), "--each", "--json",
                             "-trace", str(tracePath)],
                            input="\n".join(texts) + "\n", capture_output=True,
                            text=True, encoding="utf-8", check=True)
    return json.loads(result.stdout)


def part_rows(tracePath):
    """The candidate rows of every word decision, keyed as the JSON choices are.

    The trace numbers lines from one; a key is the line, chunk, sentence, token and
    start of the decision in both dumps.
    """
    rows = {}
    for line in tracePath.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if fields[0] != "CAND" or len(fields) < 11:
            continue
        key = (str(int(fields[1]) - 1), fields[2], fields[3], fields[4], fields[5])
        part = fields[10][:1]
        rows.setdefault(key, []).append({"index": int(fields[7]), "chosen": fields[8] == "1",
                                         "class": PART_CLASSES.get(part, "")})
    return rows


def choice_key(choice):
    return (str(choice["line"]), str(choice["chunk"]), str(choice["sentence"]),
            str(choice["token"]), str(choice["start"]))


def chosen_class(rows):
    for row in rows:
        if row["chosen"]:
            return row["class"]
    return ""


def overlaps(choice, span):
    """Whether the word is part of the span or holds it.

    The offsets of the decisions are counted from the start of their sentence, so
    the span is compared as text instead: a word outside the span that shares a
    sentence would otherwise be inside it by the number.
    """
    surface = choice.get("surface", "")
    return bool(surface) and (surface in span or span in surface)


def defect_of(choice, rows, part, output, fix):
    """The class of defect for a word decision, or "" when it is not one."""
    text = choice.get("choice", {}).get("text", "")
    if any(marker in output for marker in MARKER.findall(text)):
        return "assembly"
    # A stem carries a fragment that the ending turns into a word, so its Korean
    # cannot be compared with the fixed span; only a marker left behind is a defect.
    # A row of markers alone (留め &C7@, 様 %B3@) is the same: the ending expands it.
    if part in STEM:
        return ""
    korean = MARKER.sub("", text).strip()
    if not korean and MARKER.search(text):
        return ""
    # A single kana is a piece of a conjugation (ら, あ, め), not a word of its own.
    surface = choice.get("surface", "")
    if len(surface) == 1 and not KANJI.search(surface):
        return ""
    # An allomorph written as a particle form ((이)가) never occurs in a translation.
    if korean.startswith("("):
        return ""
    if not korean:
        # A row with no word at all: a kanji word should read something, while an
        # empty row for a one-character prefix (御) is a filler.
        return "missing" if KANJI.search(choice.get("surface", "")) and len(surface) > 1 else ""
    if korean in MARKER.sub("", fix):
        return ""
    return "choice" if len(rows) > 1 else "content"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cli", type=Path, metavar="CLI")
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("verdicts", type=Path, metavar="VERDICTS_DIR")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--min-count", type=int, default=2,
                        help="write only words wrong at least this often")
    args = parser.parse_args()
    for path, kind in ((args.cli, "CLI"), (args.dicts, "dictionary folder"), (args.verdicts, "verdict folder")):
        if not path.exists():
            parser.error(f"no {kind} at {path}")

    header = ["word", "korean", "class", "candidates", "source", "cause", "defect", "count", "ja", "fix"]
    rows = read_verdicts(args.verdicts)
    bad = [r for r in rows if r["verdict"] != "ok" and r["span"].strip() and r["fix"].strip()]
    print(f"{len(rows)} judged lines, {len(bad)} with a span", file=sys.stderr)
    if not bad:
        with args.out.open("w", encoding="utf-8", newline="") as sink:
            csv.writer(sink, delimiter="\t", lineterminator="\n").writerow(header)
        return

    with tempfile.TemporaryDirectory(prefix="hdtrans-gaps-") as temp:
        tracePath = Path(temp) / "trace.tsv"
        data = translate(args.cli, args.dicts, [r["ja"] for r in bad], tracePath)
        traces = part_rows(tracePath)
    output = data.get("text", "").split("\n")
    per_line = collections.defaultdict(list)
    for choice in data.get("choices", []):
        per_line[choice["line"]].append(choice)

    wrong = []
    for index, row in enumerate(bad):
        translated = output[index] if index < len(output) else ""
        for choice in per_line.get(index, []):
            candidates = traces.get(choice_key(choice), [])
            part = chosen_class(candidates)
            if part not in CONTENT or not overlaps(choice, row["span"]):
                continue
            defect = defect_of(choice, candidates, part, translated, row["fix"])
            if not defect:
                continue
            source = choice.get("choice", {}).get("source", "") or (choice.get("ending") or {}).get("source", "")
            wrong.append({"word": choice["surface"], "korean": choice.get("choice", {}).get("text", ""),
                          "class": part, "candidates": len(candidates),
                          "source": source, "cause": row["cause"], "defect": defect,
                          "ja": row["ja"], "fix": row["fix"]})

    total = collections.Counter(w["word"] for w in wrong)
    common = collections.defaultdict(collections.Counter)
    for w in wrong:
        common[w["word"]][w["defect"]] += 1
    with args.out.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        written = 0
        for word in sorted(total, key=lambda w: (-total[w], w)):
            if total[word] < args.min_count:
                continue
            defect = max(common[word], key=lambda d: (common[word][d], d))
            entry = next(w for w in wrong if w["word"] == word and w["defect"] == defect)
            writer.writerow([word, entry["korean"], entry["class"], entry["candidates"],
                             entry["source"], entry["cause"], defect, total[word],
                             entry["ja"], entry["fix"]])
            written += 1
    summary = collections.Counter(w["defect"] for w in wrong)
    print(f"{len(wrong)} wrong decisions on {len(total)} words; {written} written; "
          + ", ".join(f"{k} {v}" for k, v in sorted(summary.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
