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
- Translation changes were measured and every changed line was judged (fix-translation steps
  11–12). Keep the numbers for the PR body.
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
  `windows-x64`, `windows-x86`, `wasm`, `release-tools`) as `pass`.
  - The matrix may show `skipping` only when `gate` passed and decided the tree was already tested
    (docs/build.md "CI").
  - `release` and `tested` may show `skipping`.
- An empty list or `pending` means the checks have not started or not finished. That is not a
  pass: wait or rerun, and never merge with checks pending.

## 2. Work branch → develop

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
- Open it with `gh pr create --base main --head develop --milestone X.Y.Z`.
- Wait for its checks (see "Merging any PR"), then merge with a merge commit, not a squash:
  `gh pr merge N --merge`.

## 4. What CI does after the merge

1. **Build** on the main push builds and tests every preset.
2. **Release main** commits the version files and CHANGELOG, pushes the `vX.Y.Z` tag and creates a
   draft release. No separate release PR exists, and develop pushes publish nothing.
3. **Publish npm** on the tag runs the full Build, then publishes the GitHub Release and the npm
   package. Check its log for the published `hdtrans@X.Y.Z`; if the registry failed, rerun the job.
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
