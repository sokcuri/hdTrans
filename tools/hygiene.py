#!/usr/bin/env python3
"""Checks the lines a change adds and its commit messages for things that must not be committed.

  tools/hygiene.py BASE        added lines of `git diff BASE` (working tree included) and the
                               messages of BASE..HEAD
  tools/hygiene.py --self-test

Built-in checks: conflict markers, added paths starting with - or holding whitespace, absolute
home paths, the middle dot, and wording that records an instruction instead of a technical
statement. Further patterns (for example names of private sources and data sets) come from the
file named by HDTRANS_HYGIENE_PATTERNS: one case-insensitive Python regular expression per line,
`#` lines ignored; keep that file outside the repository. Each hit prints as
`file:line: check: text` or `commit SHA: check: text`; read every hit, since a feature name can
match too.
Exit: 0 clean, 1 hits, 2 invalid arguments.
"""
import os
import re
import subprocess
import sys

BUILTIN = {
    "marker": r"^(<<<<<<<|>>>>>>>|=======$)",
    "abs-path": r"/(Users|home)/[^/\s]+/",
    "middle-dot": r"·",
    "instruction": r"\b(the )?users? (wants?|asked|requested|said|told)\b|\bas (requested|asked|instructed)\b|"
                   r"사용자(가|의) (요청|지시)|요청(에 따라|대로|하신)|지시(에 따라|대로|하신|한 대로)|"
                   r"\b(per|by) (the )?(instructions?|prompts?)\b",
}


def patterns():
    found = {k: re.compile(v, re.I) for k, v in BUILTIN.items()}
    path = os.environ.get("HDTRANS_HYGIENE_PATTERNS")
    if path:
        with open(path, encoding="utf-8") as f:
            for n, line in enumerate(f, 1):
                line = line.strip()
                if line and not line.startswith("#"):
                    found[f"pattern {n}"] = re.compile(line, re.I)
    return found


def scan_diff(diff, checks):
    hits, path, line_no = [], None, 0
    for line in diff.split("\n"):
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
            continue
        if line.startswith("@@"):
            line_no = int(re.search(r"\+(\d+)", line).group(1))
            continue
        if path and line.startswith("+"):
            for name, rx in checks.items():
                if rx.search(line[1:]):
                    hits.append(f"{path}:{line_no}: {name}: {line[1:].strip()[:100]}")
            line_no += 1
    return hits


def scan_paths(paths):
    return [f"{p}: stray-path: added path" for p in paths if re.search(r"(^|/)-|\s", p)]


def git(*args):
    r = subprocess.run(["git", *args], capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout


def self_test():
    checks = {k: re.compile(v, re.I) for k, v in BUILTIN.items()}
    diff = ("+++ b/a.md\n@@ -1,0 +3,3 @@\n+ok line\n+as requested, the rows\n+see /Users/someone/x\n"
            "+++ b/dicts/user.tsv\n@@ -0,0 +1 @@\n+user dictionary rows\n")
    hits = scan_diff(diff, checks)
    assert hits == ["a.md:4: instruction: as requested, the rows", "a.md:5: abs-path: see /Users/someone/x"], hits
    assert scan_paths(["tools/a.py", "--in", "a b.txt"]) == ["--in: stray-path: added path",
                                                              "a b.txt: stray-path: added path"]
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) != 1 or argv[0].startswith("-"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    base, checks = argv[0], patterns()
    hits = scan_diff(git("diff", "-U0", base), checks)
    added = git("diff", "--name-only", "--diff-filter=A", base).split("\n")
    added += git("ls-files", "--others", "--exclude-standard").split("\n")
    hits += scan_paths([p for p in added if p])
    for sha in git("rev-list", f"{base}..HEAD").split():
        for line in git("log", "-1", "--format=%B", sha).split("\n"):
            for name, rx in checks.items():
                if name != "middle-dot" and rx.search(line):
                    hits.append(f"commit {sha[:10]}: {name}: {line.strip()[:100]}")
    for h in hits:
        print(h)
    print(f"hygiene: {len(hits)} hits")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
