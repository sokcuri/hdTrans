#!/usr/bin/env python3
"""Turn the #test lines of the rule files into rows of the case tables.

Usage: tools/test-to-cases.py RULES_DIR DICTS OUT_DIR [--cli PATH] [--check]
A #test line sits under the rule it checks, named by position, so moving the rule
body into the dictionary row leaves the line without a home. This writes one case
file per rule file instead, with the rule column holding the row id:

  #test "in" -> "out"      expected is that output, kind fix
  #test "in" -> unchanged  expected is the input, kind keep
  #test "in" picks "ko"    expected is what the CLI prints now, kind keep
  #test "in" silent        the same, kind keep

picks and silent are position-dependent checks; their current output is the
expectation once they pass, so the CLI runs once per input. The rule column needs
the dictionary, which is why DICTS is required.

With --check the files are only compared and a difference is reported.
"""
import argparse
import collections
from pathlib import Path
import re
import subprocess
import sys

TEST = re.compile(r'^#test "(.*)" -> ("(?:[^"\\]|\\.)*"|unchanged)$|^#test "(.*)" picks ("(?:[^"\\]|\\.)*")$|^#test "(.*)" silent$')
LOCAL = re.compile(r"^L (\d+) ")


def quote(text):
    out = []
    index = 0
    while index < len(text):
        if text[index] == "\\" and index + 1 < len(text):
            out.append({"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"'}.get(text[index + 1], text[index + 1]))
            index += 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def rule_rows(directory):
    """The row id of each local rule number."""
    rows = {}
    for path in sorted(Path(directory).glob("*.tsv")):
        if path.name == "kana.tsv":
            continue
        with path.open(encoding="utf-8") as source:
            header = None
            for line in source:
                if line.startswith("#") or not line.strip():
                    continue
                fields = line.rstrip("\n").split("\t")
                if fields[0] == "key":
                    header = {name: i for i, name in enumerate(fields)}
                    continue
                if header is None or "rule" not in header or "id" not in header:
                    continue
                rule = fields[header["rule"]] if header["rule"] < len(fields) else ""
                row_id = fields[header["id"]] if header["id"] < len(fields) else ""
                if rule.isdigit() and row_id:
                    rows.setdefault(rule, row_id)
    return rows


def read_tests(directory):
    """[(rule number, file name, test text)] in file order."""
    out = []
    for path in sorted(Path(directory).glob("*.txt")):
        rule = None
        for line in path.read_text(encoding="utf-8").splitlines():
            match = LOCAL.match(line)
            if match:
                rule = match.group(1)
                continue
            if line.startswith("#test ") and rule is not None:
                out.append((rule, path.name, line[6:].strip()))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rules", type=Path, metavar="RULES_DIR")
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("out", type=Path, metavar="OUT_DIR")
    parser.add_argument("--cli", default="build/mac/hdtrans")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    rows = rule_rows(args.dicts)
    tests = read_tests(args.rules)
    parsed = []
    for rule, file_name, text in tests:
        match = TEST.match("#test " + text)
        if not match:
            print(f"cannot read: {text}", file=sys.stderr)
            continue
        text_in = match.group(1) if match.group(1) is not None else match.group(3) if match.group(3) is not None else match.group(5)
        # A picks line names the target the rule selects, not the whole output.
        expected = match.group(2)
        match_picks = quote(match.group(4)[1:-1]) if match.group(4) is not None else None
        parsed.append((rule, file_name, text_in, expected, match_picks))

    # picks, silent and unchanged have no expected output of their own; the line the CLI
    # makes now stands in, so a later change still shows. One run for all of them, in
    # order.
    # One run with -why for every line: the output, and the rules that chose.
    sources = [quote(p[2]) for p in parsed]
    result = subprocess.run([args.cli, "-dicts", str(args.dicts), "--each", "-why"],
                            input="\n".join(sources) + "\n", capture_output=True,
                            text=True, encoding="utf-8", check=True)
    outputs = result.stdout.splitlines()
    if len(outputs) != len(sources):
        print(f"{len(sources)} inputs gave {len(outputs)} lines", file=sys.stderr)
        return 2
    current = dict(enumerate(outputs))
    chose = collections.defaultdict(set)
    line = 0
    for row in result.stderr.splitlines():
        if row.startswith("WHY-LINE\t"):
            line = int(row.split("\t")[1]) - 1
            continue
        if not row.startswith("WHY\t"):
            continue
        fields = row.split("\t")
        if len(fields) > 9 and fields[7] == "LocalRule":
            chose[line].add("L" + fields[8] + "=" + fields[9])
        if len(fields) > 17 and fields[11] == "LocalRule":
            chose[line].add("L" + fields[12] + "=" + fields[17])
    skipped = 0

    by_file = collections.defaultdict(list)
    wrote = 0
    for index, (rule, file_name, raw_in, expected, match_picks) in enumerate(parsed):
        text_in = quote(raw_in)
        if rule not in rows:
            print(f"L {rule} has no row", file=sys.stderr)
            continue
        if expected is not None and expected != "unchanged":
            want = quote(expected[1:-1])
            kind = "fix"
        elif (match_picks is not None and " " not in match_picks
              and ("L" + rule + "=" + match_picks) in chose[index]):
            # The rule must choose this target, which is what -why prints.
            want = "rule:dict:" + rows[rule] + "=" + match_picks
            kind = "keep"
        elif match_picks is not None:
            # The chosen target carries markers or is composed, so -why does not show
            # this text; pin the whole line instead.
            want = current[index]
            kind = "keep"
        else:
            # silent, or unchanged: the rule must choose nothing here (a rule that
            # changes nothing and one that does not run read the same).
            want = "!rule:dict:" + rows[rule]
            kind = "keep"
        by_file[file_name.replace(".txt", ".tsv")].append(
            ["dict:" + rows[rule], text_in, want, kind, "local"])
        wrote += 1

    if args.check:
        print(f"{wrote} test lines, {len(by_file)} files", file=sys.stderr)
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    for name, lines in by_file.items():
        with (args.out / name).open("w", encoding="utf-8", newline="") as sink:
            sink.write("rule\tinput\texpected\tkind\tlayer\n")
            for line in lines:
                sink.write("\t".join(line) + "\n")
    print(f"{wrote} test lines -> {len(by_file)} case files in {args.out}; {skipped} left", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
