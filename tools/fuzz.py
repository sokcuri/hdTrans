#!/usr/bin/env python3
"""Runs the libFuzzer target hdtrans_fuzz (tests/fuzz/translate.cpp) on a seed corpus made of the
repository's inputs, and prints every input that crashed, hung or ran out of memory.

  tools/fuzz.py FUZZER DICTS --replay [--out DIR]
  tools/fuzz.py FUZZER DICTS [--seconds N] [--timeout N|auto] [--seed N] [--jobs N] [--out DIR]
  tools/fuzz.py --seeds DIR      only write the seed corpus
  tools/fuzz.py --self-test

FUZZER is built with -DHDTRANS_FUZZ=ON (Clang, with the sanitizers of linux-asan). DICTS is a
dictionary folder or .hdsnap. Seeds: each example (examples/*.ja.txt without its first line), every
SEED_STEP-th input of the rule cases, the files of tests/fuzz/regressions/ and a few made-up texts
with long runs, whitespace runs, ruby markup and unbalanced brackets.
--replay translates every seed once, each within REPLAY_TIMEOUT seconds, and reports the slowest; the
result depends only on the tree. Without it the seeds are replayed the same way first, then --jobs
(default: the number of processors) fuzzers mutate them for --seconds (default 150), job i with the
random seed --seed + i (default 1), each into a corpus of its own; what they find depends on the
speed of the machine. An input slower than --timeout counts as a hang; auto (the default) is
SLOWEST_FACTOR times the slowest seed, at least MIN_TIMEOUT seconds.
The logs (replay.log, fuzz-<i>.log) and failing inputs are written to --out (default build/fuzz), and
the failing inputs are printed escaped and in hex. To keep one as a regression case, copy it into
tests/fuzz/regressions/ and add the fixed translation as a rule case.
Exit: 0 nothing found, 1 a failing input, 2 invalid arguments or a fuzzer that failed otherwise.
"""
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEED_STEP = 25
CASE_DIRS = ["tests/rules/cases", "tests/rules/speech-soft", "tests/rules/speech-strict"]
MAX_LEN = 4096
FAILURES = ("crash", "timeout", "oom", "leak")  # libFuzzer artifact prefixes
REPLAY_TIMEOUT = 60
SLOWEST_FACTOR = 10
MIN_TIMEOUT = 10
MADE_UP = [
    "あ" * 600,
    "ー" * 300 + "！",
    "！？" * 200,
    "……" * 150 + "。",
    "ドドドドドドドドドドドドドドドドドドドドドド",
    "名前" + " " * 40 + "数値" + "　" * 12 + "備考",
    "\t\t項目\t\t\t値\n\n\n\n",
    "｜漢字《かんじ》と｜文字（もじ）と山《やま》。",
    "<ruby>漢字<rt>かんじ</rt></ruby>を読む。",
    "「「「『（【",
    "】）』」」」",
    "１２３４５６７８９０ｋｍ、3.14、1,000,000円",
    "ｱｲｳｴｵｶﾞｷﾞｸﾞ ﾊﾟﾋﾟﾌﾟ",
    "\r\n\r\n\ufeff\u200b\ufffd\u3099",
]


def case_inputs():
    out = []
    for d in CASE_DIRS:
        for path in sorted((ROOT / d).glob("*.tsv")):
            header = None
            for line in path.read_text(encoding="utf-8").split("\n"):
                if not line or line.startswith("#"):
                    continue
                cells = line.split("\t")
                if header is None:
                    header = cells.index("input")
                elif header < len(cells):
                    out.append(cells[header])
    return out


