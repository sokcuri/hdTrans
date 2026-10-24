---
name: review-code
description: Review an engine or tools diff before a PR. Use for the mandatory code review of a work or integration branch, covering correctness of rules and data changes, sanitizer and lifetime checks, -Wshadow-all, dangling views, thread safety and repository hygiene.
---

# Review code

The reviewer only reads; the author fixes. Verify every finding with `file:line`, plus the input and
the outputs before and after when behaviour is involved. Report findings by section, blockers first.

## 1. Translation logic

- Each rule or data change fixes its cause at the stage where the error first appears. Watch for
  whole-phrase rows, output rewrites after selection, and conditions that list surface forms from
  the examples (fix-translation steps 2–5).
- A condition with sentence-wide triggers needs to be positional. A widened rule needs `keep` cases
  on the other side of its condition.
- Two mechanisms that decide the same thing, or duplicate case files across branches: keep one.
- Cases assert what the change decides. They must not pin unrelated wrong output.
- Cases cover the class (fix-translation step 9): rows from real lines for each shape the change
  corrects and for its near misses. A finding that sends a line back asks for that line, and the
  others like it, as cases with the correct output.
- Rule lint: `hdrulec lint build/rules.txt -dicts dicts` adds no new warnings for the changed rules; ctest
  `rules.lint` fails when a flag's count rises above `tools/lint-budget.tsv`; a lower count passes with a
  note, and the line may be lowered then.

## 2. Builds that catch memory and lifetime errors

- Sanitizers: use the `mac-asan` or `linux-asan` preset when `cmake --list-presets` shows it.
  Otherwise configure the same with flags:
  ```sh
  F='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-sanitize=vptr -fno-omit-frame-pointer'
  cmake --preset mac -B build/asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$F" -DCMAKE_C_FLAGS="$F" \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined
  tools/with-timeout.sh 3600 cmake --build build/asan --parallel
  (cd build/asan && HOME=$(mktemp -d) HDTRANS_DICTS=$PWD/../../dicts ../../tools/with-timeout.sh 3600 ctest -E '^testdata$')
  ```
  - On Linux use `--preset linux`. Use Clang; GCC's stack-use-after-return checking makes
    dictionary loading far slower.
  - Also run a long input and the examples under the sanitizer build. Any report aborts the run.
- `tools/shadow.py [<base>]` runs `-Wshadow-all` on the sources changed since the merge base with
  develop. MSVC rejects a local or parameter that hides another name (C4456, C4457), and only the
  Windows CI would catch it otherwise.
  - Warnings on lines the change added or edited fail. Warnings elsewhere are listed as existing.
  - A constructor parameter named like its member and a lambda local that shadows an uncaptured
    local are not reported, because MSVC accepts both.
- Views into arguments: when the tree has `HDTRANS_LIFETIMEBOUND` (`src/core/text/lifetime.h`), every
  function returning a `string_view`/`span` into a parameter or `*this` carries it, so Clang reports
  views of temporaries (`-Wdangling-gsl`).

## 3. What to read for

- **Dangling**:
  - a `string_view` or `span` bound to a temporary `std::string` (a key built inline, `a + b`,
    `.substr()`);
  - views kept in a struct past the owner's lifetime;
  - references into a `std::vector` that grows;
  - a range-for over a member of a temporary;
  - lambdas capturing locals by reference that outlive the call.
- **Bounds and empty input**:
  - indexing without a size check;
  - `memcpy`/`fwrite` with a null pointer for empty data;
  - an `npos` result going into arithmetic.
- **Input size**:
  - recursion whose depth grows with the line length (thread stacks are 1 MB on Windows);
  - rescans that make a long line quadratic.
  Time a long line (for example 2000 repeats of a short phrase) with a time limit before and after.
- **Threads**: `Translate` runs on several threads over one loaded dictionary. Look for mutable
  statics, caches without synchronisation, and globals with side-effect constructors.
- **Library rules** (AGENTS.md):
  - no stdout/stderr, `exit` or locale changes in the library;
  - no exceptions or RTTI;
  - standard library only, outside src/core/filter and src/core/config.

## 4. Hygiene over the diff

- `tools/hygiene.py <base>` covers conflict markers, added paths named like options or holding
  spaces, absolute home paths, the middle dot, wording that records an instruction, and the
  private patterns in `$HDTRANS_HYGIENE_PATTERNS` (names of sources, data sets, works).
  Read every hit.
- Generated files (`*_gen.*`, `program.hash`) change only through `tools/regen-rules.sh`.
  Check with `git diff <base> --name-only | grep -E '_gen\.|program\.hash'` and regenerate to compare.
- Tests use neutral names.
- Comments follow AGENTS.md: no history, no justification, and no references to sources, machines
  or people.
