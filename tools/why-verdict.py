#!/usr/bin/env python3
"""Name the word decision that made each Korean line wrong, through JEV.

Usage: tools/why-verdict.py CLI DICTS CASES.tsv OUT.tsv [--resume]
CASES.tsv has a header row and the columns ja, out, span, and optionally verdict.
Rows whose verdict is ok are skipped. For each remaining line the tool reads the
word decisions of the engine's --json output and asks JEV which one is the
mistranslation, plus a none option for a line that is acceptable. JEV is a choice
model: it picks among the decisions the engine made, it never writes a translation.
OUT.tsv columns: ja, surface, chosen, source, pick, confidence, span, match. pick
is empty when none was chosen, and match is 1 when the picked surface occurs in
span. Picks below 0.6 confidence agree with a reshuffled rerun only three times in
four, so review them. --resume skips the lines already in OUT.tsv.
Credentials: JEV_KEY, else ~/.config/hdtrans/jev.key with the key on its first line.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request


API = "https://api.typesafe.ai/v1/systemone"
MAX_DECISIONS = 40
PARTICLES = {
    "は", "が", "を", "に", "へ", "と", "で", "の", "も", "や", "か",
    "ね", "よ", "な", "わ", "ぞ", "ぜ", "さ", "から", "まで", "より",
    "だけ", "しか", "ほど", "くらい", "ぐらい", "って", "でも", "など",
}
OUTPUT_FIELDS = ["ja", "surface", "chosen", "source", "pick", "confidence", "span", "match"]


def credential():
    if os.environ.get("JEV_KEY"):
        return os.environ["JEV_KEY"]
    config_dirs = []
    if os.environ.get("XDG_CONFIG_HOME"):
        config_dirs.append(Path(os.environ["XDG_CONFIG_HOME"]))
    config_dirs.append(Path.home() / ".config")
    for config_dir in config_dirs:
        path = config_dir / "hdtrans" / "jev.key"
        if path.is_file():
            lines = path.read_text(encoding="utf-8").splitlines()
            return lines[0].strip() if lines else ""
    return ""


def sentences(text):
    parts = re.split(r"(?<=[。！？!?])", text)
    return [part for part in parts if part.strip()] or [text]


def ask(payload, headers):
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    for attempt in range(6):
        try:
            request = urllib.request.Request(API, body, headers)
            with urllib.request.urlopen(request, timeout=900) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (408, 409, 429) and error.code < 500:
                raise RuntimeError(f"JEV request failed with HTTP {error.code}") from None
            if attempt == 5:
                raise RuntimeError(f"JEV request failed with HTTP {error.code}") from None
        except (OSError, ValueError, KeyError, TypeError) as error:
            if attempt == 5:
                raise RuntimeError(f"JEV request failed: {type(error).__name__}") from None
        print(f"retry {attempt + 1}/5", file=sys.stderr)
        time.sleep(5 << attempt)


def run_cli(cli, dicts, rows):
    sentence_rows = []
    for case_index, row in enumerate(rows):
        for sentence in sentences(row["ja"]):
            sentence_rows.append((case_index, sentence))
    if not sentence_rows:
        return []
    command = [str(cli), "-dicts", str(dicts), "--json", "--each"]
    result = subprocess.run(command, input="\n".join(text for _, text in sentence_rows) + "\n",
                            text=True, encoding="utf-8", capture_output=True)
    if result.returncode:
        raise RuntimeError(f"translation CLI failed (exit {result.returncode})")
    try:
        data = json.loads(result.stdout)
    except (ValueError, TypeError):
        raise RuntimeError("translation CLI returned invalid JSON") from None
    grouped = [[] for _ in sentence_rows]
    for decision in data.get("choices", []):
        line = decision.get("line")
        if type(line) is not int or not 0 <= line < len(grouped):
            raise RuntimeError("translation CLI returned an invalid line number")
        choice = decision.get("choice") or {}
        ending = decision.get("ending") or {}
        # Every chosen word is a candidate: a default dictionary reading can be
        # the wrong one (もや starting もやってくれ reads 안개). The source only
        # names the rule when one made the choice; many word choices have none.
        decision["source"] = choice.get("source") or ending.get("source") or ""
        if (decision.get("surface") in PARTICLES
                or len(decision.get("surface", "")) <= 1):
            continue
        grouped[line].append(decision)
    return [(sentence_rows[i][0], sentence_rows[i][1], choices[:MAX_DECISIONS])
            for i, choices in enumerate(grouped)]


def completed_rows(path):
    if not path.is_file():
        return set()
    with path.open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source, delimiter="\t")
        if not reader.fieldnames or not set(OUTPUT_FIELDS).issubset(reader.fieldnames):
            raise ValueError("OUT.tsv has an invalid header")
        return {(row["ja"], row["span"]) for row in reader}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cli", type=Path, metavar="CLI")
    parser.add_argument("dicts", type=Path, metavar="DICTS")
    parser.add_argument("cases", type=Path, metavar="CASES.tsv")
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.cases.resolve() == args.out.resolve():
        parser.error("OUT.tsv must differ from CASES.tsv")
    key = credential()
    if not key:
        parser.error("set JEV_KEY or put the key in $XDG_CONFIG_HOME/hdtrans/jev.key")
    if not args.cli.is_file():
        parser.error(f"no CLI at {args.cli}")
    if not args.dicts.is_dir():
        parser.error(f"no dictionary folder at {args.dicts}")

    with args.cases.open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source, delimiter="\t")
        required = {"ja", "out", "span"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            parser.error("CASES.tsv requires ja, out, and span columns")
        cases = []
        cases_read = 0
        for row in reader:
            cases_read += 1
            if None in row or any(row.get(field) is None for field in required):
                parser.error("CASES.tsv contains a malformed row")
            if "verdict" in reader.fieldnames and row.get("verdict", "").strip().lower() == "ok":
                continue
            cases.append({field: row[field] for field in ("ja", "out", "span")})

    done = completed_rows(args.out) if args.resume else set()
    indexed = [(i, case) for i, case in enumerate(cases) if (case["ja"], case["span"]) not in done]
    active_cases = [case for _, case in indexed]
    print(f"{cases_read} cases read; {len(active_cases)} to judge", file=sys.stderr)
    if not active_cases:
        return

    try:
        groups = run_cli(args.cli, args.dicts, active_cases)
    except (OSError, RuntimeError) as error:
        parser.error(str(error))
    headers = {"content-type": "application/json", "authorization": "Bearer " + key}
    rng = __import__("random").Random(1)
    questions = {}
    state_lines = []
    options_by_question = {}
    for number, (case_index, sentence, decisions) in enumerate(groups):
        if not decisions:
            continue
        case = active_cases[case_index]
        options = [dict(decision=d, label=str(i + 1)) for i, d in enumerate(decisions)]
        options.append({"decision": None, "label": "none"})
        rng.shuffle(options)
        question_id = f"i{number}"
        options_by_question[question_id] = (case_index, sentence, options)
        state_lines.extend([f"[{question_id}] Japanese: {sentence}",
                            f"[{question_id}] Korean translation: {case['out']}"])
        for option in options:
            decision = option["decision"]
            text = "none: no word decision is mistranslated" if decision is None else (
                f"{option['label']}: {decision['surface']} → {decision['choice'].get('text', '')}")
            state_lines.append(f"[{question_id}] {text}")
        questions[question_id] = {
            "type": "choice",
            "instructions": (f"For item [{question_id}], which listed word decision, if any, "
                             "is the mistranslation in the Korean line? Choose none if all listed decisions are correct."),
            "criteria": {option["label"]: "the mistranslated decision" if option["decision"]
                         else "none of the listed decisions is wrong" for option in options},
        }

    answers = {}
    if questions:
        payload = {"model": "jev-latest", "state": "\n".join(state_lines), "questions": questions}
        try:
            response = ask(payload, headers)
            answers = response["answers"]
            if not isinstance(answers, dict):
                raise RuntimeError("JEV returned invalid answers")
            for question_id, (_, _, options) in options_by_question.items():
                answer = answers.get(question_id)
                if not isinstance(answer, dict) or str(answer.get("choice")) not in {
                        option["label"] for option in options}:
                    raise RuntimeError("JEV returned an incomplete choice")
                confidence = float(answer["confidence"])
                if not 0 <= confidence <= 1:
                    raise RuntimeError("JEV returned invalid confidence")
        except (RuntimeError, KeyError, TypeError, ValueError) as error:
            parser.error(str(error))

    output_rows = []
    decisions_asked = 0
    blames = 0
    matches = 0
    low_confidence = 0
    for number, (case_index, sentence, decisions) in enumerate(groups):
        case = active_cases[case_index]
        span = case["span"]
        question_id = f"i{number}"
        if not decisions:
            output_rows.append([case["ja"], "", "", "", "", "", span, "" if not span else "0"])
            continue
        decisions_asked += len(decisions)
        answer = answers.get(question_id, {})
        label = str(answer.get("choice", ""))
        _, _, options = options_by_question[question_id]
        selected = next((item["decision"] for item in options if item["label"] == label), None)
        confidence = float(answer.get("confidence", 0))
        if confidence < 0.6:
            low_confidence += 1
        if selected:
            blames += 1
            surface = selected["surface"]
            match = int(bool(span and surface in span))
            matches += match
            output_rows.append([case["ja"], surface, selected["choice"].get("text", ""),
                                selected.get("source", ""), surface,
                                f"{confidence:.2f}", span, "" if not span else str(match)])
        else:
            output_rows.append([case["ja"], "", "", "", "", f"{confidence:.2f}", span,
                                "" if not span else "0"])

    mode = "a" if args.resume and args.out.exists() else "w"
    with args.out.open(mode, encoding="utf-8", newline="") as sink:
        writer = csv.writer(sink, delimiter="\t", lineterminator="\n")
        if mode == "w" or args.out.stat().st_size == 0:
            writer.writerow(OUTPUT_FIELDS)
        writer.writerows(output_rows)
    no_decision = sum(not decisions for _, _, decisions in groups)
    print(f"cases read: {cases_read}; decisions asked: {decisions_asked}; blames: {blames}; "
          f"span matches: {matches}; confidence below 0.6: {low_confidence}; "
          f"no decision: {no_decision}", file=sys.stderr)


if __name__ == "__main__":
    main()
