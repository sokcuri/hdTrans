#!/usr/bin/env python3
"""Counts the instructions the CLI runs for fixed inputs under valgrind and compares them with a build
of the base commit measured in the same run, and reports them against tools/perf-budget.tsv.

  tools/perf-budget.py --check CLI DICTS [--against CLI DICTS] [--base REF] [--out FILE]
  tools/perf-budget.py --update CLI DICTS [--base REF]   write the counts into the table
  tools/perf-budget.py --measure CLI DICTS               print the counts
  tools/perf-budget.py --self-test

DICTS is what --dicts takes (the CI passes the compiled snapshot of each build). The inputs are read
from the commit on the `inputs` line of the table, a develop commit, so that later edits of those
files do not move the counts:
  each      the inputs of the rule cases (tests/rules/cases, tests/rules/speech-strict), --each
  document  the examples (examples/*.ja.txt without their first line) as one document
Every run loads the dictionaries; `load` is a run on empty input, and the other rows count the
instructions above it. The lines of each run in EACH_PARTS parts, and all runs of both builds run at
once; valgrind's counts do not depend on that. The CLI runs with an empty settings file, HOME in a
fresh folder and the dictionary cache off. The dictionaries of each build are part of what is counted.
Counts depend on the compiler, the C library, the processor architecture and valgrind: the toolchain.
--check compares with the --against build (the base commit) when both have the same toolchain; a
count more than TOLERANCE above it fails, unless a commit of REF..HEAD (--base) carries a
`Perf-Budget: <reason>` trailer. Without such a base build, or when it cannot be measured, the counts
are compared with the table and only reported: the table is a record of earlier counts and never
fails a run. A count more than TOLERANCE below is reported. --out writes the measured counts as a
table. --update writes them into the table (REF defaults to the merge base with develop, which is
also the `inputs` of a new table).
Exit: 0 within the base build or only reported, 1 over the base build without a trailer, 2 invalid
arguments or a measurement that failed (valgrind missing, a run that failed or took longer than
RUN_TIMEOUT, or an error of this script).
"""
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = "tools/perf-budget.tsv"
TOLERANCE = 0.01
TRAILER = re.compile(r"^Perf-Budget:[ \t]*\S", re.M)
ROWS = ["load", "each", "document"]
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


def empty_table():
    return {"inputs": "", "toolchain": "", "rows": {}}


def read_table(text):
    """{"inputs": commit, "toolchain": text, "rows": {name: count}}"""
    table = empty_table()
    header = False
    for line in text.split("\n"):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if cells[0] in ("inputs", "toolchain") and len(cells) == 2:
            table[cells[0]] = cells[1]
        elif cells == ["row", "instructions"]:
            header = True
        elif header and len(cells) == 2:
            table["rows"][cells[0]] = int(cells[1])
    return table


def write_table(table):
    out = ["# Instruction counts of tools/perf-budget.py: the inputs at the commit `inputs`, measured with",
           "# `toolchain`; each/document count above load.",
           f"inputs\t{table['inputs']}", f"toolchain\t{table['toolchain']}", "row\tinstructions"]
    out += [f"{name}\t{table['rows'][name]}" for name in ROWS if name in table["rows"]]
    return "\n".join(out) + "\n"


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
            "document": ("\n".join(line for line in document if line) + "\n", [])}


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


def run_text(*cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    except OSError:
        return ""


def toolchain(cli):
    """The compiler that built CLI (its .comment section, else the CMake cache next to it), the C
    library, the processor architecture and the valgrind version."""
    parts = []
    comment = run_text("readelf", "-p", ".comment", str(cli)) if shutil.which("readelf") else ""
    compilers = sorted({m.strip() for m in re.findall(r"^\s*\[\s*\w+\]\s+(.+)$", comment, re.M)})
    if compilers:
        parts.append("; ".join(compilers))
    else:
        cache = Path(cli).resolve().parent / "CMakeCache.txt"
        m = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.+)$", cache.read_text(encoding="utf-8", errors="replace"),
                      re.M) if cache.exists() else None
        if m and shutil.which(m.group(1)):
            parts.append(run_text(m.group(1), "--version").split("\n")[0].strip())
    libc = os.confstr("CS_GNU_LIBC_VERSION") if "CS_GNU_LIBC_VERSION" in os.confstr_names else ""
    if libc:
        parts.append(libc)
    parts.append(platform.machine())
    if shutil.which("valgrind"):
        parts.append(run_text("valgrind", "--version").strip())
    return ", ".join(p for p in parts if p)


def measure_builds(root, builds, commit, count=valgrind_count):
    """{build: {row: instructions}} for builds {build: (cli, dicts)}, each row above load."""
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
    out, errors = {}, {}
    for build in builds:
        mine = [(name, n) for (b, name, _), n in zip(commands, counts) if b == build]
        failed = [n for _, n in mine if isinstance(n, MeasureError)]
        if failed:
            errors[build] = failed[0]
            continue
        load = next(n for name, n in mine if name == "load")
        total = dict.fromkeys(ROWS, 0)
        for name, n in mine:
            total[name] += n if name == "load" else n - load
        out[build] = total
    return out, errors


