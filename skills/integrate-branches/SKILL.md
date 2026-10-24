---
name: integrate-branches
description: Combine several fix branches into one integration branch by cherry-picking. Use when parallel fix branches (each from the same base) must land together, with TSV conflicts, rule-number collisions, regenerated rule files, a gate and a judgment of the combined output.
---

# Integrate fix branches

## Setup

- Create one integration worktree on the agreed base: `git worktree add -b work/<name> <dir> <base>`.
  Never integrate in a checkout that holds someone's uncommitted edits.
- Before updating any other checkout to the new commits, save its edits with `git diff > edits.patch`
  for the dirty files. Reapply the patch afterwards and check `git status`. Do not stash in a shared tree.
- Configure and build the preset once. `tools/pick.sh` rebuilds through `tools/regen-rules.sh`.
- Build and measure the base in a sibling worktree `$BASE` (fix-translation setup step 4).

## Order and collisions

1. List the commits per branch: `git log --reverse --oneline <base>..<branch>`.
2. Find rule numbers added by more than one branch: `tools/rule-ids.py <base> <branch>...`.
   - For each shared number, renumber it in the branch picked later: its rows' `rule` column, and the
     `L<n>` names in its cases.
   - Do the same for colliding row ids (`w:N`, `<name>:N`).
   - Commit the renumbering on that branch before picking.
3. Pick order:
   - Base-wide mechanisms (engine, codes, global rules) first.
   - Then rules, then lexical rows.
   - Within each kind, the branch whose change is broader first.
   Write the order down; it is part of the report.

## Picking

```sh
tools/pick.sh $(git rev-list --reverse <base>..<branch>)
```

- Table conflicts (dicts/, builtin tables, tests/rules/) keep both sides, unless both sides change
  the same row (same id, same case, or same key and class). Such a block is left with its markers.
- `program.hash` and `compiled_gen.cpp` are regenerated.
- `examples/*.ko.txt` keep ours and are re-checked by the gate.
- The run stops on any of these:
  - any other conflict;
  - a merge commit, or a cherry-pick that fails without conflicts (the git output is printed);
  - a new repeated row, a reused id, or a contradicting case;
  - a row the picked commit deleted that came back;
  - a staged marker or a stray path.
- A stop leaves the pick in progress. Resolve it by hand, run `git commit -C <commit>`, then pick the
  rest. Picks that stop nothing print `picked`, and only commits already applied print `empty`;
  check that the picked count matches.

Conflict policy:

- **Same cause fixed twice**: keep one mechanism, the broader one (a code or class rule over word
  rows, an engine fix over per-word rules). Delete the other branch's rows and cases, and keep its
  `keep` rows if they still pass.
- **Contradicting expectations**: decide by spec/. A rule that changes goes with its cases in the same commit.
- **Engine code**: merge by hand, never by union. Rebuild and run the unit tests of that stage.

## Verify

1. `tools/gate.sh` prints `GATE OK`.
2. The base may already have table findings, so compare the table check with the base instead of
   expecting none:
   ```sh
   (cd "$BASE" && python3 -I "$OLDPWD/tools/union-tsv.py" --check) | grep -v '^CHECK' | sort > base.check
   python3 -I tools/union-tsv.py --check | grep -v '^CHECK' | sort | comm -13 base.check -   # must be empty
   ```
3. `tools/shadow.py <base>`. Only warnings on lines the integration added or edited count.
4. Measure the integration against the base build and each branch's run (fix-translation step 11).
   - Judge, blind, every line that no single branch changed, which means only the combination
     produced it, and every line where two branches disagree (fix-translation step 12).
     `tools/new-pairs.py OUT.tsv INTEGRATION.pairs.tsv <branch pairs>...` keeps the first kind: it
     drops a line a branch already judged with the same before and after. Add the second kind, keys
     that two branch pair files hold with different after, even when the integration matches one.
   - The same thresholds apply: improved:worse at least 2:1, and chrF not lower than the base.
5. `tools/hygiene.py <base>` covers markers, stray paths, absolute paths and instruction wording
   over the whole diff and its messages.
6. Run the review-code skill on the combined diff before any PR.

## Report

Pick order, renumbered ids, overlaps and which mechanism was kept, dropped commits and why, gate
result, measurement and judged counts, and lines that only the combination changed.
