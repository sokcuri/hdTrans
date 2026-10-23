---
name: fix-translation
description: Fix a wrong Japanese→Korean output of the engine at its cause. Use when a line translates wrongly (from an issue, a review finding or a judged diff) and the fix goes into dicts/, the rules or engine code, with tests, gate, measurement and commit.
---

# Fix a translation

Read docs/fixing-translations.md and the spec/ file of the area first. The expected output is what
spec/ says (`NUM`, `LAY`, `LEX`, `SPC`, `SND` ids); a reviewer's suggested cause or wording is a
hypothesis until `--why` confirms it.

## Setup

- Work in your own worktree on a branch from `develop`. Never stash, reset or check out in a shared tree.
- `export HDTRANS_CACHE_DIR=off`; run engines and ctest with `HOME=$(mktemp -d)` when builds run in parallel.
- Pin a baseline: `git worktree add --detach "$BASE" <commit>` and build it there. Compare against that
  build, never against a moving `develop`.
- If several fixers work in parallel, use only your assigned rule-number and row-id ranges.
- Give every exploratory run a time limit. If a run is slow, shrink the input (half the lines, one
  kind of line) until the slow part is found. Never wait with blind sleeps.

## Steps

1. **Reproduce** on the current build:
   `echo '<ja>' | build/<preset>/hdtrans -dicts dicts -why` (add `--trace FILE` for every candidate).
2. **Find the first wrong stage**: segmentation, analysis (POS, conjugation), candidate selection,
   assembly (endings, quotes), spacing. Fix that stage. Never cover an upstream error downstream with
   a whole-phrase row or an output rewrite.
3. **Count the class** before editing: grep `$HDTRANS_CORPUS`, the sample `ja.txt` and the baseline
   outputs for the same failure shape with other words. About 5+ distinct words or 20+ lines makes it a
   class: fix the mechanism (codes, a rule over codes or classes, engine code), not one row per word.
4. **Pick the layer**: data (rows, `sem`/`gram` codes, senses) before rules, rules before engine
   code. Change engine code when rows would have to be listed in parallel, when a rule cannot see the
   information it needs, when a later stage overrides the decision, or when the analysis is wrong.
5. **Patch smell test** before keeping the change:
   - Does the condition list surface forms copied from the examples?
   - Does it still hold for another verb or noun of the same kind?
   - What else matches the condition, and is the output right there too?
   If any answer shows a patch, go one layer deeper.
6. **Do not change what is not wrong**:
   - Keep the register (해요체 vs 합니다체, strict vs soft speech) as it is.
   - Do not swap one acceptable wording for another.
   - Katakana loanwords stay loanwords written as pronounced (LEX-1, LEX-2).
   - Spacing after digits and Latin follows NUM-1..NUM-5. Runs of two or more spaces and tabs are
     column breaks (LAY-1).
   - Quoted speech follows the markers of the source (SPC-1); っす is polite speech.
   If two forms are both plausible, count how human translations render the context first and
   report the counts rather than picking one.
7. **Analyse before reverting.** If a change misbehaves, find the cause with `--why`/`--trace` first.
   Do not cycle through apply, revert and retry.
8. **Rules**: after editing a `ctx` column or `src/core/rules/globals.txt`, run `tools/regen-rules.sh`.
   Never hand-edit generated files (`*_gen.*`, `program.hash`).
9. **Tests**: every fix gets at least one `fix` and one `keep` row in `tests/rules/cases/<content-name>.tsv`.
   - The first line is `# 규칙: <ids>` (spec/).
   - Inputs are short excerpts of real lines, or the relevant part of a line.
   - Use neutral names, never a recognisable cast.
   - A widened rule needs keep rows on the other side of its condition.
   - Assert only what the fix decides (`rule:dict:<id>=<ko>`) unless the whole line is right.
   - Engine changes also get a unit test next to the stage (`tests/unit`, `hdtest.h`).
   - A change that contradicts a spec rule updates the rule and its cases in the same commit.
10. **Gate**: `tools/gate.sh` must print `GATE OK`. Act on its exit status; do not pipe it through `tail`.
    Rewrite expected outputs in tests/ or examples/ only for lines judged better:
    - `tools/rule-cases.sh CLI dicts CASES > report.txt`
    - read the report
    - `tools/accept-cases.py report.txt CASES`
11. **Measure** against the baseline build. Run `-c`/`-d` from the baseline worktree once and reuse
    that run:
    ```sh
    tools/measure.sh -c "$BASE/build/<preset>/hdtrans" -d "$BASE/dicts" base-<sha>
    tools/measure.sh -b base-<sha> <branch>
    ```
    For chapters in `$HDTRANS_TESTDATA`, also run `tools/e2e-pairs.py` (docs/testing.md).
12. **Judge every changed line** in `corpus.diff.tsv` and `sample.diff.tsv`. If there are more than
    about 400, judge a random 400 plus every line of a suspect type.
    - Write the verdicts (better, worse, same, with a note) to `$HDTRANS_WORK/<branch>/judged.tsv`.
    - Keep the change only if better:worse is at least 2:1 overall, no class of lines gets
      systematically worse, and the sample chrF is not lower than the baseline.
    - Narrow or drop the change behind lines judged worse.
13. **Code checks** for engine changes:
    - `tools/shadow.py` (clang `-Wshadow-all`; MSVC fails on the same names).
    - The review-code skill for views and lifetimes.
14. **Commit**: one coherent change, `fix(<scope>): <what changed in language terms>`. The body states
    - the stage where the error first appeared,
    - the size of the class (words or patterns),
    - why this layer was chosen.
    Do not name data sets, scores, corpora, line counts, sources, works or characters.

## Before committing

- `git status --short` shows only the files you meant. Look for stray files such as one named `--in`.
- `git diff --cached` has no conflict markers and no hand-edited generated files.
- Grep the diff and the message:
  `git diff --cached | grep -inE 'novel|소설|blog|블로그|review|리뷰|web|웹|public|공개|audit|user|사용자|request|요청|instruction|지시|prompt|프롬프트'`.
  Also grep for any title or character name. Read every hit and remove those that name where a case
  came from or that word an instruction. `web` (the web demo) and `user` (user dictionaries) are
  fine only as those features.
- Report: commits with before → after examples, gate result, chrF, changed/better/worse counts, what
  is left unfixed and why.
