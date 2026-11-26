#!/usr/bin/env python3
"""Counts the instructions the CLI runs for fixed inputs under valgrind and reports them next to a
build of the base commit measured in the same run. It only reports: a rise never fails a run.

  tools/perf-budget.py --report CLI DICTS [--against CLI DICTS] [--base REF]
  tools/perf-budget.py --measure CLI DICTS [--base REF]   print the counts
  tools/perf-budget.py --self-test

DICTS is what --dicts takes (the CI passes the compiled snapshot of each build). The inputs are read
from the commit REF, the base commit (default: the merge base with develop), so that both builds
translate the same text:
  each      the inputs of the rule cases (tests/rules/cases, tests/rules/speech-strict), --each
  document  the examples (examples/*.ja.txt without their first line) as one document
  first-line  one short sentence, the work of a first translation (lazy tables) above load
  long-line   a punctuation-free clause repeated LONG_REPEAT times as one line
Every run loads the dictionaries; `load` is a run on empty input, and the other rows count the
instructions above it. `kana-run` is no instruction count but the peak resident memory in KiB of a run
on KANA_RUN_CHARS pseudo-random hiragana as one line, the median of RSS_RUNS runs outside valgrind; its
row is over or under at RSS_TOLERANCE. The lines of each run in EACH_PARTS parts, and all runs of both builds run at
once; valgrind's counts do not depend on that. The CLI runs with an empty settings file, HOME in a
fresh folder and the dictionary cache off. The dictionaries of each build are part of what is counted.
Counts depend on the compiler, the C library, the processor architecture and valgrind, so the two
builds are measured on one machine with one toolchain.
--report prints the counts of this build and of the --against build with the change of each row; a
row more than TOLERANCE above the --against build gets a line starting `over:`, one more than
TOLERANCE below a line starting `under:`. When the --against build cannot be measured, this build is
reported alone after a line starting `perf-budget: no base`.
Exit: 0 measured and reported, 2 invalid arguments or a measurement of this build that failed
(valgrind missing, a run that failed or took longer than RUN_TIMEOUT, or an error of this script).
"""
import contextlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOLERANCE = 0.01
RSS_TOLERANCE = 0.05
ROWS = ["load", "each", "document", "first-line", "long-line"]
RSS_ROWS = ["kana-run"]
FIRST_LINE = "今日はいい天気ですね。"
LONG_CLAUSE = "彼は昨日学校に行って友達と話をして"
LONG_REPEAT = 200
KANA_RUN_CHARS = 16000
RSS_RUNS = 3
HIRAGANA = "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
EACH_PARTS = 2
EACH_DIRS = ["tests/rules/cases", "tests/rules/speech-strict"]
RUN_TIMEOUT = 600  # seconds for one CLI run under valgrind
IREFS = re.compile(r"^==\d+== I\s+refs:\s+([\d,]+)", re.M)


class MeasureError(Exception):
    pass


def git(root, *args, check=True):
    r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    if check and r.returncode:
        raise MeasureError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r


def file_at(root, commit, path):
    return git(root, "show", f"{commit}:{path}").stdout


def inputs(root, commit):
    """{row: (text, CLI options)} from the files at commit."""
    def listed(*dirs):
        names = git(root, "ls-tree", "-r", "--name-only", commit, "--", *dirs).stdout.split("\n")
        return sorted(n for n in names if n)

    each = []
    for path in listed(*EACH_DIRS):
        if not path.endswith(".tsv"):
            continue
        header = None
        for line in file_at(root, commit, path).split("\n"):
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if header is None:
                header = cells.index("input")
                continue
            each.append(cells[header])
    document = []
    for path in listed("examples"):
        if path.endswith(".ja.txt"):
            document += file_at(root, commit, path).split("\n")[1:]
    return {"load": ("", []), "each": ("\n".join(each) + "\n", ["--each"]),
            "document": ("\n".join(line for line in document if line) + "\n", []),
            "first-line": (FIRST_LINE + "\n", []), "long-line": (LONG_CLAUSE * LONG_REPEAT + "\n", [])}


def kana_run():
    """KANA_RUN_CHARS hiragana from a fixed linear congruential sequence, the same in every Python."""
    state, out = 1, []
    for _ in range(KANA_RUN_CHARS):
        state = (state * 1103515245 + 12345) % 2**31
        out.append(HIRAGANA[(state >> 16) % len(HIRAGANA)])
    return "".join(out) + "\n"


