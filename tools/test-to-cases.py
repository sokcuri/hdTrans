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
        # A picks line names the target the rule selects, not the whole output; the
        # case table holds whole outputs, so the current output is what to keep.
        expected = match.group(2)
        parsed.append((rule, file_name, text_in, expected, None))

    # picks, silent and unchanged have no expected output of their own; the line the CLI
    # makes now stands in, so a later change still shows. One run for all of them, in
    # order.
    need = [i for i, p in enumerate(parsed) if p[3] is None or p[3] == "unchanged"]
    current = {}
    if need:
        sources = [quote(parsed[i][2]) for i in need]
        result = subprocess.run([args.cli, "-dicts", str(args.dicts), "--each"],
                                input="\n".join(sources) + "\n", capture_output=True,
                                text=True, encoding="utf-8", check=True)
        outputs = result.stdout.splitlines()
        if len(outputs) != len(sources):
            print(f"{len(sources)} inputs gave {len(outputs)} lines", file=sys.stderr)
            return 2
        for i, text in zip(need, outputs):
            current[i] = text
    skipped = 0

    by_file = collections.defaultdict(list)
    wrote = 0
    for index, (rule, file_name, raw_in, expected, _) in enumerate(parsed):
        text_in = quote(raw_in)
        if rule not in rows:
            print(f"L {rule} has no row", file=sys.stderr)
            continue
        if expected is None or expected == "unchanged":
            # picks, silent and unchanged say that the rule runs (or that it changes
            # nothing); the case table can only pin what the line reads, so the output
            # the CLI makes now becomes the expectation. A later change still shows.
            want = current.get(index, "")
            kind = "keep"
            if not want:
                print(f"no output for: {text_in}", file=sys.stderr)
                skipped += 1
                continue
        else:
            want = quote(expected[1:-1])
            kind = "fix"
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
