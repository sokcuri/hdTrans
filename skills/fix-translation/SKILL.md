---
name: fix-translation
description: Fix a wrong Japanese→Korean output of the engine at its cause. Use when a line translates wrongly (from an issue, a review finding or a judged diff) and the fix goes into dicts/, the rules or engine code, with tests, gate, measurement, judgment and commit.
---

# Fix a translation

Read docs/fixing-translations.md and the spec/ file of the area first. The expected output is what
spec/ says (`NUM`, `LAY`, `LEX`, `SPC`, `SND` ids). A reviewer's suggested cause or wording is a
hypothesis until `-why` confirms it. `<preset>` is `mac` or `linux`, and `CLI` is `build/<preset>/hdtrans`.

## Setup

1. **Worktree.** Work in your own worktree on a branch from `develop`. Never stash, reset or check
   out in a shared tree.
2. **Environment.**
   - `export HDTRANS_CACHE_DIR=off`.
   - Run engines and ctest with `HOME=$(mktemp -d)` when builds run in parallel.
3. **Build.** `cmake --preset <preset> && cmake --build --preset <preset> --parallel`.
   - A fresh macOS configure can fail on the newest SDK. Then set `SDKROOT` to an installed older
     SDK and configure again (docs/build.md, macOS).
4. **Baseline.** Pin the commit you branched from and build it in a sibling worktree:
   ```sh
   BASE=$(dirname "$PWD")/$(basename "$PWD")-base
   git worktree add --detach "$BASE" <commit>
   (cd "$BASE" && cmake --preset <preset> && cmake --build --preset <preset> --parallel)
   ```
   Measure it once now; steps 3 and 11 reuse the run:
   `tools/measure.sh -c "$BASE/build/<preset>/hdtrans" -d "$BASE/dicts" base-<short sha>`.
   Compare against this build, never against a moving `develop`.
5. **Parallel fixers.** If several fixers work at once, use only your assigned rule-number and
   row-id ranges.
6. **Time limits.** Run anything that may hang under `tools/with-timeout.sh SECONDS CMD...`. If a
   run is slow, halve the input or keep one kind of line until the slow part shows. Do not wait
   with blind sleeps.

## Steps

1. **Reproduce** with `printf '%s\n' '<ja>' | CLI -dicts dicts -why`. Add `--trace FILE` to see
   every candidate.
2. **Find the first wrong stage.** The stages run in this order. The name in brackets is the
   layer that the end of a `WHY` line shows (docs/pipeline-layers.md):

   | Stage | What it does | `-why` layer |
   |---|---|---|
   | normalize | line text before anything else | not shown |
   | sound, protect | kana sounds, `phrases.tsv`, `sound.tsv`, `masked.tsv` and spans that are already Korean | `fixed` |
   | lookup, constraints | segmentation and candidates; `kRules` removes candidates and adds name items | not shown (added names show as `fixed`) |
   | word selection | local rules, global stem rules, defaults | `local`, `global-stem`, `defaults` |
   | endings | the same three for the ending | `local-ending`, `global-ending`, `ending-defaults` |
   | late rules and senses | priority 1–999 rules, `senses.tsv`, scorers | `global-last`, `senses`, `scorers` |
   | rewrites | about 35 sentence passes, quotes, conjunctions, `patterns.tsv` | `rewrites` |
   | compose, cleanup, restore | particle forms, conjugation, spacing, `cleanup.tsv` | not shown; use `-stages DIR` |

   Fix the first stage that goes wrong. Never cover an upstream error downstream with a
   whole-phrase row or an output rewrite.
3. **Count the class** before editing.
   - On the source: `tools/count-class.py '<regex with (?P<key>...)>'` gives matching lines and
     distinct keys in `$HDTRANS_CORPUS` and the sample.
   - On the outputs: grep the baseline run's `corpus.txt` and `sample.txt` (setup step 4).
   - Distinct words matter more than lines. Roughly 5+ distinct words or 20+ lines makes a class.
     Several keys of the same shape that one mechanism would cover also make a class, even with
     few lines.
   - For a class, fix the mechanism (codes, a rule over codes or classes, engine code), not one
     row per word.
