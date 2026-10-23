---
name: triage-issue
description: Triage a GitHub issue that reports a wrong translation. Use when a new or open issue needs reproduction on the current and previous release, a regression verdict, labels, a milestone, a reply and a fix branch.
---

# Triage a mistranslation issue

1. **Read** the issue: `gh issue view N --comments`. Take the Japanese input, the output and the expected
   Korean. Map the expectation to a spec/ rule id (`NUM-`, `LAY-`, `LEX-`, `SPC-`, `SND-`). If no rule
   covers it, note that the fix will add or narrow one.
2. **Reproduce on two releases**: the current release tag and the one before it.
   - Fetch each release's CLI, which carries its own dictionaries:
     ```sh
     gh release download vX.Y.Z -p '*macos-arm64.tar.gz' -D "$dir"   # or linux-x64
     tar -xzf "$dir"/*.tar.gz -C "$dir"
     ```
     Alternatively, build each tag in its own detached worktree and pass `-dicts <worktree>/dicts`.
   - Run with `HDTRANS_CACHE_DIR=off` and a fresh `HOME`:
     `printf '%s\n' '<ja>' | HOME=$(mktemp -d) "$dir"/hdTrans-X.Y.Z/hdtrans`
   - Also try `--speech soft` when the line is speech, and the current `develop` build.
3. **Decide**:
   - Mistranslation if the output breaks a spec rule or reads wrongly.
   - Regression if the previous release was better. A change of established output style counts too,
     even when the meaning is still right.
   - If the output is acceptable, close with an explanation.
   - Find the stage with `-why` (fix-translation step 2) to name the cause class.
4. **Label**: `mistranslation`; `regression` when it is one; one area label matching the spec file:
   `area:numbers` (NUM), `area:layout` (LAY), `area:words` (LEX), `area:speech` (SPC),
   `area:sounds` (SND).
   `gh issue edit N --add-label mistranslation,area:numbers`
5. **Milestone**: the release that will ship the fix (`gh api repos/{owner}/{repo}/milestones` lists
   them), then `gh issue edit N --milestone X.Y.Z`.
6. **Reply** in Korean, politely:
   - thank the report,
   - state the cause class in one sentence and the version that will fix it,
   - for a regression, say the fix restores the earlier behaviour.
   Keep internal tooling, data sets and other reports out of it.
   `gh issue comment N --body-file reply.md`
7. **Branch**: `git switch -c fix/issue-N develop`, then follow the fix-translation skill.
   - The fix and keep cases come from the issue's lines and their neighbours.
   - Run the probe-variations skill on the fix.
   - The commit or PR body carries `Fixes #N`.
8. **PR**: target `develop` and set the same milestone:
   `gh pr create --base develop --milestone X.Y.Z`
   The issue closes when the PR merges; check that it did.
