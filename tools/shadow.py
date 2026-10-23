#!/usr/bin/env python3
"""Compiles changed sources with clang -Wshadow-all, which reports what MSVC rejects in the
Windows builds (C4456, C4457, C4458: a local or parameter hiding another name).

  tools/shadow.py [REF]     sources under src/ and tests/ changed since REF (default: the upstream
                            branch, else develop) and in the working tree
  tools/shadow.py --all     every source of the build

Uses the flags in build/<preset>/compile_commands.json (HDTRANS_PRESET, default mac on macOS,
linux elsewhere) with -fsyntax-only; a changed header checks every source. The compiler is
CLANGXX, else the one in the build when it is clang, else clang++.
Exit: 0 clean, 1 warnings, 2 no build or no compiler.
"""
import concurrent.futures
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# A constructor parameter named like the member it initialises is not a C4458.
FLAGS = ["-fsyntax-only", "-Wshadow-all", "-Wno-shadow-field-in-constructor", "-Werror"]


def changed(ref):
    out = subprocess.run(["git", "diff", "--name-only", f"{ref}...HEAD", "--", "src", "include", "tests"],
                         capture_output=True, text=True, check=True, cwd=ROOT).stdout.split()
    out += subprocess.run(["git", "diff", "--name-only", "HEAD", "--", "src", "include", "tests"],
                          capture_output=True, text=True, check=True, cwd=ROOT).stdout.split()
    return set(out)


def main(argv):
    preset = os.environ.get("HDTRANS_PRESET") or ("mac" if platform.system() == "Darwin" else "linux")
    db = ROOT / "build" / preset / "compile_commands.json"
    if not db.exists():
        print(f"no {db}; configure the preset first", file=sys.stderr)
        return 2
    entries = [e for e in json.loads(db.read_text()) if not Path(e["file"]).is_relative_to(ROOT / "third_party")
               and Path(e["file"]).is_relative_to(ROOT)]
    if argv[:1] != ["--all"]:
        upstream = subprocess.run(["git", "rev-parse", "--abbrev-ref", "@{upstream}"], capture_output=True,
                                  text=True, cwd=ROOT).stdout.strip()
        files = changed(argv[0] if argv else upstream or "develop")
        if not any(f.endswith((".h", ".hpp", ".inc")) for f in files):
            entries = [e for e in entries if str(Path(e["file"]).relative_to(ROOT)) in files]
    compiler, clang = os.environ.get("CLANGXX"), {}
    jobs = []
    for e in entries:
        args = e.get("arguments") or shlex.split(e["command"])
        if args[0] not in clang:
            version = subprocess.run([args[0], "--version"], capture_output=True, text=True).stdout
            clang[args[0]] = args[0] if "clang" in version else shutil.which("clang++")
        cc = compiler or clang[args[0]]
        if not cc:
            print("no clang++; set CLANGXX", file=sys.stderr)
            return 2
        keep, skip = [], False
        for a in args[1:]:
            if skip:
                skip = False
            elif a == "-o":
                skip = True
            elif a != "-c" and not a.startswith("-W"):
                keep.append(a)
        jobs.append(([cc, *keep, *FLAGS], e["directory"]))
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        run = lambda j: subprocess.run(j[0], cwd=j[1], capture_output=True, text=True)  # noqa: E731
        for r in pool.map(run, jobs):
            if r.returncode:
                bad += 1
                print(r.stderr.strip())
    print(f"shadow: {len(jobs)} sources, {bad} with warnings")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
