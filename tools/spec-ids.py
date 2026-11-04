#!/usr/bin/env python3
"""Checks that every rule id of the spec has one section and that rule cases name existing ids.

  tools/spec-ids.py --check [ROOT]   ROOT defaults to the repository of this script
  tools/spec-ids.py --self-test

A rule id is the first word of a `## ` heading in spec/*.md that looks like AREA-NUMBER
(GRM-3910, LEX-4809). Two headings with the same id fail: parallel branches that took the
same number need one of them renumbered. A case file under tests/rules/ names the ids it tests
in a `# 규칙:` line (ids separated by commas or spaces); an id there with no section fails.
Each problem prints as `path:line: message`.
Exit: 0 clean, 1 problems, 2 invalid arguments.
"""
import os
import re
import sys
import tempfile

ID = re.compile(r"^[A-Z]+-\d+$")
HEADING = re.compile(r"^## ([A-Z]+-\d+)(?=\s|$)")
CASES = re.compile(r"^#\s*규칙:\s*(.*)$")


def spec_ids(root):
    """The rule ids of the spec headings: id -> [(path, line)]."""
    ids = {}
    spec = os.path.join(root, "spec")
    for name in sorted(os.listdir(spec)):
        if not name.endswith(".md"):
            continue
        path = os.path.join(spec, name)
        with open(path, encoding="utf-8") as f:
            for n, line in enumerate(f, 1):
                m = HEADING.match(line)
                if m:
                    ids.setdefault(m.group(1), []).append((os.path.relpath(path, root), n))
    return ids


def case_refs(root):
    """The ids the `# 규칙:` lines of the rule case files name: [(path, line, id)]."""
    refs = []
    base = os.path.join(root, "tests", "rules")
    for dirpath, _, files in sorted(os.walk(base)):
        for name in sorted(files):
            if not name.endswith(".tsv"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8") as f:
                for n, line in enumerate(f, 1):
                    if not line.startswith("#"):
                        break
                    m = CASES.match(line.rstrip("\n"))
                    if not m:
                        continue
                    for word in re.split(r"[,\s]+", m.group(1)):
                        if ID.match(word):
                            refs.append((os.path.relpath(path, root), n, word))
    return refs


def check(root):
    problems = []
    ids = spec_ids(root)
    for rid, places in sorted(ids.items()):
        if len(places) > 1:
            first = f"{places[0][0]}:{places[0][1]}"
            for path, n in places[1:]:
                problems.append(f"{path}:{n}: {rid} is already the heading at {first}")
    for path, n, rid in case_refs(root):
        if rid not in ids:
            problems.append(f"{path}:{n}: {rid} has no section in spec/")
    return problems


def self_test():
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, "spec"))
        os.makedirs(os.path.join(root, "tests", "rules", "cases"))

        def write(rel, text):
            with open(os.path.join(root, rel), "w", encoding="utf-8") as f:
                f.write(text)

        write("spec/a.md", "# A\n\n## GRM-1 one\n\ntext\n\n## LEX-2 two\n")
        write("spec/b.md", "## LEX-3 three\n### GRM-1 not a section\n")
        write("tests/rules/cases/x.tsv", "# 규칙: GRM-1, LEX-3\nrule\tinput\texpected\tkind\tlayer\n")
        assert check(root) == [], check(root)
        write("spec/b.md", "## LEX-3 three\n\n## GRM-1 again\n")
        write("tests/rules/cases/y.tsv", "# 규칙: LEX-9 LEX-2\nrule\tinput\n# 규칙: LEX-8\n")
        got = check(root)
        assert got == ["spec/b.md:3: GRM-1 is already the heading at spec/a.md:3",
                       "tests/rules/cases/y.tsv:1: LEX-9 has no section in spec/"], got
    print("spec-ids self-test: ok")
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) in (2, 3) and argv[1] == "--check":
        root = argv[2] if len(argv) == 3 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        problems = check(root)
        for p in problems:
            print(p)
        print(f"spec ids: {len(spec_ids(root))} sections, {len(problems)} problems")
        return 1 if problems else 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
