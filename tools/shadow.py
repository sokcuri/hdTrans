#!/usr/bin/env python3
"""Compiles changed sources with clang -Wshadow-all, which reports what MSVC rejects in the
Windows builds (C4456, C4457, C4458: a local or parameter hiding another name).

  tools/shadow.py [REF]     sources under src/, include/ and tests/ changed since the merge base
                            with REF (default: origin/develop, else another remote's develop, else
                            develop), committed or not
  tools/shadow.py --all     every source of the build

Uses the include paths, defines and language flags of build/<preset>/compile_commands.json
(HDTRANS_PRESET, default mac on macOS, linux elsewhere) with -fsyntax-only; a changed header
checks every source. A warning on a line the change added or edited is new and fails; warnings
elsewhere are listed as existing (read them when a header changed: a renamed member can make an
unchanged line shadow it). Constructor parameters named like their member and lambda locals that
shadow uncaptured locals are not reported; MSVC accepts both. The compiler is CLANGXX, else the
build's compiler when it is clang, else clang++.
Exit: 0 no new warning, 1 new warnings, 2 no build, no compiler or no base.
"""
import collections
import concurrent.futures
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAGS = ["-fsyntax-only", "-Wshadow-all", "-Wno-shadow-field-in-constructor", "-Wno-shadow-uncaptured-local",
         "-Werror"]
WITH_VALUE = {"-I", "-D", "-U", "-isystem", "-iquote", "-isysroot", "-include", "-arch", "-target", "-idirafter"}
PREFIXES = ("-I", "-D", "-U", "-std=", "-isystem", "-iquote", "-isysroot", "--sysroot", "-stdlib=", "-target",
            "--target=", "-fno-exceptions", "-fno-rtti", "-fexceptions", "-funsigned-char", "-fsigned-char")
DIAG = re.compile(r"^(/[^:]+):(\d+):\d+: (?:error|warning): (.*)$", re.M)


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, cwd=ROOT)


def base_ref(ref):
    candidates = [ref] if ref else ["origin/develop"] + [
        r for r in git("for-each-ref", "--format=%(refname:short)", "refs/remotes/*/develop").stdout.split()
        if r != "origin/develop"] + ["develop"]
    for r in candidates:
        base = git("merge-base", "HEAD", r).stdout.strip()
        if base:
            return r, base
    return None, None


def changed_lines(base):
    """Changed files and, per file, the line numbers the change added or edited."""
    diff = git("diff", "-U0", base, "--", "src", "include", "tests").stdout
    files, lines, path = set(), collections.defaultdict(set), None
    for line in diff.split("\n"):
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
            if path:
                files.add(path)
        elif line.startswith("--- a/"):
            files.add(line[6:])
        elif path and line.startswith("@@"):
            m = re.search(r"\+(\d+)(?:,(\d+))?", line)
            start, n = int(m.group(1)), int(m.group(2) or 1)
            lines[path].update(range(start, start + n))
    return files, lines


def compile_args(args, cc):
    keep, it = [], iter(args[1:])
    for a in it:
        if a in WITH_VALUE:
            keep += [a, next(it, "")]
        elif a.startswith(PREFIXES):
            keep.append(a)
    return [cc, *keep]


def main(argv):
    preset = os.environ.get("HDTRANS_PRESET") or ("mac" if platform.system() == "Darwin" else "linux")
    db = ROOT / "build" / preset / "compile_commands.json"
    if not db.exists():
        print(f"no {db}; configure the preset first", file=sys.stderr)
        return 2
    entries = []
    for e in json.loads(db.read_text()):
        file = (Path(e["directory"]) / e["file"]).resolve()
        if file.is_relative_to(ROOT) and not file.is_relative_to(ROOT / "third_party"):
            entries.append((e, file))
    new_lines = None
    if argv[:1] != ["--all"]:
        ref, base = base_ref(argv[0] if argv else None)
        if not base:
            print("no base: pass a ref such as origin/develop", file=sys.stderr)
            return 2
        files, new_lines = changed_lines(base)
        if not any(f.endswith((".h", ".hpp", ".inc")) for f in files):
            entries = [(e, f) for e, f in entries if str(f.relative_to(ROOT)) in files]
        print(f"shadow: base {ref} ({base[:10]})")
    if not entries:
        print("shadow: warning: no sources to check (no source changed since the base?)")
        return 0
    compiler, clang, jobs = os.environ.get("CLANGXX"), {}, []
    for e, file in entries:
        args = e.get("arguments") or shlex.split(e["command"])
        if args[0] not in clang:
            version = subprocess.run([args[0], "--version"], capture_output=True, text=True).stdout
            clang[args[0]] = args[0] if "clang" in version else shutil.which("clang++")
        cc = compiler or clang[args[0]]
        if not cc:
            print("no clang++; set CLANGXX", file=sys.stderr)
            return 2
        jobs.append(([*compile_args(args, cc), *FLAGS, str(file)], e["directory"]))
    found = {}
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        run = lambda j: subprocess.run(j[0], cwd=j[1], capture_output=True, text=True)  # noqa: E731
        for r in pool.map(run, jobs):
            for path, line, text in DIAG.findall(r.stderr):
                rel = os.path.relpath(Path(path).resolve(), ROOT)
                found[(rel, int(line), text)] = None
    new = [k for k in found if new_lines is None or k[1] in new_lines.get(k[0], ())]
    old = [k for k in found if k not in new]
    for label, items in (("new", new), ("existing", old)):
        for rel, line, text in sorted(items):
            print(f"{label}: {rel}:{line}: {text}")
    print(f"shadow: {len(jobs)} sources, {len(new)} new, {len(old)} existing warnings")
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
