#!/usr/bin/env python3
"""Leaves out each sentence pass of the word stage in turn and runs the rule cases and examples/, to
list the passes that no test catches.

  tools/pass-mutation.py [-c CLI] [-d DICTS] [-j JOBS] [PASS...]
  tools/pass-mutation.py --self-test

CLI is an experiment build (CMake -DHDTRANS_PASS_EXPERIMENTS=ON, docs/testing.md; default
build/<preset>-passexp/hdtrans with HDTRANS_PRESET as in the other tools). DICTS defaults to dicts/.
The passes are the names in src/core/word/flush.cpp; PASS... limits the run to some of them. Each
pass is left out with HDTRANS_PASS_SKIP while tests/rules/cases.tsv, tests/rules/cases/,
tests/rules/speech-soft/ (--speech soft), tests/rules/speech-strict/ (--speech strict) and
examples/ run, JOBS at a time (default: the number of CPUs), each with a fresh HOME and one
dictionary cache that a run without a left-out pass fills first.
Prints a TSV line per pass (pass, failed rows of each suite, caught or uncaught), then the
uncaught passes.
Exit: 0 done, 2 invalid arguments, a suite failing with no pass left out, or a CLI that ignores
HDTRANS_PASS_SKIP.
"""
import concurrent.futures
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITES = [
    ("cases", ["tests/rules/cases.tsv"]),
    ("themes", ["tests/rules/cases"]),
    ("soft", ["tests/rules/speech-soft", "--", "--speech", "soft"]),
    ("strict", ["tests/rules/speech-strict", "--", "--speech", "strict"]),
    ("examples", None),
]
PASS = re.compile(r'\{"(\w+)", (\w+)\}')
CASES_SUMMARY = re.compile(r"^rule cases: \d+ rows, (\d+) failed", re.M)
EXAMPLE_DIFF = re.compile(r"^\S+: output differs from ", re.M)


def pass_names(source):
    names = [m.group(1) for m in PASS.finditer(source) if m.group(1) == m.group(2)]
    return list(dict.fromkeys(names))


def failures(suite, returncode, output):
    """Failed rows (or example documents) of one suite run, or None when the run itself failed."""
    if suite == "examples":
        return len(EXAMPLE_DIFF.findall(output)) if returncode in (0, 1) else None
    m = CASES_SUMMARY.search(output)
    return int(m.group(1)) if m and returncode in (0, 1) else None


def run_suite(cli, dicts, suite, args, skip, cache):
    with tempfile.TemporaryDirectory(prefix="hdtrans-mutation.") as home:
        env = dict(os.environ, HOME=home, HDTRANS_CACHE_DIR=cache, HDTRANS_PASS_SKIP=skip, LC_ALL="C")
        if args is None:
            env["HDTRANS_EXAMPLES_OUT"] = os.path.join(home, "examples")
            cmd = ["sh", str(ROOT / "tools/examples.sh"), cli, dicts]
        else:
            cmd = ["sh", str(ROOT / "tools/rule-cases.sh"), cli, dicts, str(ROOT / args[0]), *args[1:]]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    n = failures(suite, r.returncode, r.stdout)
    if n is None:
        detail = (r.stderr or r.stdout).strip()[-300:]
        raise RuntimeError(f"{suite} with HDTRANS_PASS_SKIP={skip!r} failed to run: {detail}")
    return n


def self_test():
    src = ('constexpr Pass kTextPasses[] = {\n    {"Select", Select},\n    {"NounTo", NounTo},\n};\n'
           'Run(f, {"DrainNmTail", DrainNmTail});\nRun(f, {"NounTo", NounTo});\nauto x = {"a", b};\n')
    assert pass_names(src) == ["Select", "NounTo", "DrainNmTail"], pass_names(src)
    assert failures("cases", 1, "== layer local: 2 failed\nrule cases: 40 rows, 2 failed (local 2)\n") == 2
    assert failures("cases", 2, "CLI failed") is None
    assert failures("examples", 1, "3c3\n< a\n---\n> b\nnovel: output differs from examples/novel.ko.txt\n") == 1
    assert failures("examples", 0, "") == 0
    print("self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    preset = os.environ.get("HDTRANS_PRESET") or ("mac" if platform.system() == "Darwin" else "linux")
    cli, dicts = str(ROOT / "build" / f"{preset}-passexp" / "hdtrans"), str(ROOT / "dicts")
    jobs, only = os.cpu_count() or 4, []
    it = iter(argv)
    for a in it:
        if a in ("-c", "-d", "-j"):
            value = next(it, None)
            if value is None:
                print(__doc__.strip(), file=sys.stderr)
                return 2
            if a == "-c":
                cli = value
            elif a == "-d":
                dicts = value
            else:
                jobs = int(value)
        elif a.startswith("-"):
            print(__doc__.strip(), file=sys.stderr)
            return 2
        else:
            only.append(a)
    if not os.access(cli, os.X_OK):
        print(f"no CLI at {cli}; build one with -DHDTRANS_PASS_EXPERIMENTS=ON (docs/testing.md)", file=sys.stderr)
        return 2
    names = pass_names((ROOT / "src/core/word/flush.cpp").read_text(encoding="utf-8"))
    unknown = [p for p in only if p not in names]
    if unknown:
        print(f"unknown passes: {' '.join(unknown)}", file=sys.stderr)
        return 2
    names = only or names
    cache = tempfile.mkdtemp(prefix="hdtrans-mutation-cache.")
    try:
        try:
            base = {s: run_suite(cli, dicts, s, a, "", cache) for s, a in SUITES}
        except RuntimeError as e:
            print(e, file=sys.stderr)
            return 2
        if any(base.values()):
            print(f"suites fail with no pass left out: {base}", file=sys.stderr)
            return 2
        runs = [(p, s, a) for p in names + ([] if "Select" in names else ["Select"]) for s, a in SUITES]
        results = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(run_suite, cli, dicts, s, a, p, cache): (p, s) for p, s, a in runs}
            for fut in concurrent.futures.as_completed(futures):
                try:
                    results[futures[fut]] = fut.result()
                except RuntimeError as e:
                    print(e, file=sys.stderr)
                    return 2
    finally:
        shutil.rmtree(cache, ignore_errors=True)
    if not any(results[("Select", s)] for s, _ in SUITES):
        print(f"leaving out Select changes nothing: {cli} ignores HDTRANS_PASS_SKIP", file=sys.stderr)
        return 2
    print("pass\t" + "\t".join(s for s, _ in SUITES) + "\tverdict")
    uncaught = []
    for p in names:
        counts = [results[(p, s)] for s, _ in SUITES]
        caught = any(counts)
        if not caught:
            uncaught.append(p)
        print(p + "\t" + "\t".join(map(str, counts)) + "\t" + ("caught" if caught else "uncaught"))
    print(f"uncaught: {len(uncaught)} of {len(names)}" + (": " + " ".join(uncaught) if uncaught else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