def measure(root, cli, dicts, commit, count=valgrind_count):
    out, errors = measure_builds(root, {"head": (cli, dicts)}, commit, count)
    if errors:
        raise errors["head"]
    return out["head"]


def compare(measured, baseline):
    """(over, under): (row, count, baseline) beyond the tolerance either way; a row without a
    baseline is over."""
    over, under = [], []
    for name in ROWS:
        n, b = measured[name], baseline.get(name)
        if b is None or n > b * (1 + TOLERANCE):
            over.append((name, n, b))
        elif n < b * (1 - TOLERANCE):
            under.append((name, n, b))
    return over, under


def has_trailer(root, base):
    return bool(TRAILER.search(git(root, "log", "--format=%B", f"{base}..HEAD").stdout))


def default_base(root):
    refs = ["origin/develop"] + [r for r in git(root, "for-each-ref", "--format=%(refname:short)",
                                                 "refs/remotes/*/develop").stdout.split() if r != "origin/develop"]
    for ref in refs + ["develop"]:
        base = git(root, "merge-base", "HEAD", ref, check=False).stdout.strip()
        if base:
            return base
    raise MeasureError("no develop branch to take the merge base with; pass --base REF")


def percent(n, b):
    return f"{(n - b) / b * 100:+.2f}%" if b else "new"


def check(root, cli, dicts, base, out=None, against=None, count=valgrind_count, tc=toolchain):
    path = Path(root) / TABLE
    table = read_table(path.read_text(encoding="utf-8")) if path.exists() else empty_table()
    if not table["inputs"]:
        raise MeasureError(f"{TABLE}: no inputs commit")
    builds = {"head": (cli, dicts)}
    if against:
        builds["base"] = against
    measured, errors = measure_builds(root, builds, table["inputs"], count)
    if "head" in errors:
        raise errors["head"]
    if "base" in errors:
        print(f"perf-budget: the base build could not be measured ({errors['base']}); reported only")
        against = None
    head, mine = measured["head"], tc(cli)
    if out:
        Path(out).write_text(write_table({"inputs": table["inputs"], "toolchain": mine, "rows": head}),
                             encoding="utf-8")
    if against and tc(against[0]) == mine:
        reference, name, gating = measured["base"], "the base build", True
    else:
        if against:
            print(f"perf-budget: the base build has another toolchain ({tc(against[0])}); reported only")
        elif "base" not in errors:
            print("perf-budget: no base build; reported only")
        reference, name, gating = table["rows"], TABLE, False
        if table["toolchain"] != mine:
            print(f"perf-budget: measured with {mine}, {TABLE} with {table['toolchain'] or 'nothing'}")
    print("row\tthis build\t" + ("base build\t" if "base" in measured else "") + f"{TABLE}\tchange")
    for row in ROWS:
        cells = [row, str(head[row])]
        if "base" in measured:
            cells.append(str(measured["base"][row]))
        cells += [str(table["rows"].get(row, "-")), percent(head[row], reference.get(row))]
        print("\t".join(cells))
    status = 0
    over, under = compare(head, reference)
    trailer = gating and bool(base) and has_trailer(root, base)
    for row, n, b in over:
        verdict = "with a Perf-Budget trailer" if trailer else "over the budget" if gating else "reported only"
        print(f"{row}: {n} instructions, {percent(n, b)} against {name} ({b}); {verdict}")
        if gating and not trailer:
            status = 1
    for row, n, b in under:
        print(f"{row}: {n} instructions, {percent(n, b)} against {name} ({b})")
    print("perf-budget: " + ("over" if status else "ok"))
    return status


def update(root, cli, dicts, base, count=valgrind_count, tc=toolchain):
    path = Path(root) / TABLE
    table = read_table(path.read_text(encoding="utf-8")) if path.exists() else empty_table()
    commit = table["inputs"] or base or default_base(root)
    new = {"inputs": commit, "toolchain": tc(cli), "rows": measure(root, cli, dicts, commit, count)}
    path.write_text(write_table(new), encoding="utf-8")
    print(f"perf-budget: wrote {TABLE}")
    return 0


