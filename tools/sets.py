#!/usr/bin/env python3
"""Translates measurement sets fast and compares two runs.

  tools/sets.py run [-c CLI] [-d DICTS] [-j JOBS] [--cache DIR] [--base RUN --needles FILE [--canary N]]
                    RUN SPEC... [-- CLI-OPTIONS...]
  tools/sets.py pairs BASE_RUN HEAD_RUN
  tools/sets.py --self-test

SPEC is NAME=FILE (every line its own session, as --each), NAME=FILE@doc (the file is one document) or
NAME=FILE@blocks=N (the file cut into documents of N lines). `run` writes RUN/NAME.txt, line for
line with FILE, and RUN/NAME.json. In each-line mode it translates every distinct line once, in
shards on JOBS processes (default: CPU count); documents and blocks also run JOBS at a time, and the
sets run together. CLI defaults to build/<preset>/hdtrans and DICTS to dicts/ of this tree. Every
process has a fresh HOME and no user dictionary; the dictionaries are compiled once per run into a
cache folder that the processes share.

--cache DIR keeps finished full runs under a key of the CLI bytes, the dictionary files, FILE, the
mode and the CLI options; a run with the same key is copied from there instead of translated, so a
pinned base build is measured once for every fix made on it.

--base RUN --needles FILE translates only the lines that hold a needle (tools/affected.py; lines are
folded by its fold()) and the documents or blocks that hold such a line, and copies the rest from
RUN, the base run of the same sets. A needle file holding `*` translates everything. --canary N
also translates N seeded distinct lines without a needle per each-line set and fails when one of
them differs from RUN, which means the needles missed a way the change reaches the text.

`pairs` writes HEAD_RUN/NAME.pairs.tsv for every set both runs hold: the changed lines as key
(NAME:LINE), ja, before, after in the format of tools/judge.py, and prints the counts.
Exit: 0 done, 1 a canary line changed, 2 invalid arguments or a failed CLI.
"""
import concurrent.futures
import csv
import hashlib
import importlib.util
import json
import os
import platform
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("affected", HERE / "tools" / "affected.py")
_affected = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_affected)
fold = _affected.fold


def fail(message, code=2):
    print(message, file=sys.stderr)
    sys.exit(code)


def lines_of(data):
    text = data.decode("utf-8")
    if text == "":
        return []
    return (text[:-1] if text.endswith("\n") else text).split("\n")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def dicts_hash(folder):
    h = hashlib.sha256()
    root = Path(folder)
    for p in sorted(root.rglob("*")):
        if p.is_file():
            h.update(p.relative_to(root).as_posix().encode() + b"\0" + sha(p.read_bytes()).encode() + b"\n")
    return h.hexdigest()


class Spec:
    def __init__(self, text):
        name, eq, rest = text.partition("=")
        if not eq or not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            fail(f"bad set {text}: NAME=FILE[@doc|@blocks=N]")
        self.name, self.mode, self.block = name, "each", 0
        m = re.fullmatch(r"(.*)@(doc|blocks=(\d+))", rest)
        if m:
            rest, self.mode = m.group(1), "doc" if m.group(2) == "doc" else "blocks"
            self.block = int(m.group(3) or 0)
            if self.mode == "blocks" and self.block < 1:
                fail(f"bad block size in {text}")
        self.path = Path(rest).resolve()
        if not self.path.is_file():
            fail(f"no file at {self.path}")
        self.data = self.path.read_bytes()
        self.lines = lines_of(self.data)

    def mode_text(self):
        return f"blocks={self.block}" if self.mode == "blocks" else self.mode


