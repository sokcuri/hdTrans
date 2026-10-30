#!/usr/bin/env python3
"""LLM baseline: translate a frozen held-out set with document context (evaluation only).

  llm_translate.py --set EVALDIR/SET --out OUT.ko.txt [--model claude-opus-5-5] [--chunk 60]
                   [--cache DIR/llm-translate.jsonl] [--jobs 4]
  llm_translate.py --lines JA.txt --out OUT.ko.txt [...]   independent lines (e.g. the flip suite)

Each document is translated from its first line through its last scored line, in chunks of up to
--chunk non-empty lines. Every chunk sees the previous chunk's source lines and their translations as
context, so names, speech level and references carry across. The scored rows are then picked by
line_no. Chunk answers are cached by (model, prompt), so a rerun is free.
"""
import argparse
import csv
import os
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import api  # noqa: E402

csv.field_size_limit(1 << 30)

DOMAIN = {
    "fiction": "a chapter of a Japanese web novel (narration and dialogue)",
    "game": "a visual-novel game script (narration and dialogue; speaker names may precede lines)",
    "captions": "subtitles of an anime, drama, film or variety show (one subtitle per line)",
    "broadcast": "live TV captions, mostly news and information programmes (one caption unit per line; "
                 "a name in parentheses marks the speaker)",
    "web": "an encyclopedia article (headings, list items and sentences)",
}

RULES = """Translate the Japanese lines into Korean.
- Natural, fluent Korean that a skilled professional translator would publish, keeping the full meaning:
  no omissions, no additions, no explanations.
- Use the context to resolve names, pronouns, speech level and references; keep each character's
  speech level consistent (casual Japanese → 반말/해체, polite → 해요체/합니다체, narration → -다).
- Japanese person and place names are transliterated from their Japanese reading in standard Korean
  orthography; keep established Korean names for real places, people and works.
- Keep the quote brackets (「」『』), parentheses and the punctuation style of each line; keep symbols
  such as ≫, ♪, ―― and …… as they are.
- Return exactly one translation per numbered line, with the same number. Never merge or split lines."""

SCHEMA = {
    "type": "object",
    "properties": {"lines": {"type": "array", "items": {
        "type": "object", "properties": {"n": {"type": "integer"}, "ko": {"type": "string"}},
        "required": ["n", "ko"], "additionalProperties": False}}},
    "required": ["lines"], "additionalProperties": False,
}


def read_rows(set_dir):
    with open(os.path.join(set_dir, "lines.tsv"), encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def translate_doc(model, cache, domain, lines, last, chunk, effort):
    """lines: doc lines; translate indexes 0..last-1; returns {index: ko}."""
    todo = [i for i in range(last) if lines[i].strip()]
    out, prev = {}, []
    for c in range(0, len(todo), chunk):
        part = todo[c:c + chunk]
        ctx = "\n".join(f"{lines[i].strip()}\t→\t{out[i]}" for i in prev) or "(start of the document)"
        body = "\n".join(f"{n + 1}\t{lines[i].strip()}" for n, i in enumerate(part))
        prompt = (f"{RULES}\n\nThe text is {domain}.\n\nPrevious lines of the document, with the translation "
                  f"already given (context only, do not translate again):\n{ctx}\n\nLines to translate "
                  f"(number, tab, Japanese):\n{body}\n")
        k = api.key("llm-translate-v1", model, effort, prompt)
        got = cache.get(k)
        if got is None:
            def check(obj, size=len(part)):
                ns = [x["n"] for x in obj["lines"]]
                if ns != list(range(1, size + 1)):
                    raise ValueError(f"numbering {ns[:5]}… for {size} lines")
            obj = api.call(model, prompt, SCHEMA, effort=effort, validate=check)
            got = [x["ko"].replace("\n", " ").replace("\t", " ").strip() for x in obj["lines"]]
            cache.put(k, got)
        for i, ko in zip(part, got):
            out[i] = ko
        prev = part[-min(len(part), 30):]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--set")
    ap.add_argument("--lines")
    ap.add_argument("--out", required=True)
    ap.add_argument("--model", default="claude-opus-5-5")
    ap.add_argument("--effort", default="medium")
    ap.add_argument("--chunk", type=int, default=60)
    ap.add_argument("--cache", default="/Users/sokcuri/hdwork/v08/eval-runs/cache/llm-translate.jsonl")
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    cache = api.Cache(a.cache)
    if a.lines:
        with open(a.lines, encoding="utf-8") as f:
            src = f.read().rstrip("\n").split("\n")
        parts = [src[i:i + a.chunk] for i in range(0, len(src), a.chunk)]
        dom = "a list of unrelated short lines from novels, games and captions; each line stands alone"

        def one(part):
            res = translate_doc(a.model, cache, dom, part, len(part), a.chunk, a.effort)
            return [res.get(i, "") for i in range(len(part))]
        with ThreadPoolExecutor(a.jobs) as ex:
            out = [x for r in ex.map(one, parts) for x in r]
        with open(a.out, "w", encoding="utf-8") as f:
            f.write("\n".join(out) + "\n")
        print(f"{a.out}: {len(out)} lines ({a.model})")
        return
    name = os.path.basename(os.path.normpath(a.set))
    scored = [r for r in read_rows(a.set) if r["scored"] == "1"]
    last = {}
    for r in scored:
        last[r["doc_id"]] = max(last.get(r["doc_id"], 0), int(r["line_no"]))

    def run(doc_id):
        with open(os.path.join(a.set, "docs", doc_id + ".ja.txt"), encoding="utf-8") as f:
            lines = f.read().split("\n")
        return doc_id, translate_doc(a.model, cache, DOMAIN.get(name, "a Japanese text"), lines, last[doc_id],
                                     a.chunk, a.effort)

    with ThreadPoolExecutor(a.jobs) as ex:
        docs = dict(ex.map(run, list(last)))
    out = [docs[r["doc_id"]][int(r["line_no"]) - 1] for r in scored]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"{a.out}: {len(out)} lines ({a.model})")


if __name__ == "__main__":
    main()
