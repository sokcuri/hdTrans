#!/usr/bin/env python3
"""Port every dictionary file into one normalized table.

Usage: tools/dict-port.py DICTS_DIR OUT.tsv [--builtin DIR]
Every row of every column file and every builtin table is written with the same
columns, so the whole dictionary can be counted, joined with the rules program,
and filtered as one table instead of as 27 files in four formats.

OUT.tsv columns:
  layer     base, words, names, kana, or builtin
  file      the file the row came from
  line      the line in that file
  key       the Japanese key
  ko        the Korean
  pos       the part-of-speech code
  sem       the semantic codes
  gram      the grammar codes
  rule      the local rule number the row points at, empty for none
  flags     the space-separated flags as written
  reading   the reading column, when the file has one
  id        the row id
  shape     column for a row of a column file, plain for a builtin free table,
            sense for a sense row, kana for a kana row
  extra     the columns a free table has beyond key and Korean, joined by tabs
"""
import argparse
import csv
from pathlib import Path
import sys

COLUMN_LAYER = {"base": "base", "words": "words", "names": "names", "kana": "kana"}


def header(line):
    """The layer named by a #! line, or None."""
    if not line.startswith("#!"):
        return None
    fields = dict(part.split("=", 1) for part in line[2:].split() if "=" in part)
    return fields.get("layer")


def read_column(path, layer, out):
    with path.open(encoding="utf-8") as source:
        lines = source.read().splitlines()
    columns = None
    for number, line in enumerate(lines, 1):
        if line.startswith("#") or not line.strip():
            continue
        fields = line.split("\t")
        if fields[0] == "key":
            columns = {name: i for i, name in enumerate(fields)}
            continue
        if columns is None:
            continue
        def field(name):
            at = columns.get(name)
            return fields[at] if at is not None and at < len(fields) else ""
        out.append(dict(layer=layer, file=path.name, line=number, key=fields[0],
                        ko=field("ko"), pos=field("pos"), sem=field("sem"),
                        gram=field("gram"), rule=field("rule"), flags=field("flags"),
                        reading=field("reading"), id=field("id"), shape="column",
                        extra=""))


def read_kana(path, out):
    with path.open(encoding="utf-8") as source:
        for number, line in enumerate(source, 1):
            if line.startswith("#") or line.startswith("//") or not line.strip():
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 2:
                continue
            out.append(dict(layer="kana", file=path.name, line=number, key=fields[1],
                            ko="", pos="", sem="", gram="", rule="", flags="",
                            reading=fields[0], id="", shape="kana",
                            extra=fields[2] if len(fields) > 2 else ""))


def read_builtin(path, out):
    name = path.name
    with path.open(encoding="utf-8") as source:
        for number, line in enumerate(source, 1):
            if line.startswith("//") or line.startswith("#") or not line.strip():
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 2:
                continue
            shape = "sense" if name == "senses.tsv" else "plain"
            out.append(dict(layer="builtin", file=name, line=number, key=fields[0],
                            ko=fields[1], pos="", sem="", gram="", rule="", flags="",
                            reading="", id="", shape=shape,
                            extra="\t".join(fields[2:])))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dicts", type=Path, metavar="DICTS_DIR")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--builtin", type=Path, metavar="DIR")
    args = parser.parse_args()
    if not args.dicts.is_dir():
        parser.error(f"no dictionary folder at {args.dicts}")

    out = []
    files = sorted(args.dicts.glob("*.tsv"))
    for path in files:
        lines = path.read_text(encoding="utf-8").splitlines()
        layer = None
        for line in lines[:3]:
            if line.startswith("#!"):
                layer = header(line)
                break
        has_header = any(l.startswith("key\t") for l in lines if not l.startswith("#") and l.strip())
        if layer == "kana" and not has_header:
            read_kana(path, out)
        elif has_header:
            read_column(path, layer or "base", out)
        else:
            # A free table such as a user-format file: key, Korean, then extras.
            for number, line in enumerate(lines, 1):
                if line.startswith("#") or not line.strip():
                    continue
                fields = line.rstrip("\n").split("\t")
                if len(fields) < 2:
                    continue
                out.append(dict(layer=layer or "base", file=path.name, line=number,
                                key=fields[0], ko=fields[1], pos="", sem="", gram="",
                                rule="", flags="", reading="", id="", shape="plain",
                                extra="\t".join(fields[2:])))
    if args.builtin:
        for path in sorted(args.builtin.glob("*.tsv")):
            read_builtin(path, out)

    columns = ["layer", "file", "line", "key", "ko", "pos", "sem", "gram", "rule",
               "flags", "reading", "id", "shape", "extra"]
    with args.out.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        writer.writerow(columns)
        for row in out:
            writer.writerow([row[c] for c in columns])
    print(f"{len(files)} files, {len(out):,} rows -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