class Runner:
    """Runs CLI processes JOBS at a time, each on its own input file."""

    def __init__(self, cli, dicts, jobs, options, tmp):
        self.cli, self.dicts, self.options, self.tmp = cli, dicts, options, tmp
        self.timeout = int(os.environ.get("HDTRANS_TIMEOUT", "1800"))
        self.pool = concurrent.futures.ThreadPoolExecutor(jobs)
        self.count, self.lock = 0, threading.Lock()
        home = tmp / "home"
        home.mkdir()
        self.env = dict(os.environ, HOME=str(home), HDTRANS_CACHE_DIR=str(tmp / "cache"))
        for var in ("HDTRANS_SNAPSHOT", "HDTRANS_VERIFY_DICTS", "HDTRANS_DRIVER_DUMP"):
            self.env.pop(var, None)
        (tmp / "empty.txt").write_bytes(b"")
        self.call([], tmp / "empty.txt")  # compiles the dictionaries into the shared cache

    def call(self, flags, path):
        out = path.with_suffix(".out")
        run = subprocess.run([str(self.cli), "--dicts", str(self.dicts), "--userdict", "", *flags,
                              *self.options, "--in", str(path), "--out", str(out)],
                             env=self.env, capture_output=True, timeout=self.timeout)
        if run.returncode:
            raise RuntimeError(f"CLI failed on {path}: {run.stderr.decode(errors='replace')[-500:]}")
        return out.read_bytes()

    def translate(self, flags, lines, raw=None):
        """A future of the output lines of lines, each alone (flags --each) or as one document
        (raw: the document bytes when they are a whole input file)."""
        with self.lock:
            self.count += 1
            path = self.tmp / f"in.{self.count}.txt"
        if raw is None:
            raw = "\n".join(lines).encode("utf-8") + (b"\n" if flags and lines else b"")
        path.write_bytes(raw)

        def job():
            data = self.call(flags, path)
            if flags:
                out = lines_of(data)
            else:  # a document of k line breaks has k + 1 lines, the last one empty after a final break
                out = data.decode("utf-8").split("\n")
                if len(out) == len(lines) + 1 and out[-1] == "" and raw.endswith(b"\n"):
                    out.pop()
            if len(out) != len(lines):
                raise RuntimeError(f"line counts differ: {len(lines)} in, {len(out)} out ({path})")
            return out
        return self.pool.submit(job)


def shards(items, count):
    """items cut into about count runs of similar byte length, in order."""
    weight = [len(s.encode()) + 16 for s in items]
    size, runs, run, used = (sum(weight) or 1) / max(count, 1), [], [], 0
    for s, w in zip(items, weight):
        run.append(s)
        used += w
        if used >= size * (len(runs) + 1):
            runs.append(run)
            run = []
    if run:
        runs.append(run)
    return runs


def cache_key(cli_sha, dicts_sha, spec, options):
    return sha("\n".join([cli_sha, dicts_sha, sha(spec.data), spec.mode_text(), *options]).encode())


def documents(spec):
    """(first line, line count) of the documents of a set."""
    if spec.mode == "doc":
        return [(0, len(spec.lines))]
    return [(i, min(spec.block, len(spec.lines) - i)) for i in range(0, len(spec.lines), spec.block)]


def plan(spec, runner, jobs, selected, canary):
    """Submits the translations of one set; returns a function of the base lines (or None) that
    gives the output lines, the number of units translated and the canary lines that changed."""
    lines = spec.lines
    if spec.mode == "each":
        wanted = list(dict.fromkeys(lines[i] for i in selected))
        extra = []
        if canary:
            rest = sorted(set(lines) - set(wanted))
            extra = random.Random(f"{spec.name}:{len(lines)}").sample(rest, min(canary, len(rest)))
        todo = wanted + extra
        parts = shards(todo, jobs * 3 if len(todo) > 200 else 1)
        futures = [runner.translate(["--each"], part) for part in parts]

        def done(base):
            got = {}
            for part, f in zip(parts, futures):
                got.update(zip(part, f.result()))
            bad = []
            if extra:
                first = {}
                for i, s in enumerate(lines):
                    first.setdefault(s, i)
                bad = [s for s in extra if got[s] != base[first[s]]]
            out = list(base) if base is not None else [None] * len(lines)
            for i in selected:
                out[i] = got[lines[i]]
            return out, len(todo), bad
        return done
    docs = [d for d in documents(spec) if any(i in selected for i in range(d[0], d[0] + d[1]))]
    whole = spec.mode == "doc"
    futures = [runner.translate([], lines[s:s + n], spec.data if whole else None) for s, n in docs]

    def done(base):
        out = list(base) if base is not None else [None] * len(lines)
        for (s, n), f in zip(docs, futures):
            out[s:s + n] = f.result()
        return out, len(docs), []
    return done