4. **Pick the layer.**
   - For the same cause, data comes first (rows, `sem`/`gram` codes, senses), then rules, then
     engine code.
   - A word whose Korean depends on the words around it after selection (a particle read by its
     neighbours) is a row of `src/core/dict/builtin/constructions.tsv`, not a new C++ pass
     (docs/rule-language.md, 구문 표).
   - Change engine code when rows would have to be listed in parallel, when a rule cannot see the
     information it needs, when a later stage overrides the decision, or when the analysis itself
     is wrong.
   - A genuinely lexical problem (a missing word, a wrong gloss, a fixed idiom) stays one row.
5. **Patch smell test** before keeping the change:
   - Does the condition list surface forms copied from the examples?
   - Does it still hold for another verb or noun of the same kind?
   - What else matches the condition, and is the output right there too?

   If any answer shows a patch, go one layer deeper.
6. **Leave alone what is not wrong.**
   - Keep the register (해요체 vs 합니다체, strict vs soft speech) as it is.
   - Do not swap one acceptable wording for another.
   - Follow spec LEX-1 and LEX-2 for loanwords, NUM for digits and Latin, LAY-1 for whitespace
     columns, SPC-1 for quoted speech.
   - If two forms are plausible, count how human translations render the context and report the
     counts instead of picking one.
7. **Analyse before reverting.** When a change misbehaves, find the cause with `-why`/`--trace`
   first. Do not cycle through apply, revert and retry.
8. **Rules.** After editing a `ctx` column or `src/core/rules/globals.txt`, run
   `tools/regen-rules.sh`. Never hand-edit generated files (`*_gen.*`, `program.hash`).
9. **Tests.** Every fix gets at least one `fix` and one `keep` row.
   - **Where.** Default speech goes in `tests/rules/cases/<content-name>.tsv`. Lines that depend on
     the speech level also go in `tests/rules/speech-soft/` or `tests/rules/speech-strict/`.
   - **First line.** `# 규칙: <spec ids>`. Columns are `rule input expected kind layer` (header in
     tools/rule-cases.sh):
     - `rule`: `dict:<row id>` for a dictionary row's local rule, `L<n>`, `G<prio>`,
       `cx:<family>:<prio>` for a construction row, or `<stage>:<name>` for other changes
       (`sound:`, `constraint:`, `rewrite:`, `compose:`, `normalize:`, `sense:`).
     - `expected`: the whole output; `rule:dict:<row id>=<ko>` (the rule that row carries chose
       `<ko>`); `rule:L<n>=<ko>`; `rule:cx:<family>:<prio>=<ko>`; or `!rule:dict:<row id>` and
       `!rule:cx:<family>:<prio>` (it chooses nothing). Assert only what the fix decides unless the
       whole line is right. Every construction row needs a `fix` and a `keep` case (ctest
       `rules.constructions`).
     - `kind`: `fix`, `keep` or `current`.
     - `layer`: the docs/pipeline-layers.md name of the layer that decides the line.
   - **Inputs.** Short excerpts of real lines or the relevant part of a line. Use neutral names.
   - **Coverage.** A widened rule needs keep rows on the other side of its condition. Engine
     changes also get a unit test next to the stage (`tests/unit`, `hdtest.h`).
   - **Spec.** A change that contradicts a spec rule updates the rule and its cases in the same
     commit.
   - **Run.** `tools/rule-cases.sh CLI dicts tests/rules/cases`, then the same with
     `tests/rules/speech-soft -- --speech soft` and with `tests/rules/speech-strict -- --speech strict`.
10. **Gate.** `tools/gate.sh` must print `GATE OK`. Act on its exit status; never pipe it through
    `tail`. It skips the `testdata` comparison, which is a report.
    - Expected outputs in tests/ or examples/ change only for lines judged better.
    - Use `tools/rule-cases.sh CLI dicts CASES > report.txt`, read it, then
      `tools/accept-cases.py report.txt CASES`, which rewrites only rows whose expectation is a
      whole translation. Fix `rule:` rows by hand.
    - For examples, use `tools/examples.sh CLI dicts --accept`.
