#!/usr/bin/env python3
"""Leaves out each sentence pass of the word stage in turn and runs the rule cases and examples/, to
list the passes that no test catches.

  tools/pass-mutation.py [-c CLI] [-d DICTS] [-j JOBS] [-t SECONDS] [PASS...]
  tools/pass-mutation.py --self-test

CLI is an experiment build (CMake -DHDTRANS_PASS_EXPERIMENTS=ON, docs/testing.md; default
build/<preset>-passexp/hdtrans with HDTRANS_PRESET as in the other tools). DICTS defaults to dicts/.
The passes are the names in src/core/word/flush.cpp; PASS... limits the run to some of them. Each
pass is left out with HDTRANS_PASS_SKIP while tests/rules/cases.tsv, tests/rules/cases/,
tests/rules/speech-soft/ (--speech soft), tests/rules/speech-strict/ (--speech strict) and
examples/ run, JOBS at a time (default: the number of CPUs), each with a fresh HOME, a time limit
of SECONDS (default 900) and one dictionary cache that a run without a left-out pass fills first.
A suite whose CLI fails (crash) or that runs out of time counts as catching the pass.
Prints a TSV line per pass (pass, failed rows of each suite or crash or timeout, and caught,
uncaught or not exercised: a pass that only an option off by default runs, src/core/word/analyze.h),
then the uncaught passes.
Exit: 0 done, 2 invalid arguments, a suite failing with no pass left out, or a CLI that ignores
HDTRANS_PASS_SKIP.
"""
import concurrent.futures
import os
import platform
import re
import shutil
import signal
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
OFF_BY_DEFAULT = re.compile(r"X\((\w+),\s*false\)")


def pass_names(source):
    names = [m.group(1) for m in PASS.finditer(source) if m.group(1) == m.group(2)]
    return list(dict.fromkeys(names))


def not_exercised(flush, analyze):
    """Passes whose Run call is guarded only by improvements that are off by default."""
    off = set(OFF_BY_DEFAULT.findall(analyze))
    found, lines = [], flush.split("\n")
    for i, line in enumerate(lines):
        if "Run(" not in line:
            continue
        for m in PASS.finditer(line):
            guard = line[:m.start()]
            if not guard.strip().startswith("if") and i > 0 and lines[i - 1].strip().startswith("if"):
                guard = lines[i - 1]
            options = re.findall(r"improvements\.(\w+)", guard)
            if options and all(o in off for o in options):
                found.append(m.group(1))
    return found


def failures(suite, returncode, stdout, stderr):
    """Failed rows (or example documents) of one suite run, "crash" when its CLI failed, or None when
    the suite itself could not run."""
    if suite == "examples":
        if returncode == 1:
            return len(EXAMPLE_DIFF.findall(stdout)) or "crash"
        return 0 if returncode == 0 else None
    m = CASES_SUMMARY.search(stdout)
    if m and returncode in (0, 1):
        return int(m.group(1))
    return "crash" if returncode == 2 and "CLI failed" in stderr else None


def run_suite(cli, dicts, suite, args, skip, cache, limit):
    with tempfile.TemporaryDirectory(prefix="hdtrans-mutation.") as home:
        env = dict(os.environ, HOME=home, HDTRANS_CACHE_DIR=cache, HDTRANS_PASS_SKIP=skip, LC_ALL="C")
        if args is None:
            env["HDTRANS_EXAMPLES_OUT"] = os.path.join(home, "examples")
            cmd = ["sh", str(ROOT / "tools/examples.sh"), cli, dicts]
        else:
            cmd = ["sh", str(ROOT / "tools/rule-cases.sh"), cli, dicts, str(ROOT / args[0]), *args[1:]]
        p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                             start_new_session=True)
        try:
            stdout, stderr = p.communicate(timeout=limit)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            return "timeout"
    n = failures(suite, p.returncode, stdout, stderr)
    if n is None:
        detail = (stderr or stdout).strip()[-300:]
        raise RuntimeError(f"{suite} with HDTRANS_PASS_SKIP={skip!r} failed to run: {detail}")
    return n


