#!/usr/bin/env python3
"""Finds the linux build of a commit for tools/perf-budget.py --against and downloads it.

  tools/perf-base.py COMMIT DIR [--wait SECONDS]
  tools/perf-base.py --self-test

Looks for the artifact hdtrans-linux (hdtrans, dicts.hdsnap) of the Build workflow in the push runs
of COMMIT, then in the runs that recorded COMMIT's tree as tested (artifact tested-tree-<tree>). The
linux job uploads it while the rest of its run goes on, so while a push run of COMMIT is not finished
it looks again every POLL seconds, for at most --wait seconds (default 600). Needs gh with a token
that reads actions (GH_TOKEN) and GITHUB_REPOSITORY, and COMMIT in the local repository.
Exit: 0 downloaded into DIR, 1 no build found, 2 invalid arguments.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ARTIFACT = "hdtrans-linux"
FILES = ("hdtrans", "dicts.hdsnap")
POLL = 30
WORKFLOW = "build.yml"


def gh(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout


class GitHub:
    def __init__(self, repo):
        self.repo = repo

    def api(self, path):
        """The parsed answer, or None when the call failed."""
        status, out = gh("api", f"repos/{self.repo}/{path}")
        try:
            return json.loads(out) if status == 0 else None
        except ValueError:
            return None

    def push_runs(self, commit):
        """[(run id, completed)] of the Build push runs of commit; None when unknown."""
        runs = self.api(f"actions/workflows/{WORKFLOW}/runs?head_sha={commit}&event=push&per_page=20")
        return None if runs is None else [(r["id"], r.get("status") == "completed")
                                          for r in runs.get("workflow_runs", [])]

    def tested_runs(self, tree):
        found = self.api(f"actions/artifacts?name=tested-tree-{tree}&per_page=20") or {}
        return [a["workflow_run"]["id"] for a in found.get("artifacts", [])
                if not a.get("expired") and a.get("workflow_run", {}).get("id")]

    def has_build(self, run):
        found = self.api(f"actions/runs/{run}/artifacts?name={ARTIFACT}") or {}
        return any(not a.get("expired") for a in found.get("artifacts", []))

    def linux_done(self, run):
        """Whether the linux job of run finished; it uploads the build only when its tests pass."""
        found = self.api(f"actions/runs/{run}/jobs?per_page=100") or {}
        return any(j.get("name") == "linux" and j.get("status") == "completed" for j in found.get("jobs", []))

    def download(self, run, directory):
        status, _ = gh("run", "download", str(run), "-R", self.repo, "-n", ARTIFACT, "-D", str(directory))
        return status == 0


def tree_of(commit):
    r = subprocess.run(["git", "rev-parse", f"{commit}^{{tree}}"], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def fetch(hub, commit, tree, directory, wait, clock=time.monotonic, pause=time.sleep):
    """The run whose build was downloaded into directory, or None."""
    deadline = clock() + wait
    failed = set()
    while True:
        pushes = hub.push_runs(commit)
        candidates = [run for run, _ in pushes or []] + (hub.tested_runs(tree) if tree else [])
        for run in candidates:
            if run in failed or not hub.has_build(run):
                continue
            for _ in range(2):
                # gh run download does not overwrite files.
                shutil.rmtree(directory, ignore_errors=True)
                Path(directory).mkdir(parents=True)
                if hub.download(run, directory) and all((Path(directory) / name).is_file() for name in FILES):
                    (Path(directory) / "hdtrans").chmod(0o755)
                    return run
            failed.add(run)
        waiting = pushes is None or any(not done and run not in failed and
                                        not (hub.linux_done(run) and not hub.has_build(run)) for run, done in pushes)
        if not waiting or clock() + POLL > deadline:
            return None
        print("perf-base: waiting for the linux build of a push run in progress", flush=True)
        pause(POLL)


def self_test():
    class Fake:
        def __init__(self):
            self.pushes, self.tested, self.builds, self.downloads, self.finished = [], [], set(), [], set()

        def push_runs(self, commit):
            return self.pushes

        def tested_runs(self, tree):
            return self.tested

        def has_build(self, run):
            return run in self.builds

        def linux_done(self, run):
            return run in self.finished

        def download(self, run, directory):
            self.downloads.append(run)
            for name in FILES:
                (Path(directory) / name).write_text("x")
            return True

    now = [0.0]

    def clock():
        return now[0]

    def pause(seconds):
        now[0] += seconds
        hub.builds.add(1)  # the running push run uploads its build meanwhile

    stdout, sys.stdout = sys.stdout, open(os.devnull, "w")
    try:
        with tempfile.TemporaryDirectory() as tmp:
            hub = Fake()
            hub.pushes = [(1, True)]
            hub.builds = {1}
            assert fetch(hub, "c", "t", tmp, 600, clock, pause) == 1
            assert os.access(Path(tmp) / "hdtrans", os.X_OK)
            # The tree was tested by another run.
            hub = Fake()
            hub.tested, hub.builds = [7], {7}
            assert fetch(hub, "c", "t", tmp, 600, clock, pause) == 7
            # Nothing, and nothing running: no wait.
            hub, now[0] = Fake(), 0.0
            assert fetch(hub, "c", "t", tmp, 600, clock, pause) is None and now[0] == 0
            # A push run in progress uploads its build later.
            hub = Fake()
            hub.pushes = [(1, False)]
            assert fetch(hub, "c", "t", tmp, 600, clock, pause) == 1 and now[0] == POLL
            # The wait is bounded.
            hub, now[0] = Fake(), 0.0
            hub.pushes = [(2, False)]
            hub.has_build = lambda run: False
            assert fetch(hub, "c", "t", tmp, 100, clock, lambda s: now.__setitem__(0, now[0] + s)) is None
            assert now[0] <= 100
            # A linux job that finished without a build ends the wait.
            hub, now[0] = Fake(), 0.0
            hub.pushes, hub.finished = [(3, False)], {3}
            assert fetch(hub, "c", "t", tmp, 600, clock, lambda s: now.__setitem__(0, now[0] + s)) is None
            assert now[0] == 0
            # Unknown runs (a failed call) keep the wait going.
            hub, now[0] = Fake(), 0.0
            hub.push_runs = lambda commit: None
            assert fetch(hub, "c", "t", tmp, 100, clock, lambda s: now.__setitem__(0, now[0] + s)) is None
            assert now[0] > 0
            # A download that failed is tried once more; old files do not block it.
            hub = Fake()
            hub.pushes, hub.builds = [(1, True)], {1}
            (Path(tmp) / "stale").write_text("x")
            flaky = iter([False, True])
            real = hub.download
            hub.download = lambda run, d: next(flaky) and real(run, d)
            assert fetch(hub, "c", "t", tmp, 600, clock, lambda s: None) == 1
    finally:
        sys.stdout.close()
        sys.stdout = stdout
    print("self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    wait = 600
    if "--wait" in argv:
        i = argv.index("--wait")
        if i + 1 >= len(argv) or not argv[i + 1].isdigit():
            print(__doc__.strip(), file=sys.stderr)
            return 2
        wait = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if len(argv) != 2 or not repo:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    commit, directory = argv
    full = subprocess.run(["git", "rev-parse", "--verify", "-q", f"{commit}^{{commit}}"], capture_output=True, text=True)
    commit = full.stdout.strip() or commit
    Path(directory).mkdir(parents=True, exist_ok=True)
    run = fetch(GitHub(repo), commit, tree_of(commit), directory, wait)
    if run is None:
        print(f"perf-base: no linux build of {commit}")
        return 1
    print(f"perf-base: the linux build of {commit} from run {run}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
