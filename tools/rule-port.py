#!/usr/bin/env python3
"""Port the whole rules program into one structured table.

Usage: tools/rule-port.py RULES_DIR OUT.tsv [--dicts DIR]
RULES_DIR holds the rule files (*.txt). Every L and G line is parsed into its
prefix expression tree and written as rows of one table, so a rule can be read,
counted, filtered and rewritten as data instead of as text.

OUT.tsv columns:
  kind      L for a local rule, G for a global rule
  number    the L number, or the G priority
  pos       the global rule's part-of-speech key, empty for L
  range     the global rule's run range, empty for L
  stem      1 when the rule picks a word, 0 when it picks an ending
  depth     nesting depth of the body
  path      the index path inside the body, 1-based per level
  op        || || && & for a group, or the field letter (S,D,P,A,G,U,Y,M,T,W) for a condition
  count     the group's operand count
  field     the condition's field letter
  position  the condition's position, e.g. +1
  operator  == != = -= ==@ or none
  value     the condition's literal, without quotes
  ref       the @position operand when the value is one
  file      the rule file
  line      the line in that file
  comment   the comment line directly above the rule, when there is one
"""
import argparse
import csv
import os
from pathlib import Path
import re
import sys

GROUP = {"||", "&&", "&"}
FIELD = set("SDPAGUYMTW")
COND = re.compile(r'^([SDPAGUYMTW])([+-]\d)(==|!=|=|-=)?(?:\s*(?:"((?:[^"\\]|\\.)*)"|(@[+-]\d)))?$')


def tokenize(body):
    """Splits a rule body on single spaces, keeping a quoted value in one token."""
    tokens = []
    current = []
    quoted = False
    for ch in body:
        if ch == '"':
            quoted = not quoted
            current.append(ch)
        elif ch == " " and not quoted:
            if current:
                tokens.append("".join(current))
                current = []
        else:
            current.append(ch)
    if current:
        tokens.append("".join(current))
    return tokens


def parse_body(tokens, index, depth, path, out, rule, file_name, line, comment):
    """Writes one row per node of the prefix expression and returns the next index."""
    if index >= len(tokens):
        raise ValueError("body ends inside a group")
    token = tokens[index]
    if token.startswith(("||", "&&")):
        head, rest = token[:2], token[2:]
    elif token.startswith("&"):
        head, rest = "&", token[1:]
    else:
        head, rest = None, ""
    if head is not None:
        count = int(rest) if rest else 0
        out.append(dict(rule, depth=depth, path=path, op=head, count=count,
                        field="", position="", operator="", value="", ref=""))
        index += 1
        for n in range(1, count + 1):
            index = parse_body(tokens, index, depth + 1, f"{path}.{n}" if path else str(n),
                               out, rule, file_name, line, comment)
        return index
    match = COND.match(token)
    if not match:
        raise ValueError(f"cannot parse token {token!r}")
    field, position, operator, literal, ref = match.groups()
    if literal is None and ref is None and index + 1 < len(tokens):
        operand = tokens[index + 1]
        if operand.startswith("@"):
            ref = operand
        else:
            literal = operand.strip('"')
        index += 1
    out.append(dict(rule, depth=depth, path=path, op=field, count="",
                    field=field, position=position, operator=operator or "",
                    value=(literal or "").replace('\\"', '"').replace("\\\\", "\\"),
                    ref=ref or ""))
    return index + 1


def read_head_line(path):
    """The first comment above a rule, for the table's comment column."""
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    comments = {}
    pending = None
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("#") and not stripped.startswith("#test"):
            pending = stripped[1:].strip()
        elif stripped.startswith(("L ", "G ")):
            if pending:
                comments[number] = pending
            pending = None
        elif stripped:
            pending = None
    return comments


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rules", type=Path, metavar="RULES_DIR")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--dicts", type=Path, metavar="DIR", help="unused, kept for symmetry")
    args = parser.parse_args()
    if not args.rules.is_dir():
        parser.error(f"no rule folder at {args.rules}")

    rows = []
    errors = []
    files = sorted(args.rules.glob("*.txt"))
    for path in files:
        comments = read_head_line(path)
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.strip()
            if not stripped.startswith(("L ", "G ")):
                continue
            tokens = tokenize(stripped)
            if tokens[0] == "L":
                rule = dict(kind="L", number=tokens[1], pos="", range="", stem="", depth="",
                            path="", op="", count="", field="", position="", operator="",
                            value="", ref="", file=path.name, line=number,
                            comment=comments.get(number, ""))
                body = tokens[2:]
            else:
                rule = dict(kind="G", number=tokens[1], pos=tokens[2] if len(tokens) > 2 else "",
                            range=tokens[3] if len(tokens) > 3 else "",
                            stem=tokens[4] if len(tokens) > 4 else "", depth="", path="",
                            op="", count="", field="", position="", operator="", value="",
                            ref="", file=path.name, line=number, comment=comments.get(number, ""))
                body = tokens[5:]
            try:
                parse_body(body, 0, 0, "", rows, rule, path.name, number, comments.get(number, ""))
            except (ValueError, IndexError) as error:
                errors.append(f"{path.name}:{number}: {error}")

    with args.out.open("w", encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        writer.writerow(["kind", "number", "pos", "range", "stem", "depth", "path", "op",
                         "count", "field", "position", "operator", "value", "ref", "file",
                         "line", "comment"])
        for row in rows:
            writer.writerow([row[k] for k in ("kind", "number", "pos", "range", "stem", "depth",
                                              "path", "op", "count", "field", "position",
                                              "operator", "value", "ref", "file", "line",
                                              "comment")])
    rules = len({(r["kind"], r["number"]) for r in rows})
    print(f"{len(files)} files, {rules} rules, {len(rows)} nodes -> {args.out}", file=sys.stderr)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        sys.exit(1)


if __name__ == "__main__":
    main()