def self_test():
    src = ('constexpr Pass kTextPasses[] = {\n    {"Select", Select},\n    {"NounTo", NounTo},\n};\n'
           'Run(f, {"DrainNmTail", DrainNmTail});\nRun(f, {"NounTo", NounTo});\nauto x = {"a", b};\n')
    assert pass_names(src) == ["Select", "NounTo", "DrainNmTail"], pass_names(src)
    assert failures("cases", 1, "== layer local: 2 failed\nrule cases: 40 rows, 2 failed (local 2)\n", "") == 2
    assert failures("cases", 2, "", "CLI failed\n") == "crash"
    assert failures("cases", 2, "", "no cases at x\n") is None
    assert failures("examples", 1, "3c3\n< a\n---\n> b\nnovel: output differs from examples/novel.ko.txt\n", "") == 1
    assert failures("examples", 1, "", "novel: CLI failed\n") == "crash"
    assert failures("examples", 0, "", "") == 0
    flush = ('    if (f.w.settings.text && f.w.settings.improvements.misreadFilter) '
             'Run(f, {"FilterMisreads", FilterMisreads});\n'
             '    if (f.w.settings.improvements.cgRules || f.w.settings.improvements.cgLearned) {\n'
             '        Run(f, {"FilterCg", FilterCg});\n    }\n    Run(f, {"VerbNegative", VerbNegative});\n')
    analyze = "X(misreadFilter, false) \\\n X(cgRules, true) \\\n X(cgLearned, false)\n"
    assert not_exercised(flush, analyze) == ["FilterMisreads"], not_exercised(flush, analyze)
    print("self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    preset = os.environ.get("HDTRANS_PRESET") or ("mac" if platform.system() == "Darwin" else "linux")
    cli, dicts = str(ROOT / "build" / f"{preset}-passexp" / "hdtrans"), str(ROOT / "dicts")
    jobs, limit, only = os.cpu_count() or 4, 900, []
    it = iter(argv)
    for a in it:
        if a in ("-c", "-d", "-j", "-t"):
            value = next(it, None)
            if value is None:
                print(__doc__.strip(), file=sys.stderr)
                return 2
            if a == "-c":
                cli = value
            elif a == "-d":
                dicts = value
            elif a == "-j":
                jobs = int(value)
            else:
                limit = int(value)
        elif a.startswith("-"):
            print(__doc__.strip(), file=sys.stderr)
            return 2
        else:
            only.append(a)
    if not os.access(cli, os.X_OK):
        print(f"no CLI at {cli}; build one with -DHDTRANS_PASS_EXPERIMENTS=ON (docs/testing.md)", file=sys.stderr)
        return 2
    flush = (ROOT / "src/core/word/flush.cpp").read_text(encoding="utf-8")
    names = pass_names(flush)
    idle = set(not_exercised(flush, (ROOT / "src/core/word/analyze.h").read_text(encoding="utf-8")))
    unknown = [p for p in only if p not in names]
    if unknown:
        print(f"unknown passes: {' '.join(unknown)}", file=sys.stderr)
        return 2
    names = only or names
    cache = tempfile.mkdtemp(prefix="hdtrans-mutation-cache.")
    try:
        try:
            base = {s: run_suite(cli, dicts, s, a, "", cache, limit) for s, a in SUITES}
        except RuntimeError as e:
            print(e, file=sys.stderr)
            return 2
        if any(base.values()):
            print(f"suites fail with no pass left out: {base}", file=sys.stderr)
            return 2
        runs = [(p, s, a) for p in names + ([] if "Select" in names else ["Select"]) for s, a in SUITES]
        results = {}
        pool = concurrent.futures.ThreadPoolExecutor(max_workers=jobs)
        try:
            futures = {pool.submit(run_suite, cli, dicts, s, a, p, cache, limit): (p, s) for p, s, a in runs}
            for fut in concurrent.futures.as_completed(futures):
                results[futures[fut]] = fut.result()
        except RuntimeError as e:
            pool.shutdown(wait=True, cancel_futures=True)
            print(e, file=sys.stderr)
            return 2
        pool.shutdown()
    finally:
        shutil.rmtree(cache, ignore_errors=True)
    if not any(results[("Select", s)] for s, _ in SUITES):
        print(f"leaving out Select changes nothing: {cli} ignores HDTRANS_PASS_SKIP", file=sys.stderr)
        return 2
    print("pass\t" + "\t".join(s for s, _ in SUITES) + "\tverdict")
    uncaught, unexercised = [], []
    for p in names:
        counts = [results[(p, s)] for s, _ in SUITES]
        verdict = "caught" if any(counts) else "not exercised" if p in idle else "uncaught"
        (uncaught if verdict == "uncaught" else unexercised if verdict == "not exercised" else []).append(p)
        print(p + "\t" + "\t".join(map(str, counts)) + "\t" + verdict)
    print(f"uncaught: {len(uncaught)} of {len(names)}" + (": " + " ".join(uncaught) if uncaught else ""))
    if unexercised:
        print("not exercised: " + " ".join(unexercised))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