def runs(rows):
    """The CLI runs of the rows, (row, text, options); the independent lines of each are split into
    EACH_PARTS runs."""
    out = []
    for name, (text, options) in rows.items():
        if name == "each":
            lines = text.split("\n")[:-1]
            step = -(-len(lines) // EACH_PARTS) or 1
            out += [(name, "\n".join(lines[i:i + step]) + "\n", options) for i in range(0, len(lines), step)]
        else:
            out.append((name, text, options))
    return out


def valgrind_count(cmd, env):
    try:
        r = subprocess.run(["valgrind", "--tool=cachegrind", "--cache-sim=no", "--cachegrind-out-file=/dev/null",
                            *cmd], capture_output=True, text=True, encoding="utf-8", errors="replace", env=env,
                           timeout=RUN_TIMEOUT)
    except subprocess.TimeoutExpired as e:
        raise MeasureError(f"{' '.join(cmd)} did not finish in {RUN_TIMEOUT} s") from e
    except OSError as e:
        raise MeasureError(f"valgrind: {e}") from e
    m = IREFS.search(r.stderr)
    if r.returncode or not m:
        raise MeasureError(f"{' '.join(cmd)} failed ({r.returncode}):\n{r.stderr[-4000:]}")
    return int(m.group(1).replace(",", ""))


def peak_rss(cmd, env):
    """The peak resident memory of a run in KiB."""
    try:
        p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    except OSError as e:
        raise MeasureError(f"{cmd[0]}: {e}") from e
    _, status, usage = os.wait4(p.pid, 0)
    p.returncode = status  # reaped here; Popen must not wait again
    if status != 0:
        raise MeasureError(f"{' '.join(cmd)} failed ({status})")
    return usage.ru_maxrss // 1024 if sys.platform == "darwin" else usage.ru_maxrss


def measure_builds(root, builds, commit, count=valgrind_count, rss=peak_rss):
    """{build: {row: count}} for builds {build: (cli, dicts)}: instructions of each row above load, and the
    peak resident memory of the RSS rows."""
    rows = runs(inputs(root, commit))
    with tempfile.TemporaryDirectory() as tmp:
        home = Path(tmp) / "home"
        home.mkdir()
        config = Path(tmp) / "empty.yaml"
        config.write_text("", encoding="utf-8")
        env = dict(os.environ, HOME=str(home), USERPROFILE=str(home), HDTRANS_CACHE_DIR="off", LC_ALL="C")
        commands = []
        for i, (name, text, options) in enumerate(rows):
            path = Path(tmp) / f"{i}.txt"
            path.write_text(text, encoding="utf-8")
            for build, (cli, dicts) in builds.items():
                commands.append((build, name, [str(cli), "--config", str(config), "--dicts", str(dicts), *options,
                                               "--in", str(path), "--out", os.devnull]))
        def attempt(c):
            try:
                return count(c[2], env)
            except MeasureError as e:
                return e

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
            counts = list(pool.map(attempt, commands))
        kana = Path(tmp) / "kana-run.txt"
        kana.write_text(kana_run(), encoding="utf-8")
        peaks = {}
        for build, (cli, dicts) in builds.items():
            cmd = [str(cli), "--config", str(config), "--dicts", str(dicts), "--in", str(kana), "--out", os.devnull]
            try:
                peaks[build] = sorted(rss(cmd, env) for _ in range(RSS_RUNS))[RSS_RUNS // 2]
            except MeasureError as e:
                peaks[build] = e
    out, errors = {}, {}
    for build in builds:
        mine = [(name, n) for (b, name, _), n in zip(commands, counts) if b == build]
        failed = [n for _, n in mine if isinstance(n, MeasureError)]
        if isinstance(peaks[build], MeasureError):
            failed.append(peaks[build])
        if failed:
            errors[build] = failed[0]
            continue
        load = next(n for name, n in mine if name == "load")
        total = dict.fromkeys(ROWS, 0)
        for name, n in mine:
            total[name] += n if name == "load" else n - load
        total["kana-run"] = peaks[build]
        out[build] = total
    return out, errors


def measure(root, cli, dicts, commit, count=valgrind_count, rss=peak_rss):
    out, errors = measure_builds(root, {"head": (cli, dicts)}, commit, count, rss)
    if errors:
        raise errors["head"]
    return out["head"]


def compare(measured, baseline):
    """(over, under): (row, count, baseline) beyond the tolerance either way."""
    over, under = [], []
    for name in ROWS + RSS_ROWS:
        n, b = measured[name], baseline[name]
        tolerance = RSS_TOLERANCE if name in RSS_ROWS else TOLERANCE
        if n > b * (1 + tolerance):
            over.append((name, n, b))
        elif n < b * (1 - tolerance):
            under.append((name, n, b))
    return over, under


def default_base(root):
    refs = ["origin/develop"] + [r for r in git(root, "for-each-ref", "--format=%(refname:short)",
                                                 "refs/remotes/*/develop").stdout.split() if r != "origin/develop"]
    for ref in refs + ["develop"]:
        base = git(root, "merge-base", "HEAD", ref, check=False).stdout.strip()
        if base:
            return base
    raise MeasureError("no develop branch to take the merge base with; pass --base REF")


def percent(n, b):
    return f"{(n - b) / b * 100:+.2f}%"


def report(root, cli, dicts, base, against=None, count=valgrind_count, rss=peak_rss):
    """Prints the counts of this build and of the against build; returns 0, as a rise is only reported."""
    builds = {"head": (cli, dicts)}
    if against:
        builds["base"] = against
    measured, errors = measure_builds(root, builds, base, count, rss)
    if "head" in errors:
        raise errors["head"]
    head = measured["head"]
    if "base" in errors:
        print(f"perf-budget: no base: the base build could not be measured ({errors['base']})")
    elif not against:
        print("perf-budget: no base build to compare with")
    reference = measured.get("base")
    print("row\tthis build" + ("\tbase build\tchange" if reference else ""))
    for row in ROWS + RSS_ROWS:
        print("\t".join([row, str(head[row])] + ([str(reference[row]), percent(head[row], reference[row])]
                                                 if reference else [])))
    if reference:
        over, under = compare(head, reference)
        def unit(row):
            return "KiB peak resident memory" if row in RSS_ROWS else "instructions"
        for row, n, b in over:
            print(f"over: {row}: {n} {unit(row)}, {percent(n, b)} against the base build ({b})")
        for row, n, b in under:
            print(f"under: {row}: {n} {unit(row)}, {percent(n, b)} against the base build ({b})")
    return 0


def self_test():
    assert IREFS.search("==12== Cachegrind, a high-precision tracing profiler\n==12== I   refs:      1,234,567\n")
    rows = {"load": 100, "each": 2000, "document": 300, "first-line": 50, "long-line": 70, "kana-run": 1000}
    assert compare(dict(rows, each=2021, document=296), rows) == ([("each", 2021, 2000)], [("document", 296, 300)])
    assert compare(dict(rows, load=101, each=2020, document=297), rows) == ([], [])
    assert compare(dict(rows, **{"kana-run": 1040}), rows) == ([], [])
    assert compare(dict(rows, **{"kana-run": 900}), rows) == ([], [("kana-run", 900, 1000)])
    run = kana_run()
    assert len(run) == KANA_RUN_CHARS + 1 and set(run[:-1]) <= set(HIRAGANA) and len(set(run)) > 40
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "examples").mkdir()
        (root / "tests" / "rules" / "cases").mkdir(parents=True)
        (root / "examples" / "a.ja.txt").write_text("# case\nあ\n\nい\n", encoding="utf-8")
        (root / "tests" / "rules" / "cases" / "t.tsv").write_text(
            "# R-1\nrule\tinput\texpected\tkind\tlayer\nx\tう\tu\tfix\tlocal\nx\tえお\teo\tkeep\tlocal\n",
            encoding="utf-8")

        def run(*args):
            subprocess.run(["git", "-C", tmp, "-c", "user.name=t", "-c", "user.email=t@t", *args],
                           check=True, capture_output=True)

        cost = {"head": 1000, "base": 1000}
        memory = {"head": 5000, "base": 5000}
        broken = set()

        def fake(cmd, env):
            assert env["HDTRANS_CACHE_DIR"] == "off" and Path(env["HOME"]).is_dir()
            if cmd[0] in broken:
                raise MeasureError(f"{cmd[0]} crashed")
            text = Path(cmd[cmd.index("--in") + 1]).read_text(encoding="utf-8")
            return cost[cmd[0]] + len(text.encode("utf-8")) * (3 if "--each" in cmd else 1)

        def fake_rss(cmd, env):
            assert env["HDTRANS_CACHE_DIR"] == "off"
            if cmd[0] in broken:
                raise MeasureError(f"{cmd[0]} crashed")
            assert Path(cmd[cmd.index("--in") + 1]).read_text(encoding="utf-8") == kana_run()
            return memory[cmd[0]]

        def rep(against=True):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                status = report(root, "head", "dicts", base, ("base", "dicts") if against else None, fake, fake_rss)
            return status, buf.getvalue()

        saved = {k: os.environ.get(k) for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM")}
        os.environ.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        try:
            run("init", "-q")
            run("add", "-A")
            run("commit", "-q", "-m", "inputs")
            base = git(root, "rev-parse", "HEAD").stdout.strip()
            got = inputs(root, base)
            assert got["each"] == ("う\nえお\n", ["--each"]) and got["document"] == ("あ\nい\n", []), got
            first = len((FIRST_LINE + "\n").encode("utf-8"))
            long = len((LONG_CLAUSE * LONG_REPEAT + "\n").encode("utf-8"))
            assert measure(root, "head", "dicts", base, fake, fake_rss) == {
                "load": 1000, "each": 33, "document": 8, "first-line": first, "long-line": long, "kana-run": 5000}
            # Later edits of the inputs do not move the counts: both builds read the base commit.
            (root / "examples" / "a.ja.txt").write_text("# case\nあああ\n", encoding="utf-8")
            run("commit", "-q", "-am", "edit an example")
            status, out = rep()
            assert status == 0 and "over:" not in out and "document\t8\t8\t+0.00%" in out, out
            # A rise is reported, never failed.
            cost["head"] = 1030
            status, out = rep()
            assert status == 0 and "over: load: 1030 instructions, +3.00%" in out, out
            cost["head"], cost["base"] = 1000, 1030
            status, out = rep()
            assert status == 0 and "under: load:" in out and "over:" not in out, out
            cost["base"] = 1000
            memory["head"] = 6000
            status, out = rep()
            assert status == 0 and "over: kana-run: 6000 KiB peak resident memory, +20.00%" in out, out
            memory["head"] = 5000
            # A base build that cannot run, or none, leaves this build reported alone.
            broken.add("base")
            status, out = rep()
            assert status == 0 and "perf-budget: no base:" in out and "load\t1000\n" in out, out
            broken.clear()
            status, out = rep(False)
            assert status == 0 and "no base build" in out and "change" not in out, out
            broken.add("head")
            try:
                rep()
                raise AssertionError("no error")
            except MeasureError:
                pass
        finally:
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
    print("self-test ok")
    return 0


def main(argv):
    base = None
    against = None
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == "--base" and i + 1 < len(argv):
            base = argv[i + 1]
            i += 2
        elif argv[i] == "--against" and i + 2 < len(argv):
            against = (Path(argv[i + 1]).resolve(), Path(argv[i + 2]).resolve())
            i += 3
        else:
            rest.append(argv[i])
            i += 1
    if rest == ["--self-test"]:
        return self_test()
    if len(rest) != 3 or rest[0] not in ("--report", "--measure") or (against and rest[0] != "--report"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cli, dicts = Path(rest[1]).resolve(), Path(rest[2]).resolve()
    try:
        for p in [cli, dicts, *(against or ())]:
            if not p.exists():
                raise MeasureError(f"{p} does not exist")
        if not shutil.which("valgrind"):
            raise MeasureError("valgrind is not installed")
        commit = base or default_base(ROOT)
        if rest[0] == "--report":
            return report(ROOT, cli, dicts, commit, against)
        for name, n in measure(ROOT, cli, dicts, commit).items():
            print(f"{name}\t{n}")
        return 0
    except MeasureError as e:
        print(f"perf-budget: not measured: {e}")
        return 2
    except Exception:  # a defect of this script is not a slower engine
        traceback.print_exc(file=sys.stdout)
        print("perf-budget: failed")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