def run(argv):
    preset = os.environ.get("HDTRANS_PRESET") or ("mac" if platform.system() == "Darwin" else "linux")
    cli, dicts, jobs = HERE / "build" / preset / "hdtrans", HERE / "dicts", os.cpu_count() or 4
    cache = base = needles_path = None
    canary, options, args = 0, [], []
    if "--" in argv:
        options = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    it = iter(argv)
    for a in it:
        def value():
            v = next(it, None)
            return v if v is not None else fail(f"{a} needs a value")
        if a == "-c":
            cli = Path(value())
        elif a == "-d":
            dicts = Path(value())
        elif a == "-j":
            jobs = int(value())
        elif a == "--cache":
            cache = Path(value())
        elif a == "--base":
            base = Path(value())
        elif a == "--needles":
            needles_path = Path(value())
        elif a == "--canary":
            canary = int(value())
        elif a.startswith("-"):
            fail(f"unknown option {a}")
        else:
            args.append(a)
    if len(args) < 2:
        fail(__doc__.strip().split("\n\n")[1])
    if (base is None) != (needles_path is None):
        fail("--base and --needles go together")
    if not os.access(cli, os.X_OK) or not dicts.is_dir():
        fail(f"no CLI at {cli} or no dictionaries at {dicts}")
    out = Path(args[0])
    specs = [Spec(s) for s in args[1:]]
    if len({s.name for s in specs}) != len(specs):
        fail("set names repeat")
    out.mkdir(parents=True, exist_ok=True)
    pattern = None
    if needles_path:
        needles = [n for n in needles_path.read_text(encoding="utf-8").split("\n") if n]
        if "*" not in needles:
            alternatives = "|".join(map(re.escape, sorted(set(needles), key=len, reverse=True)))
            pattern = re.compile(alternatives or "(?!)")
    cli_sha, dicts_sha = sha(cli.read_bytes()), dicts_hash(dicts)
    start = time.time()
    with tempfile.TemporaryDirectory(prefix="hdtrans-sets.") as tmp:
        runner = Runner(cli.resolve(), dicts.resolve(), jobs, options, Path(tmp))
        pending = []
        for spec in specs:
            key = cache_key(cli_sha, dicts_sha, spec, options)
            meta = {"set": spec.name, "file": str(spec.path), "input": sha(spec.data), "mode": spec.mode_text(),
                    "key": key, "lines": len(spec.lines), "cli": str(cli), "dicts": str(dicts), "options": options}
            hit = cache / f"{key}.txt" if cache else None
            if base is None and hit and hit.is_file():
                shutil.copyfile(hit, out / f"{spec.name}.txt")
                meta.update(cached=True, translated=0)
                (out / f"{spec.name}.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1) + "\n")
                print(f"{spec.name}: {len(spec.lines)} lines from the cache")
                continue
            base_lines, selected = None, range(len(spec.lines))
            if base is not None:
                b = base / f"{spec.name}.txt"
                bm = json.loads((base / f"{spec.name}.json").read_text()) if b.is_file() else None
                if not bm or [bm[k] for k in ("file", "input", "mode", "options")] != \
                        [str(spec.path), meta["input"], spec.mode_text(), options]:
                    fail(f"{base} has no run of {spec.name} from this {spec.path} as {spec.mode_text()}")
                base_lines = lines_of(b.read_bytes())
                if len(base_lines) != len(spec.lines):
                    fail(f"{b}: line count differs from {spec.path}")
                if pattern is not None:
                    selected = [i for i, s in enumerate(spec.lines) if pattern.search(fold(s))]
            pending.append((spec, meta, base_lines, selected,
                            plan(spec, runner, jobs, selected if pattern is None else set(selected),
                                 canary if pattern is not None and spec.mode == "each" else 0)))
        status = 0
        for spec, meta, base_lines, selected, done in pending:
            try:
                lines, units, bad = done(base_lines)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                runner.pool.shutdown(cancel_futures=True)
                fail(str(error))
            data = ("\n".join(lines) + "\n").encode("utf-8") if lines else b""
            (out / f"{spec.name}.txt").write_bytes(data)
            meta.update(cached=False, translated=units, selected=len(selected), partial=pattern is not None,
                        seconds=round(time.time() - start, 1))
            if pattern is None and cache:
                cache.mkdir(parents=True, exist_ok=True)
                part = cache / f".{meta['key']}.{os.getpid()}"
                part.write_bytes(data)
                part.replace(cache / f"{meta['key']}.txt")
            (out / f"{spec.name}.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1) + "\n")
            unit = "distinct lines" if spec.mode == "each" else "documents"
            note = f", {len(selected)} lines hold a needle" if pattern is not None else ""
            print(f"{spec.name}: {units} {unit} translated{note}, {round(time.time() - start, 1)} s")
            for s in bad:
                status = 1
                print(f"{spec.name}: canary line changed without a needle: {s}", file=sys.stderr)
        runner.pool.shutdown()
    return status


def pairs(argv):
    if len(argv) != 2:
        fail(__doc__.strip().split("\n\n")[1])
    base, head = Path(argv[0]), Path(argv[1])
    total = 0
    for meta_path in sorted(head.glob("*.json")):
        name = meta_path.stem
        meta = json.loads(meta_path.read_text())
        if not (base / f"{name}.txt").is_file():
            print(f"{name}: not in {base}")
            continue
        ja = lines_of(Path(meta["file"]).read_bytes())
        b = lines_of((base / f"{name}.txt").read_bytes())
        a = lines_of((head / f"{name}.txt").read_bytes())
        if not len(ja) == len(b) == len(a):
            fail(f"{name}: line counts differ")
        n = 0
        with open(head / f"{name}.pairs.tsv", "w", encoding="utf-8", newline="") as f:
            w = csv.writer(f, delimiter="\t", lineterminator="\n")
            for i, (x, y, z) in enumerate(zip(ja, b, a), 1):
                if y != z:
                    n += 1
                    w.writerow([f"{name}:{i}", x, y, z])
        total += n
        print(f"{name}: {n} changed lines ({head / f'{name}.pairs.tsv'})")
    print(f"total: {total} changed lines")
    return 0


FAKE_CLI = r"""#!/usr/bin/env python3
# Marks the dictionary words of each line; in document mode a line holding "ctx" also shows the line
# before it.
import os, sys
a = sys.argv[1:]
src, dst = a[a.index("--in") + 1], a[a.index("--out") + 1]
words = open(os.path.join(a[a.index("--dicts") + 1], "w.txt"), encoding="utf-8").read().split()
text = open(src, encoding="utf-8").read()
each = "--each" in a
lines = ((text[:-1] if text.endswith("\n") else text).split("\n") if text else []) if each else text.split("\n")
out, prev = [], ""
for s in lines:
    t = s.upper()
    for w in words:
        t = t.replace(w.upper(), "<" + w + ">")
    if "ctx" in s and not each:
        t += "|" + prev
    out.append(t)
    prev = s
open(dst, "w", encoding="utf-8").write("".join(t + "\n" for t in out) if each else "\n".join(out))
"""


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        cli = t / "cli"
        cli.write_text(FAKE_CLI)
        cli.chmod(0o755)
        for name, words in (("d0", "abc"), ("d1", "abc xyz")):
            (t / name).mkdir()
            (t / name / "w.txt").write_text(words)
        lines = [f"line {i} {'xyz' if i % 7 == 0 else 'abc'}" for i in range(300)] + ["", "ctx here", "xyz"]
        lines[49] = lines[99] = ""
        (t / "in.txt").write_text("\n".join(lines) + "\n")
        specs = [f"e={t}/in.txt", f"d={t}/in.txt@doc", f"b={t}/in.txt@blocks=50"]

        def call(*args):
            out = subprocess.run([sys.executable, "-I", __file__, *map(str, args)], capture_output=True, text=True)
            return out.returncode, out.stdout + out.stderr
        rc, log = call("run", "-c", cli, "-d", t / "d0", "-j", "4", "--cache", t / "cache", t / "base", *specs)
        assert rc == 0, log
        base = (t / "base/e.txt").read_text().split("\n")
        assert base[0] == "LINE 0 XYZ" and base[1] == "LINE 1 <abc>" and len(base) == 304, base[:3]
        assert (t / "base/d.txt").read_text().split("\n")[301] == "CTX HERE|"
        rc, log = call("run", "-c", cli, "-d", t / "d0", "--cache", t / "cache", t / "again", *specs)
        assert rc == 0 and log.count("from the cache") == 3, log
        rc, log = call("run", "-c", cli, "-d", t / "d1", "-j", "4", t / "full", *specs)
        assert rc == 0, log
        (t / "needles").write_text("xyz\n")
        rc, log = call("run", "-c", cli, "-d", t / "d1", "-j", "4", "--base", t / "base", "--needles",
                       t / "needles", "--canary", "20", t / "inc", *specs)
        assert rc == 0, log
        for s in ("e", "d", "b"):
            assert (t / "inc" / f"{s}.txt").read_bytes() == (t / "full" / f"{s}.txt").read_bytes(), s
        assert "e: 63 distinct lines translated, 43 lines hold a needle" in log, log
        assert "b: 7 documents translated" in log and "d: 1 documents translated" in log, log
        rc, log = call("pairs", t / "base", t / "inc")
        assert rc == 0 and "e: 43 changed lines" in log and "total: 129" in log, log
        with open(t / "inc/e.pairs.tsv", encoding="utf-8") as f:
            row = next(csv.reader(f, delimiter="\t"))
        assert row == ["e:1", "line 0 xyz", "LINE 0 XYZ", "LINE 0 <xyz>"], row
        (t / "needles").write_text("nothing\n")
        rc, log = call("run", "-c", cli, "-d", t / "d1", "--base", t / "base", "--needles", t / "needles",
                       "--canary", "300", t / "miss", f"e={t}/in.txt")
        assert rc == 1 and "canary line changed" in log, log
    print("sets self-test OK")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv and argv[0] == "run":
        return run(argv[1:])
    if argv and argv[0] == "pairs":
        return pairs(argv[1:])
    fail(__doc__.strip().split("\n\n")[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
