---
name: release
description: Land finished work and cut a release. Use when a reviewed work branch goes to develop, or when develop is promoted to main for a new version, including the code review gate, PR titles and bodies, CI release steps, goldens acceptance in the test-data repository and milestone closing.
---

# Release

Branch and title rules: AGENTS.md "Branches" and docs/contributing.md. CI: docs/build.md "CI" and
"릴리스", web deploy: docs/web.md "자동 배포".

## 1. Review gate

- Run the review-code skill on the whole branch diff (`git diff <remote>/develop...HEAD`) with a separate Opus
  reviewer. Fix every blocker, then have the commits added after the review reviewed as a delta.
- `tools/gate.sh` prints `GATE OK`. `tools/shadow.py <remote>/develop` is clean. If the tree has `mac-asan` or
  `linux-asan` presets, the sanitizer ctest is clean too.
- Translation changes were measured and judged (fix-translation steps 11–12). Keep the numbers for
  the PR body.
- Grep the diff, the commit messages and the PR text for source names and instruction wording
  (fix-translation "Before committing"). Rewrite any hit before pushing.

## 2. Work branch → develop

```sh
git push -u <remote> <branch>
gh pr create --base develop --title 'fix(<scope>): <summary>' --body-file body.md --milestone X.Y.Z
gh pr merge N --squash --subject 'fix(<scope>): <summary> (#N)' --body '<plain summary>'
```

- The squash subject decides the bump. A `feat` subject on main bumps the minor version, so use
  `fix:` unless a feature release is intended. Breaking changes use `!`.
- The body states what changed, the cause classes, the tests, and the judged counts (changed,
  better, same, worse). Name no data sets or sources.
- Use `Fixes #N` for the issues the PR resolves.

## 3. develop → main

- Write a new title and body as a release summary, grouped by area (words, speech, numbers, layout,
  sounds, tools). Do not copy the develop PR. The title follows Conventional Commits and sets the
  version bump.
- `gh pr create --base main --head develop --milestone X.Y.Z`, then merge with a merge commit, not a
  squash: `gh pr merge N --merge`.

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