11. **Measure.** Do not rebuild during a run (measure.sh copies the CLI it uses into the run).
    Runs go to `$HDTRANS_WORK/<name>`, and a `/` in the name becomes `-`.
    - `tools/measure.sh -b base-<short sha> <branch>` translates every set whose variable is set
      (`HDTRANS_CORPUS`, `HDTRANS_REF_SAMPLE`, `HDTRANS_TESTDATA`, `HDTRANS_EXAMPLES`), writes
      `corpus.pairs.tsv`, `sample.pairs.tsv`, `docs.pairs.tsv` and `examples.pairs.tsv`, and prints
      the changed lines per set and sample chrF before → after. Measure the baseline with the same
      variables, or its run lacks the sets.
    - The folder in a `docs:<folder>/<name>:<line>` key tells the chapters you looked at while fixing
      (dev set) from the ones you did not (held-out set); report them separately. Other chapter
      folders go through `tools/e2e-pairs.py` (docs/testing.md).
12. **Judge every changed line.** Do not sample. You wrote the fix, so you do not judge it, and
    no judge learns which side is new (docs/testing.md "변경 판정").
    - With API access to both models, `tools/judge.py MODEL OUT.tsv PAIRS.tsv [--only KEYS]
      [--effort high]` asks blind (X/Y shuffled per line): the first model reads every pair, the
      second re-reads the lines the first called worse or same.
    - Otherwise judge blind with sub-agents (docs/testing.md "블라인드 판정"):
      1. Give each judge a fresh folder outside the run (`mktemp -d`). `tools/blind.py make PAIRS.tsv
         KEY.tsv J1/blind.tsv` writes the blind file (`id ja X Y`) there and the key next to the pairs;
         copy the blind file into J2.
      2. Two sub-agents that did not take part in the fix each read only their `blind.tsv` and write
         `answers.tsv` (`id X|Y|same [note]`) in their folder. Give them that path and the criteria
         of `tools/judge.py` (faithful, natural Korean; judge only the differing parts; loanword
         transliterations are fine). Tell them not to read the key, the pairs, the run folder, the
         repository or the other judge's folder, and not to run the engine.
      3. `tools/blind.py disagree J1/blind.tsv J1/answers.tsv J2/answers.tsv J3/blind.tsv`, then a
         third such sub-agent reads only `J3/blind.tsv` and writes `J3/answers.tsv`. When that file
         is empty, the two agreed everywhere; leave the third answers empty.
      4. `tools/blind.py final KEY.tsv J1/answers.tsv J2/answers.tsv J3/answers.tsv > FINAL.tsv`;
         a `worse` there is a line two judges called worse. Keep the answers with the run.
    - For large sets, split the pairs (`split -l 500 PAIRS.tsv part.`) across parallel judges and
      concatenate their outputs.
    - Verdict files have the columns `key verdict judge [note]`, with verdict `improved`, `same` or `worse`.
    - `tools/verdicts.py PAIRS FIRST [SECOND]` prints changed, improved, same, worse and every key
      both judges call worse; for blind judging it is `tools/verdicts.py PAIRS FINAL.tsv FINAL.tsv`,
      and every `worse` is listed as both-worse.
    - A line both judges call worse means reverting or narrowing the change.
    - Keep the change only if improved:worse is at least 2:1, no class of lines gets worse as a
      group, and the sample chrF is not lower than the baseline.
13. **Code checks** when C++ changed.
    - `tools/shadow.py` checks the sources changed since the merge base with develop. Only
      warnings on lines you added or edited fail. Read the existing ones it lists when you changed
      a header.
    - Also recommended: build the sanitizer configuration (review-code skill, section 2) and run
      `tools/rule-cases.sh build/<asan>/hdtrans dicts tests/rules/cases` and
      `tools/examples.sh build/<asan>/hdtrans dicts` on it.
14. **Commit.** One coherent change: `fix(<scope>): <what changed in language terms>`. The body states:
    - the stage where the error first appeared;
    - the size of the class (distinct words, patterns);
    - why this layer;
    - the judgment numbers (changed, improved, same, worse);
    - whether the expectation sets moved.

    Do not name data sets, sources, works or characters.

## Before committing

- `git status --short` shows only the files you meant. Watch for stray files such as one named `--in`.
- `tools/hygiene.py <base>` reports markers, stray paths, absolute paths, the middle dot, wording
  that records an instruction, and the private patterns in `$HDTRANS_HYGIENE_PATTERNS`. Read every
  hit and remove the real ones.
- Report: commits with before → after examples, gate result, chrF, judgment numbers, and what is
  left unfixed and why.
