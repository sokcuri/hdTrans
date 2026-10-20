#!/usr/bin/env python3
"""Check dictionary rows against JEV, which decides what a word means.

Usage: tools/judge-dict.py DICTS OUT.tsv [--sample N] [--file NAME] [--resume]
                         [--progress FILE.json] [--jobs N]
JEV is a choice model, not a translator: it picks the correct reading from the
candidates it is given. Each sampled row is put in a three-way choice with two
same-pos decoys from the dictionary; a row whose Korean is not the highest
probability is written to OUT.tsv as key, ko, pos, pick, confidence, source.
Rows already in OUT.tsv for the same dictionaries are skipped on restart.
--progress writes a JSON report as the run goes (rows, rejected, rate, file) for
tools/audit-dashboard.py. --jobs sends batches in parallel; the default is one.
Credentials: JEV_KEY, else the first existing line of $HDTRANS_JEV_KEY_FILE,
$XDG_CONFIG_HOME/hdtrans/jev.key, or ~/.config/hdtrans/jev.key.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import random
import re
import sys
import threading
import time
import urllib.error
import urllib.request

API = "https://api.typesafe.ai/v1/systemone"
BATCH = 20
# Complete words only: a stem with a conjugation marker cannot be judged apart
# from the endings it takes, and an ending row is not a word.
JUDGEABLE_POS = "NIKA"


def credential():
    if os.environ.get("JEV_KEY"):
        return os.environ["JEV_KEY"]
    for path in (os.environ.get("HDTRANS_JEV_KEY_FILE"),
                 Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "hdtrans" / "jev.key"):
        if path and Path(path).is_file():
            return Path(path).read_text(encoding="utf-8").strip()
    return ""


def read_rows(directory, only):
    rows = []
    for name in sorted(only or [p.name for p in Path(directory).glob("*.tsv")]):
        path = Path(directory) / name
        if not path.is_file():
            raise SystemExit(f"no dictionary file {name}")
        with path.open(encoding="utf-8") as source:
            for number, line in enumerate(source, 1):
                if line.startswith("#") or not line.strip():
                    continue
                f = line.rstrip("\n").split("\t")
                if f[0] == "key" or len(f) < 4:
                    continue
                yield {"key": f[0], "ko": f[1], "pos": f[2], "source": f"{name}:{number}"}


def judgeable(rows):
    """Complete Korean words that a meaning question can settle."""
    for r in rows:
        if r["pos"][:1] not in JUDGEABLE_POS:
            continue
        if "&" in r["key"] or "&" in r["ko"]:
            continue
        if not re.fullmatch(r"[가-힣]{2,6}", r["ko"]):
            continue
        yield r


def ask(payload, headers):
    body = json.dumps(payload, ensure_ascii=False).encode()
    for attempt in range(6):
        try:
            request = urllib.request.Request(API, body, headers)
            with urllib.request.urlopen(request, timeout=900) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (408, 409, 429) and error.code < 500:
                raise
            if attempt == 5:
                raise
        except (OSError, ValueError, KeyError, TypeError):
            if attempt == 5:
                raise
        print(f"retry {attempt + 1}/5", file=sys.stderr)
        time.sleep(5 << attempt)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dicts", metavar="DICTS", help="dictionary folder")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--sample", type=int, metavar="N", help="check N random rows instead of all")
    parser.add_argument("--file", action="append", metavar="NAME", help="read only this file (repeatable)")
    parser.add_argument("--resume", action="store_true", help="keep the rows already in OUT.tsv")
    parser.add_argument("--progress", type=Path, metavar="FILE.json",
                        help="write the run's progress here for tools/audit-dashboard.py")
    parser.add_argument("--jobs", type=int, default=1, metavar="N", help="batches to send in parallel")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least one")
    key = credential()
    if not key:
        parser.error("set JEV_KEY or put the key in ~/.config/hdtrans/jev.key")
    if not Path(args.dicts).is_dir():
        parser.error(f"no dictionary folder at {args.dicts}")

    rows = list(judgeable(read_rows(args.dicts, args.file)))
    if not rows:
        parser.error("no judgeable rows")
    if args.sample:
        rows = random.Random(1).sample(rows, min(args.sample, len(rows)))
    done = set()
    if args.resume and args.out.is_file():
        with args.out.open(encoding="utf-8", newline="") as source:
            done = {r[0] for r in csv.reader(source, delimiter="\t") if r}
    rows = [r for r in rows if r["key"] not in done]
    print(f"{len(rows)} rows to check", file=sys.stderr)

    decoys = [r["ko"] for r in judgeable(read_rows(args.dicts, args.file))]
    headers = {"content-type": "application/json", "authorization": "Bearer " + key}
    started = time.monotonic()
    doneRun = 0
    picked = 0
    lock = threading.Lock()

    def report(finished):
        if args.progress is None:
            return
        elapsed = max(time.monotonic() - started, 0.001)
        state = {"total": len(rows), "done": finished, "rejected": picked, "elapsed": round(elapsed, 1),
                 "rate": round(finished / elapsed, 2),
                 "eta": round((len(rows) - finished) / (finished / elapsed), 0) if finished else None,
                 "updated": time.strftime("%Y-%m-%dT%H:%M:%S")}
        temporary = args.progress.with_suffix(".tmp")
        temporary.write_text(json.dumps(state), encoding="utf-8")
        os.replace(temporary, args.progress)

    mode = "a" if args.resume else "w"
    sink = args.out.open(mode, encoding="utf-8", newline="")
    writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
    if mode == "w":
        writer.writerow(["key", "ko", "pos", "pick", "confidence", "source"])
        sink.flush()
    report(0)

    # A row is reported only when two runs with different random decoys both reject
    # it: one run rejects correct rows too when a decoy happens to fit better.
    def judge(start):
        chunk = rows[start:start + BATCH]
        rejected = []
        for attempt in range(2):
            rng = random.Random(start * 2 + attempt)
            state, questions, options = [], {}, {}
            for i, row in enumerate(chunk):
                chosen = set()
                while len(chosen) < 2:
                    candidate = rng.choice(decoys)
                    if candidate != row["ko"]:
                        chosen.add(candidate)
                labels = ["A", "B", "C"]
                values = [row["ko"], *sorted(chosen)]
                rng.shuffle(values)
                options[i] = dict(zip(labels, values))
                state.append(f"[{i}] 日本語: {row['key']}")
                state.extend(f"[{i}] {label}: {options[i][label]}" for label in labels)
                questions[f"i{i}"] = {
                    "type": "choice",
                    "instructions": f"Item [{i}]: Which Korean word is the correct translation of the Japanese word?",
                    "criteria": {label: "a Korean word" for label in labels}}
            answer = ask({"model": "jev-latest", "state": "\n".join(state), "questions": questions}, headers)
            run = {}
            for i, row in enumerate(chunk):
                got = answer["answers"][f"i{i}"]
                if options[i][got["choice"]] != row["ko"]:
                    run[row["key"]] = (options[i][got["choice"]], got["confidence"])
            rejected.append(run)
        found = []
        for row in chunk:
            first, second = rejected[0].get(row["key"]), rejected[1].get(row["key"])
            if first is None or second is None:
                continue
            found.append({**row, "pick": first[0], "confidence": min(first[1], second[1])})
        return found

    starts = list(range(0, len(rows), BATCH))
    if args.jobs == 1:
        for start in starts:
            found = judge(start)
            with lock:
                for row in found:
                    writer.writerow([row["key"], row["ko"], row["pos"], row["pick"],
                                     f"{row['confidence']:.2f}", row["source"]])
                sink.flush()
                picked += len(found)
                doneRun = min(start + BATCH, len(rows))
                report(doneRun)
            print(f"  {doneRun}/{len(rows)}", file=sys.stderr)
    else:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for found in pool.map(judge, starts):
                with lock:
                    for row in found:
                        writer.writerow([row["key"], row["ko"], row["pos"], row["pick"],
                                         f"{row['confidence']:.2f}", row["source"]])
                    sink.flush()
                    picked += len(found)
                    doneRun = min(doneRun + BATCH, len(rows))
                    report(doneRun)
                print(f"  {doneRun}/{len(rows)}", file=sys.stderr)
    sink.close()
    print(f"{picked} of {len(rows)} rows rejected in both runs; wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
