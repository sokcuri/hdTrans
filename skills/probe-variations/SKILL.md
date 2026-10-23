---
name: probe-variations
description: Test a translation fix beyond its own example sentence. Use after a fix (or before accepting one) to write variation and near-miss sets, run them through the baseline and fixed builds, and classify each line as fixed, still wrong, regressed or unchanged.
---

# Probe variations of a fix

## Write the sets

Keep both sets in `$HDTRANS_WORK/<branch>/` (not in the repository) as one line per input.

- **variations.txt** (about 60–120 lines): the fixed pattern in other forms.
  - Other verbs or nouns of the same class, and other conjugations (past, negative, て, polite).
  - Politeness levels, quoted and narrated lines.
  - `？`/`?`/`か`/`。` endings.
  - Full-width and half-width digits and Latin, kana and kanji spellings.
  - The pattern inside a longer line, and at the start and end of a line.
- **near-misses.txt**: look-alikes that must not change.
  - The same surface in another sense, or a neighbouring construction.
  - Lines just outside the rule's condition (the other side of every test in it).
- Add real lines that contain the pattern:
  `tools/count-class.py '<regex>' --show 50` lists them from `$HDTRANS_CORPUS` and `$HDTRANS_REF_SAMPLE/ja.txt`.

## Run

```sh
p=$HDTRANS_WORK/<branch>
run() { # CLI DICTS SET TAG [CLI-OPTIONS...]
  cli=$1 d=$2 n=$3 tag=$4; shift 4
  HOME=$(mktemp -d) HDTRANS_CACHE_DIR=off tools/with-timeout.sh 300 \
    "$cli" -dicts "$d" -each "$@" -in "$p/$n.txt" -out "$p/$n.$tag.txt"
}
for s in variations near-misses; do
  run "$BASE/build/<preset>/hdtrans" "$BASE/dicts" "$s" base      # add e.g. --speech soft to both runs
  run build/<preset>/hdtrans dicts "$s" fixed
  paste "$p/$s.txt" "$p/$s.base.txt" "$p/$s.fixed.txt" > "$p/$s.tsv"   # ja, before, after
done
```

The base is the pinned baseline worktree build (fix-translation setup step 4). Pass the same
options to both runs.

## Classify

Put one class on each line and count each class per set:

- **fixed**: wrong before, right after.
- **still wrong**: wrong in both. Is it the same cause (the fix is too narrow) or another cause
  (record it separately)?
- **regressed**: right before, wrong after. Any regressed line, especially in near-misses, narrows
  or blocks the fix.
- **unchanged right**.

Turn representative lines into `fix` and `keep` rows of the fix's case file: at least one per
variation kind that the fix covers, and one near-miss per condition. Report the counts and the
remaining wrong lines with their causes.