def seeds():
    """The seed corpus: name -> bytes."""
    corpus = {}
    for path in sorted((ROOT / "examples").glob("*.ja.txt")):
        body = path.read_text(encoding="utf-8").partition("\n")[2]
        corpus[f"example-{path.name[:-len('.ja.txt')]}"] = body.encode("utf-8")[:MAX_LEN]
    for i, text in enumerate(case_inputs()[::SEED_STEP]):
        corpus[f"case-{i:04d}"] = text.encode("utf-8")
    for i, text in enumerate(MADE_UP):
        corpus[f"made-{i:02d}"] = text.encode("utf-8")
    regressions = ROOT / "tests" / "fuzz" / "regressions"
    if regressions.is_dir():
        for path in sorted(regressions.iterdir()):
            if path.is_file():
                corpus[f"regression-{path.name}"] = path.read_bytes()
    return corpus


def write_seeds(directory):
    directory.mkdir(parents=True, exist_ok=True)
    corpus = seeds()
    for name, data in corpus.items():
        (directory / name).write_bytes(data)
    return len(corpus)


def show(data):
    """An input as escaped text and as hex."""
    text = data.decode("utf-8", errors="backslashreplace")
    escaped = text.encode("unicode_escape").decode("ascii") if any(c in text for c in "\r\t\0") else text
    return escaped.replace("\n", "\\n"), data.hex()


def clean(out):
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*"):
        if old.is_file() and (old.name.split("-")[0] in FAILURES or old.suffix == ".log"):
            old.unlink()


def report(out, statuses):
    """Prints the failing inputs in out; 1 when there are any, 2 when a fuzzer failed without one."""
    found = sorted(p for p in out.glob("*") if p.is_file() and p.name.split("-")[0] in FAILURES)
    for path in found:
        text, hexed = show(path.read_bytes())
        print(f"fuzz: {path.name} ({path.stat().st_size} bytes)\n  text: {text}\n  hex:  {hexed}")
    if found or any(statuses):
        print(f"fuzz: failed; inputs and logs in {out}")
        return 1 if found else 2
    return 0


def replay(fuzzer, dicts, out, tmp):
    """Translates every seed once, each with REPLAY_TIMEOUT. (status, slowest seconds)."""
    seed_dir = Path(tmp) / "seeds"
    if not seed_dir.exists():
        write_seeds(seed_dir)
    files = sorted(seed_dir.iterdir())
    env = dict(os.environ, HDTRANS_FUZZ_DICTS=str(dicts), HDTRANS_CACHE_DIR="off", HOME=tmp)
    log = out / "replay.log"
    with open(log, "w") as f:
        status = subprocess.run([str(fuzzer), f"-timeout={REPLAY_TIMEOUT}", "-rss_limit_mb=4096",
                                 f"-artifact_prefix={out}/", *map(str, files)], env=env, cwd=tmp, stdout=f,
                                stderr=subprocess.STDOUT).returncode
    text = log.read_text(encoding="utf-8", errors="replace")
    times = {Path(m.group(1)).name: int(m.group(2)) for m in re.finditer(r"^Executed (\S+) in (\d+) ms$", text, re.M)}
    slowest = max(times.items(), key=lambda kv: kv[1], default=("-", 0))
    print(f"fuzz: replayed {len(times)} of {len(files)} seeds, slowest {slowest[0]} {slowest[1]} ms")
    if status:
        running = re.findall(r"^Running: (\S+)$", text, re.M)
        if running:
            print(f"fuzz: the seed {Path(running[-1]).name} failed")
        print("\n".join(text.split("\n")[-80:]))
    return status, slowest[1] / 1000


def run_replay(fuzzer, dicts, out):
    clean(out)
    with tempfile.TemporaryDirectory() as tmp:
        status, _ = replay(fuzzer, dicts, out, tmp)
    result = report(out, [status])
    if not result:
        print("fuzz: every seed passed")
    return result


