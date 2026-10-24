#!/usr/bin/env python3
"""Judge changed Japanese/Korean line pairs through the Anthropic Messages API.

Usage: tools/judge.py MODEL OUT.tsv PAIRS.tsv [--only KEYS.txt] [--effort low|high] [--grade]
PAIRS.tsv has key, ja, old, new (no header); OUT.tsv appends key, verdict, model.
--grade grades the old column alone (correct, minor or wrong) instead of comparing it with new,
as tools/audit.py expects.
Completed keys for the same model are skipped on restart. Credentials stay in the
ANTHROPIC_AUTH_TOKEN (Bearer) or ANTHROPIC_API_KEY environment variable.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor


def schema(field, values):
    return {
        "type": "object",
        "properties": {"verdicts": {"type": "array", "items": {
            "type": "object",
            "properties": {"id": {"type": "integer"},
                           field: {"type": "string", "enum": list(values)}},
            "required": ["id", field], "additionalProperties": False}}},
        "required": ["verdicts"], "additionalProperties": False,
    }


COMPARE = ("better", ("X", "Y", "same"))
GRADE = ("grade", ("correct", "minor", "wrong"))


def ask(prompt, model, effort, url, headers, size, kind=COMPARE):
    field, values = kind
    body = json.dumps({
        "model": model, "max_tokens": 16000,
        "output_config": {"effort": effort,
                          "format": {"type": "json_schema", "schema": schema(field, values)}},
        "messages": [{"role": "user", "content": prompt}],
    }).encode()
    for attempt in range(6):
        try:
            req = urllib.request.Request(url, body, headers)
            with urllib.request.urlopen(req, timeout=600) as response:
                data = json.load(response)
            if data.get("stop_reason") == "max_tokens":
                raise ValueError("truncated judge response")
            text = "".join(c["text"] for c in data["content"] if c["type"] == "text")
            verdicts = json.loads(text)["verdicts"]
            ids = [v["id"] for v in verdicts]
            if (len(ids) != size or any(type(i) is not int for i in ids)
                    or set(ids) != set(range(size))
                    or any(v[field] not in values for v in verdicts)):
                raise ValueError("incomplete or invalid judge verdicts")
            return verdicts
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", metavar="MODEL")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("pairs", type=Path, metavar="PAIRS.tsv")
    parser.add_argument("--only", type=Path, metavar="KEYS.txt")
    parser.add_argument("--effort", choices=("low", "high"),
                        default=os.environ.get("JUDGE_EFFORT", "low"))
    parser.add_argument("--grade", action="store_true")
    args = parser.parse_args()
    if args.effort not in ("low", "high"):
        parser.error("JUDGE_EFFORT must be low or high")
    if args.out.resolve() == args.pairs.resolve():
        parser.error("OUT.tsv must differ from PAIRS.tsv")
    only = set(args.only.read_text(encoding="utf-8").splitlines()) if args.only else None
    done = set()
    if args.out.exists():
        with args.out.open(encoding="utf-8", newline="") as source:
            done = {r[0] for r in csv.reader(source, delimiter="\t")
                    if len(r) == 3 and r[2] == args.model}
    rows = []
    seen = set()
    with args.pairs.open(encoding="utf-8", newline="") as source:
        for row in csv.reader(source, delimiter="\t"):
            if len(row) != 4 or not row[0]:
                parser.error("PAIRS.tsv requires four columns: key, ja, old, new")
            key = row[0]
            if key in seen:
                parser.error(f"duplicate pair key: {key}")
            seen.add(key)
            if key not in done and (only is None or key in only):
                rows.append(row)
    print(f"{len(rows)} to judge", file=sys.stderr)
    if not rows:
        args.out.touch()
        return
    base = os.environ.get("ANTHROPIC_BASE_URL", "https://api.anthropic.com").rstrip("/")
    url = base + ("/messages" if base.endswith("/v1") else "/v1/messages")
    headers = {"content-type": "application/json", "anthropic-version": "2023-06-01"}
    token = os.environ.get("ANTHROPIC_AUTH_TOKEN")
    key = os.environ.get("ANTHROPIC_API_KEY")
    if token:
        headers["authorization"] = "Bearer " + token
    elif key:
        headers["x-api-key"] = key
    else:
        parser.error("set ANTHROPIC_AUTH_TOKEN or ANTHROPIC_API_KEY")
    lock = threading.Lock()

    def grade(batch):
        lines = [
            "Japanese lines, each out of its context, with a Korean machine translation. Grade each "
            "translation: correct when it is a faithful, natural Korean rendering a reader would "
            "accept as is; minor when the meaning is right but a particle, ending, spacing, word "
            "choice or register is visibly off; wrong when meaning is lost, changed or added, "
            "Japanese is left untranslated, or the Korean is garbled or ungrammatical. "
            "Loanword transliterations are acceptable, and a reading that could be right in some "
            "context is not an error.", "",
        ]
        for i, (_, ja, out, _) in enumerate(batch):
            lines += [f"[{i}] JA: {ja}", f"KO: {out}", ""]
        verdicts = ask("\n".join(lines), args.model, args.effort, url, headers, len(batch), GRADE)
        result = [(batch[v["id"]][0], v["grade"], args.model)
                  for v in sorted(verdicts, key=lambda v: v["id"])]
        with lock, args.out.open("a", encoding="utf-8", newline="") as output:
            csv.writer(output, delimiter="\t", lineterminator="\n").writerows(result)

    def run(batch):
        rnd = random.Random(hashlib.md5(batch[0][0].encode()).hexdigest())
        lines = [
            "Japanese lines with two Korean machine translations X and Y that differ only slightly. "
            "For each item, say which translation is better as a faithful, natural Korean rendering "
            "of the Japanese line (X, Y, or same when neither is better). "
            "Judge only the differing parts. Loanword transliterations are acceptable; "
            "do not penalize them just for being transliterations.", "",
        ]
        flips = []
        for i, (_, ja, old, new) in enumerate(batch):
            flip = rnd.random() < 0.5
            flips.append(flip)
            x, y = (new, old) if flip else (old, new)
            lines += [f"[{i}] JA: {ja}", f"X: {x}", f"Y: {y}", ""]
        verdicts = ask("\n".join(lines), args.model, args.effort, url, headers, len(batch))
        result = []
        for item in sorted(verdicts, key=lambda v: v["id"]):
            i, better = item["id"], item["better"]
            verdict = "same" if better == "same" else (
                "improved" if (better == "X") == flips[i] else "worse")
            result.append((batch[i][0], verdict, args.model))
        with lock, args.out.open("a", encoding="utf-8", newline="") as output:
            csv.writer(output, delimiter="\t", lineterminator="\n").writerows(result)

    batches = [rows[i:i + 10] for i in range(0, len(rows), 10)]
    with ThreadPoolExecutor(8) as executor:
        list(executor.map(grade if args.grade else run, batches))


if __name__ == "__main__":
    main()
