#!/usr/bin/env python3
"""Write the part-of-speech schema of the dictionaries.

Usage: tools/pos-schema.py [--root REPO]
Reads the pos codes the dictionary rows use, the names edited in tools/code-meanings.tsv,
and the standard-tag table src/core/dict/pos_tags_gen.cpp, and writes

  src/core/dict/pos_schema.tsv        the schema, one row per pos code
  src/core/dict/pos_schema_gen.cpp    the same rows as a table the engine reads

Columns: code, class, name, standard, status.
  class     the first digit of the code (docs/dictionary-codes.md)
  name      the Korean name of the code
  standard  the standard analyzer tags that map to the code, comma separated
  status    named, or unnamed when no name is known yet, with the class name as the
            name until an editor gives one
Edit pos_schema.tsv to name a code; running the tool again keeps the names written
there and only adds codes the dictionaries gained.
"""
import argparse
import collections
import csv
from pathlib import Path
import re
import sys

CLASS_NAMES = {
    "0": "연체사", "1": "명사", "2": "조사", "3": "부사", "4": "동사 어간", "5": "어미",
    "6": "기호", "7": "い형용사", "8": "형용동사", "9": "구",
}
HEADER = ["code", "class", "name", "standard", "status"]


def dictionary_codes(root):
    codes = set()
    for path in sorted((root / "dicts").glob("*.tsv")):
        with path.open(encoding="utf-8", newline="") as source:
            reader = csv.DictReader((line for line in source if not line.startswith(("#", "//"))),
                                    delimiter="\t", quoting=csv.QUOTE_NONE)
            if not reader.fieldnames or "pos" not in reader.fieldnames:
                continue
            for row in reader:
                if row["pos"]:
                    codes.add(row["pos"])
    return codes


def standard_tags(root):
    tags = collections.defaultdict(list)
    for line in (root / "src/core/dict/pos_tags_gen.cpp").read_text(encoding="utf-8").splitlines():
        match = re.match(r'\s*\{"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\}', line)
        if match and match.group(2):
            tags[match.group(2)].append(match.group(1))
    return tags


def edited_names(root):
    names = {}
    for row in csv.DictReader((root / "tools/code-meanings.tsv").open(encoding="utf-8"), delimiter="\t"):
        if row["kind"] == "pos":
            names[row["code"]] = row["meaning"]
    return names


def read_schema(path):
    if not path.is_file():
        return {}
    with path.open(encoding="utf-8", newline="") as source:
        return {row["code"]: row for row in csv.DictReader(source, delimiter="\t")}


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()

    codes = dictionary_codes(root)
    standard = standard_tags(root)
    edited = edited_names(root)
    schema_path = root / "src/core/dict/pos_schema.tsv"
    previous = read_schema(schema_path)

    rows = []
    named = 0
    for code in sorted(codes):
        old = previous.get(code)
        name = (old or {}).get("name") or edited.get(code) or ""
        status = "named"
        if not name:
            name = CLASS_NAMES.get(code[1:2], "기타")
            status = "unnamed"
        else:
            named += 1
        if old and old.get("status") == "named" and status != "named":
            status = "named"
        rows.append({"code": code, "class": code[1:2], "name": name,
                     "standard": ",".join(standard.get(code, [])), "status": status})

    with schema_path.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.DictWriter(sink, fieldnames=HEADER, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    out = root / "src/core/dict/pos_schema_gen.cpp"
    body = ["// Generated table; do not edit.", '#include "core/dict/codes.h"', "",
            "namespace hdTrans::dict::internal {", "", "namespace {", "",
            "constexpr PartName kParts[] = {"]
    for row in rows:
        body.append("    {%s, %s, %s, %s}," % (cpp_string(row["code"]), cpp_string(row["class"]),
                                               cpp_string(row["name"]), cpp_string(row["standard"])))
    body += ["};", "", "}  // namespace", "",
             "std::span<const PartName> PartSchema() { return kParts; }", "",
             "}  // namespace hdTrans::dict::internal", ""]
    out.write_text("\n".join(body), encoding="utf-8")

    patterns = read_patterns(root / "src/core/dict/part_patterns.tsv")
    generated = root / "src/core/dict/part_patterns_gen.cpp"
    table = ["// Generated table; do not edit.", '#include "core/dict/codes.h"', "",
             "namespace hdTrans::dict::internal {", "", "namespace {", "",
             "constexpr PartPattern kPatterns[] = {"]
    for field, pattern, name in patterns:
        table.append("    {%s, %s, %s}," % (cpp_string(field), cpp_string(name), cpp_string(pattern)))
    table += ["};", "", "}  // namespace", "",
              "std::span<const PartPattern> PartPatternTable() { return kPatterns; }", "",
              "}  // namespace hdTrans::dict::internal", ""]
    generated.write_text("\n".join(table), encoding="utf-8")
    print(f"{len(rows)} codes, {named} named, {len(rows) - named} unnamed; "
          f"{len(patterns)} pattern names", file=sys.stderr)
    return 0


def read_patterns(path):
    """The field, name and pattern of each row, sorted by field then name."""
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        field, pattern, name = line.split("\t")
        rows.append((field, pattern, name))
    return sorted(rows, key=lambda r: (r[0], r[2]))


if __name__ == "__main__":
    sys.exit(main())