def run(fuzzer, dicts, seconds, timeout, seed, jobs, out):
    clean(out)
    with tempfile.TemporaryDirectory() as tmp:
        status, slowest = replay(fuzzer, dicts, out, tmp)
        if status:
            return report(out, [status])
        if timeout is None:
            timeout = max(MIN_TIMEOUT, math.ceil(slowest * SLOWEST_FACTOR))
        seed_dir = Path(tmp) / "seeds"
        print(f"fuzz: {jobs} jobs of {seconds} s, {timeout} s per input, seeds {seed}..{seed + jobs - 1}", flush=True)
        env = dict(os.environ, HDTRANS_FUZZ_DICTS=str(dicts), HDTRANS_CACHE_DIR="off", HOME=tmp)
        procs = []
        for i in range(jobs):
            work = Path(tmp) / f"corpus-{i}"
            work.mkdir()
            cmd = [str(fuzzer), f"-max_total_time={seconds}", f"-timeout={timeout}", f"-seed={seed + i}",
                   f"-max_len={MAX_LEN}", "-rss_limit_mb=4096", "-print_final_stats=1", "-reload=0",
                   f"-artifact_prefix={out}/", str(work), str(seed_dir)]
            log = open(out / f"fuzz-{i}.log", "w")
            procs.append((subprocess.Popen(cmd, env=env, cwd=tmp, stdout=log, stderr=subprocess.STDOUT), log))
        statuses = []
        for proc, log in procs:
            statuses.append(proc.wait())
            log.close()
    for i, status in enumerate(statuses):
        lines = (out / f"fuzz-{i}.log").read_text(encoding="utf-8", errors="replace").split("\n")
        stats = dict(line[len("stat::"):].split(":", 1) for line in lines
                     if line.startswith("stat::") and ":" in line[len("stat::"):])
        print(f"fuzz: job {i} exit {status}, {stats.get('number_of_executed_units', '?').strip()} inputs, "
              f"peak {stats.get('peak_rss_mb', '?').strip()} MB")
        if status:
            print("\n".join(lines[-80:]))
    result = report(out, statuses)
    if not result:
        print("fuzz: nothing found")
    return result


def self_test():
    corpus = seeds()
    assert any(n.startswith("example-") for n in corpus) and any(n.startswith("case-") for n in corpus)
    assert all(len(d) <= MAX_LEN or n.startswith(("made-", "regression-")) for n, d in corpus.items())
    assert show("あ\n\t".encode("utf-8")) == ("\\u3042\\n\\t", "e381820a09")
    assert show(b"\xe3a\n") == ("\\xe3a\\n", "e3610a")
    with tempfile.TemporaryDirectory() as tmp:
        assert write_seeds(Path(tmp) / "s") == len(corpus)
    print("self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) == 2 and argv[0] == "--seeds":
        print(f"fuzz: wrote {write_seeds(Path(argv[1]))} seeds")
        return 0
    options = {"--seconds": "150", "--timeout": "auto", "--seed": "1", "--jobs": str(os.cpu_count() or 1),
               "--out": str(ROOT / "build" / "fuzz")}
    rest, replay_only = [], False
    i = 0
    while i < len(argv):
        if argv[i] in options and i + 1 < len(argv):
            options[argv[i]] = argv[i + 1]
            i += 2
        elif argv[i] == "--replay":
            replay_only = True
            i += 1
        else:
            rest.append(argv[i])
            i += 1
    numbers = ("--seconds", "--seed", "--jobs") + (() if options["--timeout"] == "auto" else ("--timeout",))
    if len(rest) != 2 or not all(options[k].isdigit() for k in numbers):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    fuzzer = Path(rest[0]).resolve()
    if not shutil.which(str(fuzzer)):
        print(f"fuzz: {fuzzer} is not an executable", file=sys.stderr)
        return 2
    dicts, out = Path(rest[1]).resolve(), Path(options["--out"]).resolve()
    if replay_only:
        return run_replay(fuzzer, dicts, out)
    timeout = None if options["--timeout"] == "auto" else int(options["--timeout"])
    return run(fuzzer, dicts, int(options["--seconds"]), timeout, int(options["--seed"]),
               max(1, int(options["--jobs"])), out)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
