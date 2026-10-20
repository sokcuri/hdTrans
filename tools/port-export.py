#!/usr/bin/env python3
"""Write the rule and dictionary files back from their ported tables.

Usage: tools/port-export.py RULES_PORT.tsv DICT_PORT.tsv OUT_DIR
Creates OUT_DIR/rules/ and OUT_DIR/dicts/ with the same file names and contents as
the sources the ports were made from, so a build on the exported trees can be
compared byte for byte with a build on the sources.

The rules are rebuilt from the prefix expression tree in path order. The
dictionary is rebuilt line by line from the row recorded for each file and line.
Builtin tables are written under OUT_DIR/builtin/.
"""
import argparse
import collections
import csv
from pathlib import Path
import sys

RULE_TOKENS = {"||", "&&", "&"}
COLUMNS = ["key", "ko", "pos", "sem", "gram", "dict", "rule", "flags", "prio",
           "reading", "id", "raw"]


def rebuild_body(nodes):
    def walk(at):
        if at >= len(nodes):
            raise ValueError("tree ends early")
        node = nodes[at]
        if node["op"] in RULE_TOKENS:
            count = int(node["count"])
            parts = [node["op"] + str(count)]
            at += 1
            for _ in range(count):
                part, at = walk(at)
                parts.append(part)
            return " ".join(parts), at
        operand = node["ref"] if node["ref"] else '"' + node["value"] + '"'
        return f"{node['field']}{node['position']}{node['operator']} {operand}", at + 1
    return walk(0)[0]


def export_rules(port, out_dir):
    rows = list(csv.DictReader(open(port, encoding="utf-8"), delimiter="\t"))
    groups = collections.OrderedDict()
    for row in rows:
        groups.setdefault(row["file"], collections.OrderedDict()).setdefault(
            (row["kind"], row["number"]), []).append(row)

    comments = {}
    for file_name, rules in groups.items():
        source = Path("src/core/rules") / file_name
        if source.is_file():
            lines = source.read_text(encoding="utf-8").splitlines()
            pending = []
            for number, line in enumerate(lines, 1):
                stripped = line.strip()
                if stripped.startswith("#"):
                    pending.append(line)
                elif stripped.startswith(("L ", "G ")):
                    if pending:
                        comments[(file_name, stripped.split()[1])] = pending
                    pending = []
                elif stripped:
                    pending = []

    out_dir.mkdir(parents=True, exist_ok=True)
    for file_name, rules in groups.items():
        # Keep the source order: comments and blank lines come from the source file,
        # rules in the order they appear there.
        source = Path("src/core/rules") / file_name
        written = {}
        for (kind, number), nodes in rules.items():
            head = [kind, number]
            if kind == "G":
                first = nodes[0]
                head += [first["pos"], first["range"], first["stem"]]
            written[(kind, number)] = " ".join(head) + " " + rebuild_body(nodes)
        lines = []
        if source.is_file():
            for line in source.read_text(encoding="utf-8").splitlines():
                stripped = line.strip()
                if stripped.startswith(("L ", "G ")):
                    key = (stripped.split()[0], stripped.split()[1])
                    lines.append(written.pop(key, line))
                else:
                    lines.append(line)
            for key, text in written.items():
                lines.append(text)
        else:
            lines = list(written.values())
        (out_dir / file_name).write_text("\n".join(lines) + "\n", encoding="utf-8")


def export_dict(port, out_dir, builtin_dir):
    rows = list(csv.DictReader(open(port, encoding="utf-8"), delimiter="\t"))
    by_file = collections.defaultdict(dict)
    for row in rows:
        by_file[row["file"]][int(row["line"])] = row
    out_dir.mkdir(parents=True, exist_ok=True)
    builtin_dir.mkdir(parents=True, exist_ok=True)
    for file_name, lines_of in by_file.items():
        source = Path("dicts") / file_name
        if not source.is_file():
            source = Path("src/core/dict/builtin") / file_name
            target = builtin_dir / file_name
        else:
            target = out_dir / file_name
        original = source.read_text(encoding="utf-8").splitlines()
        result = list(original)
        header = None
        for line in original:
            if line.startswith("key\t"):
                header = line.split("\t")
                break
        for number, row in lines_of.items():
            fields = [row["key"], row["ko"]]
            if row["shape"] == "column" and header:
                # The dict, prio and raw columns are not ported; they are reused from
                # the source line this row came from.
                src = original[number - 1].split("\t")
                values = {"key": row["key"], "ko": row["ko"], "pos": row["pos"],
                          "sem": row["sem"], "gram": row["gram"], "rule": row["rule"],
                          "flags": row["flags"], "reading": row["reading"], "id": row["id"]}
                for name in ("dict", "prio", "raw"):
                    at = header.index(name) if name in header else -1
                    values[name] = src[at] if 0 <= at < len(src) else ""
                # A row may carry fewer fields than the header names; the loader fills
                # the rest, and the file keeps the short form.
                fields = [values.get(name, "") for name in header][:max(len(src), 2)]
            elif row["shape"] in ("kana", "sense", "plain"):
                # These tables keep their trailing empty columns, so the field count
                # comes from the source line rather than from the last non-empty one.
                src = original[number - 1].split("\t")
                if row["shape"] == "kana":
                    head = [row["reading"], row["key"]]
                else:
                    head = [row["key"], row["ko"]]
                extra = row["extra"].split("\t") if row["extra"] else []
                fields = head + extra
                while len(fields) < len(src):
                    fields.append("")
            result[number - 1] = "\t".join(fields)
        target.write_text("\n".join(result) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rules_port", type=Path, metavar="RULES_PORT.tsv")
    parser.add_argument("dict_port", type=Path, metavar="DICT_PORT.tsv")
    parser.add_argument("out_dir", type=Path, metavar="OUT_DIR")
    args = parser.parse_args()
    out = args.out_dir
    export_rules(args.rules_port, out / "rules")
    export_dict(args.dict_port, out / "dicts", out / "builtin")
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