def self_test():
    assert IREFS.search("==12== Cachegrind, a high-precision tracing profiler\n==12== I   refs:      1,234,567\n")
    t = {"inputs": "abc", "toolchain": "cc 1, valgrind-3", "rows": {"load": 100, "each": 2000, "document": 300}}
    assert read_table(write_table(t)) == t
    assert compare({"load": 100, "each": 2021, "document": 296}, t["rows"]) == ([("each", 2021, 2000)],
                                                                                [("document", 296, 300)])
    assert compare({"load": 101, "each": 2020, "document": 297}, t["rows"]) == ([], [])
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "tools").mkdir()
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
        chains = {"head": "cc 1", "base": "cc 1"}
        broken = set()

        def fake(cmd, env):
            assert env["HDTRANS_CACHE_DIR"] == "off" and Path(env["HOME"]).is_dir()
            if cmd[0] in broken:
                raise MeasureError(f"{cmd[0]} crashed")
            text = Path(cmd[cmd.index("--in") + 1]).read_text(encoding="utf-8")
            return cost[cmd[0]] + len(text.encode("utf-8")) * (3 if "--each" in cmd else 1)

        def fake_tc(cli):
            return chains[str(cli)]

        def fail(cmd, env):
            raise MeasureError("valgrind: not found")

        def chk(base, against=None, out=None):
            return check(root, "head", "dicts", base, out, ("base", "dicts") if against else None, fake, fake_tc)

        quiet = open(os.devnull, "w")
        stdout, sys.stdout = sys.stdout, quiet
        saved = {k: os.environ.get(k) for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM")}
        os.environ.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        try:
            run("init", "-q")
            run("add", "-A")
            run("commit", "-q", "-m", "inputs")
            base = git(root, "rev-parse", "HEAD").stdout.strip()
            got = inputs(root, base)
            assert got["each"] == ("う\nえお\n", ["--each"]) and got["document"] == ("あ\nい\n", []), got
            assert measure(root, "head", "dicts", base, fake) == {"load": 1000, "each": 33, "document": 8}
            assert update(root, "head", "dicts", base, fake, fake_tc) == 0
            assert read_table((root / TABLE).read_text(encoding="utf-8"))["inputs"] == base
            run("add", "-A")
            run("commit", "-q", "-m", "table")
            base = git(root, "rev-parse", "HEAD").stdout.strip()
            # Later edits of the inputs do not move the counts.
            (root / "examples" / "a.ja.txt").write_text("# case\nあああ\n", encoding="utf-8")
            run("commit", "-q", "-am", "edit an example")
            assert chk(base) == 0 and chk(base, True) == 0
            out = root / "measured.tsv"
            cost["head"] = 1030
            # The table only reports; the base build decides.
            assert chk(base, False, out) == 0 and chk(base, True) == 1
            assert read_table(out.read_text(encoding="utf-8"))["rows"]["load"] == 1030
            # The base build moved as much: the change is not this branch's.
            cost["base"] = 1030
            assert chk(base, True) == 0
            cost["base"] = 1000
            # A base build of another toolchain, or one that cannot run, only reports.
            chains["base"] = "cc 2"
            assert chk(base, True) == 0
            chains["base"] = "cc 1"
            broken.add("base")
            assert chk(base, True) == 0
            broken.clear()
            broken.add("head")
            try:
                chk(base, True)
                raise AssertionError("no error")
            except MeasureError:
                pass
            broken.clear()
            run("commit", "-q", "--allow-empty", "-m", "slower\n\nPerf-Budget: a test reason")
            assert chk(base, True) == 0
            assert update(root, "head", "dicts", base, fake, fake_tc) == 0
            assert read_table((root / TABLE).read_text(encoding="utf-8"))["rows"]["load"] == 1030
            try:
                check(root, "head", "dicts", None, None, None, fail, fake_tc)
                raise AssertionError("no error")
            except MeasureError:
                pass
        finally:
            sys.stdout = stdout
            quiet.close()
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
    print("self-test ok")
    return 0


def main(argv):
    values = {"--base": None, "--out": None}
    against = None
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] in values and i + 1 < len(argv):
            values[argv[i]] = argv[i + 1]
            i += 2
        elif argv[i] == "--against" and i + 2 < len(argv):
            against = (Path(argv[i + 1]).resolve(), Path(argv[i + 2]).resolve())
            i += 3
        else:
            rest.append(argv[i])
            i += 1
    if rest == ["--self-test"]:
        return self_test()
    if len(rest) != 3 or rest[0] not in ("--check", "--update", "--measure"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cli, dicts = Path(rest[1]).resolve(), Path(rest[2]).resolve()
    try:
        for p in [cli, dicts, *(against or ())]:
            if not p.exists():
                raise MeasureError(f"{p} does not exist")
        if not shutil.which("valgrind"):
            raise MeasureError("valgrind is not installed")
        if rest[0] == "--check":
            return check(ROOT, cli, dicts, values["--base"], values["--out"], against)
        if rest[0] == "--update":
            return update(ROOT, cli, dicts, values["--base"])
        table = read_table((ROOT / TABLE).read_text(encoding="utf-8")) if (ROOT / TABLE).exists() else empty_table()
        for name, n in measure(ROOT, cli, dicts, table["inputs"] or default_base(ROOT)).items():
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
