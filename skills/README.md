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
| `tools/gate.sh [NAME]` | Build and ctest with dicts/, fresh HOME, no cache; requires the case and example tests to pass; prints `GATE OK` |
| `tools/measure.sh [-c CLI] [-d DICTS] [-b BASE] NAME` | Translate the private sets, diff against an earlier run, chrF of the sample |
| `tools/chrf.py REF A [B]` | Corpus chrF, and changed, better and worse line counts between two outputs |
| `tools/regen-rules.sh` | Rebuild, lint, regenerate `program.hash` and `compiled_gen.cpp`, rebuild |
| `tools/pick.sh COMMIT...` | Cherry-pick with table union, regeneration and checks |
| `tools/union-tsv.py FILE... \| --check [COMMIT]` | Resolve table conflicts by union; check tables for repeated rows, id collisions, contradicting cases and resurrected rows |
| `tools/rule-ids.py BASE REV...` | New rule numbers per branch and the ones two branches share |
| `tools/accept-cases.py REPORT FILE...` | Take the actual output of failing case rows as expected |
| `tools/shadow.py [REF \| --all]` | clang `-Wshadow-all` on changed sources |

The Python tools take `--self-test` (except `shadow.py`). The shell tools use `HDTRANS_PRESET`
(default `mac` on macOS, `linux` elsewhere) and the Ninja build folder `build/<preset>`.

## Environment

| Variable | Used by | Holds |
|---|---|---|
| `HDTRANS_WORK` | gate, measure, probe sets | Scratch output folder (default `build/work`). Outputs of private texts stay there. |
| `HDTRANS_CORPUS` | measure, fix and probe greps | A private text file, one sentence per line, translated line by line |
| `HDTRANS_GAME_SAMPLE` | measure | A private folder with `ja.txt` and the line-aligned human reference `ref.txt` |
| `HDTRANS_TESTDATA` | `tools/e2e.sh`, `tools/e2e-pairs.py`, goldens | The test-data repository (docs/testing.md) |
| `HDTRANS_PRESET` | the shell tools, `shadow.py` | CMake preset to build and run |
| `HDTRANS_CACHE_DIR=off` | every run | No dictionary cache shared between builds |

Unset private sets are skipped; nothing in the repository names or copies them.
