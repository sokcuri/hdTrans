---
name: release
description: Land finished work and cut a release. Use when a reviewed work branch goes to develop, or when develop is promoted to main for a new version, including the code review gate, PR titles and bodies, CI release steps, goldens acceptance in the test-data repository and milestone closing.
---

# Release

Branch and title rules: AGENTS.md "Branches" and docs/contributing.md. CI: docs/build.md "CI" and
"릴리스", web deploy: docs/web.md "자동 배포".

## 1. Review gate

- Run the review-code skill on the whole branch diff (`git diff <remote>/develop...HEAD`) with a
  separate Opus reviewer. Fix every blocker, then have a reviewer read the commits added after the
  review as a delta.
- `tools/gate.sh` prints `GATE OK`.
- `tools/shadow.py` reports no new warning.
- If the tree has `mac-asan` or `linux-asan` presets, the sanitizer ctest passes too.
- The CI `Performance budget` step (linux) and the `linux-fuzz` job pass, and their warnings are read
  (docs/build.md "CI", "성능 예산", "퍼징"). A slowdown that is intended has a `Perf-Budget: <reason>`
  trailer. An input the fuzzer printed is fixed, kept in `tests/fuzz/regressions/` and covered by a
  rule case, or filed as an issue when it is an older bug.
- Translation changes were measured and every changed line was judged blind by judges that did not
  write the change (fix-translation steps 11–12). Keep the numbers for the PR body.
- For a develop → main promotion, run the cycle check (section 2) on the develop head and judge
  its new lines; the lines of earlier checks are already judged. Report the cycle totals since
  the last release (changed, improved, same, worse; docs/testing.md "주기 누적 확인").
- For a develop → main promotion, run the error-rate audit on the develop head (docs/testing.md
  "오류율 감사"): `tools/measure.sh` on all four sets, `tools/audit.py draw` with the default seed and
  size, grade every line, `tools/audit.py rate`. Lines, grades and rates stay in
  `$HDTRANS_WORK/audit/<version>/`.
  Report wrong % and not-correct % per set next to the sample chrF, with the previous release's
  numbers from its audit folder.
- `tools/hygiene.py <remote>/develop` has no real hit. Read the PR title and body for the same things.

## Merging any PR

Wait for its checks before merging:

```sh
gh pr checks N --watch --fail-fast
gh pr checks N
```

- The second command must show `gate` and `title` as `pass`, and the matrix (`linux`, `mac`,
  `windows-x64`, `windows-x86`, `wasm`, `release-tools`) and `linux-asan`, `linux-fuzz` as `pass`.
  - The matrix may show `skipping` only when `gate` passed and decided the tree was already tested
    (docs/build.md "CI").
  - `release` and `tested` may show `skipping`.
- An empty list or `pending` means the checks have not started or not finished. That is not a
  pass: wait or rerun, and never merge with checks pending.

## 2. Work branch → develop

A PR that changes translations is measured against the develop it branched from, so PRs merged in
the same cycle can interact (docs/testing.md "주기 누적 확인"). Before merging such a PR, if develop
moved since it was measured:

1. Rebase the branch on the current develop and rebuild.
2. Measure it against a run of the current develop head, built in its own worktree:
   `tools/measure.sh -b develop-<short sha> <branch>`.
3. Judge, blind, only the lines `tools/new-pairs.py OUT.tsv <set>.pairs.tsv <earlier pairs of this
   PR>...` keeps. A line two judges call worse blocks the merge, as in fix-translation step 12.
4. Push the rebased branch and wait for its checks again.

```sh
git push -u <remote> <branch>
gh pr create --base develop --title 'fix(<scope>): <summary>' --body-file body.md --milestone X.Y.Z
gh pr checks N --watch --fail-fast && gh pr checks N
gh pr merge N --squash --subject 'fix(<scope>): <summary> (#N)' --body '<plain summary>'
```

- The body states what changed, the cause classes, the tests and the judgment numbers (changed,
  improved, same, worse). Name no data sets or sources.
- Use `Fixes #N` for the issues the PR resolves.
- The squash subject becomes the develop commit and its CHANGELOG line. It does not choose the version.
- Keep the branch's `Surface-Budget:` and `Perf-Budget:` trailers as trailers of the squash body.

After merging a PR that changes translations, run the cycle check: measure the merge commit of
develop against the run of the last release tag on all four sets, with the runs in
`$HDTRANS_WORK/cycle/<next version>/`, keep only the lines no earlier check of the cycle judged
(`tools/new-pairs.py`), and judge them blind into that check's `final.tsv`. The release review
then judges only what changed since the last check.

## 3. develop → main

- **The title of this PR decides the release** (`promotionVersion` in tools/release/release.mjs,
  docs/contributing.md):
  - `fix`, `perf` and `revert` make a patch release;
  - `feat` makes a minor release;
  - `!` or a `BREAKING CHANGE:` footer in the body makes a major release;
  - `docs`, `ci`, `chore`, `test`, `build`, `refactor` and `style` make no release.
  Use `fix:` unless a feature release is intended.
- Write a new title and body as a release summary, grouped by area (words, speech, numbers, layout,
  sounds, tools). Do not copy the develop PR.
- State how the counts of the `perf-budget` artifact of the latest successful develop push Build
  (`gh run download <run> -n perf-budget -D <dir>`) moved from `git show vX.Y.Z:tools/perf-budget.tsv`
  of the last release, with the `Perf-Budget:` reasons of `git log vX.Y.Z..develop`. Before the
  promotion, commit that artifact as `tools/perf-budget.tsv` through a PR into develop.
- Open it with `gh pr create --base main --head develop --milestone X.Y.Z`.
- Wait for its checks (see "Merging any PR"), then merge with a merge commit, not a squash:
  `gh pr merge N --merge`.

## 4. What CI does after the merge

1. **Build** on the main push builds and tests every preset.
2. **Release main** commits the version files and CHANGELOG, pushes the `vX.Y.Z` tag and creates a
   draft release. No separate release PR exists, and develop pushes publish nothing.
3. **Publish npm** on the tag runs the full Build, then publishes the GitHub Release and the npm
   package. Check its log for the published `hdtrans@X.Y.Z`; if the registry failed, rerun the job.
   - The random fuzz run of this Build only warns; the seed and regression replay fails it. Read
     the `linux-fuzz` summary and handle an input it found as in section 1.
4. **Deploy web** after Publish npm deploys that release to the web demo.
   - Check that the page's version line shows the release engine version, the dictionary version and the commit.
   - Never deploy from a work branch by hand.

Watch them with `gh run list --limit 10` and `gh run watch <id>`.

## 5. Goldens

When the release commit is on main, accept its outputs in the test-data repository:

```sh
git worktree add --detach <dir> vX.Y.Z      # build it there
tools/e2e.sh --expected "$HDTRANS_TESTDATA" <dir>/build/<preset>/hdtrans <dir>/dicts --accept
```

- In the test-data repository, check that the diff contains only this release's changes.
- Commit as `test(goldens): accept the X.Y.Z outputs` with the engine commit in the body, open a PR
  there and merge it.

## 6. Milestone

- Every PR and issue of the release carries the milestone. Issues close through `Fixes #N`.
- Check for open items with `gh issue list --milestone X.Y.Z`, then close the milestone:
  `gh api -X PATCH repos/{owner}/{repo}/milestones/<number> -f state=closed`.
- Create the next milestone if it does not exist.
