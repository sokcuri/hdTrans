# Skills

Working procedures for Claude Code, one folder per skill with a `SKILL.md` (YAML front matter
`name` and `description`, then the procedure). Claude Code reads project skills from
`.claude/skills/`, which is not tracked here; link it once per checkout or worktree:

```sh
mkdir -p .claude && ln -s ../skills .claude/skills
```

| Skill | Use |
|---|---|
| [fix-translation](fix-translation/SKILL.md) | Fix a wrong output at its cause, with cases, gate, measurement and judgment |
| [triage-issue](triage-issue/SKILL.md) | Reproduce a reported mistranslation on two releases, label, milestone, reply, branch |
| [integrate-branches](integrate-branches/SKILL.md) | Cherry-pick parallel fix branches into one, resolve tables and rule numbers, judge the combination |
| [release](release/SKILL.md) | Review gate, develop and main PRs, CI release steps, goldens, milestone |
| [probe-variations](probe-variations/SKILL.md) | Variation and near-miss sets for a fix, classified against the baseline |
| [review-code](review-code/SKILL.md) | Sanitizers, lifetimes, `-Wshadow-all`, threads and hygiene before a PR |

## Tools

| Tool | Does |
|---|---|
| `tools/gate.sh [NAME]` | Build and ctest (without `testdata`) with dicts/ and a fresh HOME; the critical, case and example tests must pass; prints `GATE OK` |
| `tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME` | Translate the private sets (lines, sample, test-data documents, web examples) at once, write the changed lines as judge pairs, print the sample chrF |
| `tools/audit.py draw RUN OUT \| rate LINES GRADES` | A fixed seeded sample of current outputs for grading, and its error rate per set (docs/testing.md) |
| `tools/chrf.py REF A [B]` | Corpus chrF, and changed, better and worse line counts between two outputs |
| `tools/count-class.py REGEX [FILE...]` | Lines and distinct keys that share a failure shape |
| `tools/verdicts.py PAIRS FIRST [SECOND]` | Judgment counts and the lines two judges both call worse |
| `tools/blind.py make \| disagree \| final` | Blind judge input (X/Y per line, private key), the lines two judges answer differently, and the verdicts mapped back |
| `tools/new-pairs.py OUT NEW [OLD...]` | Judge pairs not judged before with the same before and after (rebased PRs, cycle checks) |
| `tools/regen-rules.sh` | Rebuild, lint, regenerate `program.hash` and `compiled_gen.cpp`, rebuild |
| `tools/pick.sh COMMIT...` | Cherry-pick with table union, regeneration and checks; stops instead of dropping a commit |
| `tools/union-tsv.py FILE... \| --check [COMMIT]` | Resolve table conflicts by union; check tables for repeated rows, reused ids, contradicting cases and resurrected rows |
| `tools/rule-ids.py BASE REV...` | New rule numbers per branch and the ones two branches share |
| `tools/accept-cases.py REPORT FILE...` | Take the actual output of failing whole-translation case rows as expected |
| `tools/shadow.py [REF \| --all]` | clang `-Wshadow-all` on sources changed since the merge base; fails on new warnings only |
| `tools/hygiene.py BASE` | Markers, stray paths, absolute paths, instruction wording and private patterns in a change |
| `tools/surface-budget.py [--report]` | Optional report of the Japanese and Korean text in src/core: totals, the surface:code condition ratio and the files with the most literals; used in release reviews, not a check (docs/testing.md) |
| `tools/pass-mutation.py [-c CLI] [-d DICTS] [-j JOBS] [-t SECONDS] [PASS...]` | Leaves out each sentence pass in an experiment build and lists the passes no rule case or example catches (docs/testing.md) |
| `tools/with-timeout.sh SECONDS CMD...` | Time limit with `timeout`, `gtimeout` or a perl alarm |

ctest runs the `--self-test` of each Python tool (except `shadow.py`), `tests/pick.sh` and
`tests/measure.sh`. The
shell tools use the Ninja build folder `build/<preset>`.

## Environment

| Variable | Used by | Holds |
|---|---|---|
| `HDTRANS_WORK` | gate, measure, probe sets | Scratch output folder (default `build/work`); outputs of private texts stay there |
| `HDTRANS_CORPUS` | measure, audit, count-class | A private text file, one sentence per line, translated line by line |
| `HDTRANS_REF_SAMPLE` | measure, audit, count-class | A private folder with `ja.txt` and the line-aligned human translation `ref.txt` |
| `HDTRANS_TESTDATA` | measure, audit, `tools/e2e.sh`, `tools/e2e-pairs.py`, goldens | The test-data repository (docs/testing.md) |
| `HDTRANS_EXAMPLES` | measure, audit | The folder of web example texts that are not in `web/examples/` (the `HDTRANS_WEB_EXAMPLES` folder of the web build) |
| `HDTRANS_JOBS` | measure | Documents translated at a time (default: CPU count) |
| `HDTRANS_HYGIENE_PATTERNS` | hygiene | A file outside the repository with one regular expression per line for words that must not be committed |
| `HDTRANS_PRESET` | the build tools, `shadow.py` | CMake preset (default `mac` on macOS, `linux` elsewhere) |
| `HDTRANS_TIMEOUT` | gate, measure, pick | Seconds before a long step is stopped (default 1800) |
| `HDTRANS_CACHE_DIR=off` | every run | No dictionary cache shared between builds |
| `SDKROOT` | macOS builds | An installed SDK when the newest one fails to configure (docs/build.md) |

Unset private sets are skipped; nothing in the repository names or copies them.
