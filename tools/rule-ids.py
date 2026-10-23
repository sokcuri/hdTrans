#!/usr/bin/env python3
"""Lists the new rule numbers of each revision and the numbers two revisions share.

  tools/rule-ids.py BASE REV...
  tools/rule-ids.py --self-test

A local rule is new when a row with a `rule` number and a `ctx` body appears in dicts/ or the
builtin tables and BASE has no such number; a global rule when a `G <prio>` line appears in
src/core/rules/globals.txt with a priority BASE does not use.
Tables without a header line (the user-dictionary format) have no rule columns and add no rule.
Parallel branches that add the same number need one of them renumbered before integration.
Exit 1 when two revisions share a number.
"""
import collections
import re
import subprocess
import sys

PATHS = ("dicts", "src/core/dict/builtin", "src/core/rules/globals.txt")


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def header(rev, path):
    for line in git("show", f"{rev}:{path}").split("\n"):
        if line and not line.startswith(("#", "//")):
            return line.split("\t") if line.startswith("key\t") else None
    return None


def added(diff, header_of):
    local, glob, path, cols = set(), set(), None, None
    for line in diff.split("\n"):
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
            cols = header_of(path) if path and path.endswith(".tsv") else None
            continue
        if not path or not line.startswith("+"):
            continue
        text = line[1:]
        if path.endswith("globals.txt"):
            m = re.match(r"G (\d+) ", text)
            if m:
                glob.add(int(m.group(1)))
        elif cols and "rule" in cols and "ctx" in cols:
            v = text.split("\t")
            r, c = cols.index("rule"), cols.index("ctx")
            if len(v) > max(r, c) and v[r].strip().isdigit() and v[c].strip():
                local.add(int(v[r]))
    return local, glob


def numbers(rev):
    files = git("ls-tree", "-r", "--name-only", rev, "--", *PATHS).split()
    diff = "".join(f"+++ b/{f}\n" + "".join("+" + x + "\n" for x in git("show", f"{rev}:{f}").split("\n"))
                   for f in files if f.endswith((".tsv", "globals.txt")))
    return added(diff, lambda p: header(rev, p))


def self_test():
    diff = ("+++ b/dicts/words.tsv\n+猫\t고양이\tN\t\t\t\t9001\tS+1== \"だ\"\n+犬\t개\tN\n"
            "+++ b/src/core/rules/globals.txt\n+G 812 S+1== \"ね\"\n")
    cols = ["key", "ko", "pos", "sem", "gram", "dict", "rule", "ctx"]
    assert added(diff, lambda p: cols) == ({9001}, {812}), added(diff, lambda p: cols)
    print("self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    base, revs = argv[0], argv[1:]
    known_local, known_glob = numbers(base)
    owners = collections.defaultdict(list)
    for rev in revs:
        diff = git("diff", "-U0", f"{base}...{rev}", "--", *PATHS)
        local, glob = added(diff, lambda p, rev=rev: header(rev, p))
        local, glob = local - known_local, glob - known_glob
        print(f"{rev}\tL {' '.join(map(str, sorted(local))) or '-'}\tG {' '.join(map(str, sorted(glob))) or '-'}")
        for n in local:
            owners[f"L{n}"].append(rev)
        for n in glob:
            owners[f"G{n}"].append(rev)
    shared = {k: v for k, v in owners.items() if len(v) > 1}
    for k, v in sorted(shared.items()):
        print(f"SHARED {k}: {' '.join(v)}")
    return 1 if shared else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
