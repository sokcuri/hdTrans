# hdtrans — rule-based Japanese→Korean translation engine in C++

One static library and thin front ends: CLI, `hdTransEngine.dll` (C API), `J2KEngine.dll`
(J2K_* exports for hosts that load that DLL), WebAssembly. PCRE2 and libyaml are vendored
as sources in third_party/ (built by CMake, no network): PCRE2 for the regex rows of
the filter stage (src/core/filter), libyaml for optional settings (src/core/config).
README.md is for users; developer documentation is in docs/ (index: docs/README.md).

## Build and test
- CMake presets (CMakePresets.json): `mac` / `linux` use Ninja with clang or gcc;
  `windows-x86` / `windows-x64` use Visual Studio 2026 without a Developer prompt
  (CMake 4.2+). `windows-x86-ninja` / `windows-x64-ninja` need Ninja and a matching
  MSVC Developer prompt or `vcvarsall` environment. All native presets build the
  static/shared libraries, CLI and tests; J2KEngine.dll is Windows-only.
  `tools/build.ps1` selects installed Visual Studio 2022 or 2026. Visual Studio
  Release binaries are under `build/<preset>/Release`; Ninja binaries are directly
  under `build/<preset>`. `wasm` needs Emscripten and Ninja: `emcmake cmake --preset wasm`.
- `cmake --preset mac && cmake --build --preset mac --parallel && ctest --preset mac`
  is the development loop; substitute any native platform's preset for `mac`.
- CI uses Ninja with sccache for native builds and ccache for Emscripten's Clang calls;
  neither compiler cache is required by local presets.
- Unit tests use small local fixtures, not external expectation sets or dictionary totals.
- `tools/snapshot.sh` records built-CLI outputs; `tools/compare.sh` reports baseline changes
  (docs/testing.md).
- End-to-end test data (copyrighted texts and their expected outputs) lives in the private
  repository sokcuri/hdTrans-data, found through `HDTRANS_TESTDATA`; without it the tests
  skip. `tools/e2e.sh --expected "$HDTRANS_TESTDATA" CLI dicts` reports changed lines and
  `--accept` rewrites the expected outputs there; ctest runs it as `testdata`
  (docs/testing.md). Never copy its texts or translations into this repository, commit
  messages, issues or pull requests.
- A translation behaviour change needs its own commit with the reason; once it is on `main`,
  accept the new outputs in the test-data repository and commit them there.
- `tools/detect-bad.sh` lists ill-formed Korean sequences in outputs; a pattern becomes a
  constraint rule (`src/core/word/constraints.cpp`) only when its checked hits are all errors.
- C++20. Library and DLLs are built without exceptions and RTTI; errors are return
  values. Each DLL links the CRT statically and is a single file.
- Warnings are errors; fix the code, not the flags.

## Branches
- `develop` is the default branch: work branches start from it and pull requests target it.
- `main` receives release promotions from `develop` through PRs. After CI passes, the release bot commits version files and cuts release tags.
- Commit and PR titles use Conventional Commits: `fix(scope): summary`, `feat(scope): summary`,
  or `docs`, `perf`, `refactor`, `style`, `test`, `build`, `ci`, `chore`, `revert`. Scope is optional.
  Breaking changes use `!` or a `BREAKING CHANGE:` footer, not a `major:` type.
- Squash ordinary PRs into `develop`; merge `develop` into `main` without squashing to preserve change history.
  The promotion PR title controls the release bump. After CI, the bot commits version files and CHANGELOG,
  then creates the release without another PR (docs/contributing.md).

## Fixing a translation
- docs/fixing-translations.md has the steps. Data (dictionary rows, codes) before rules, rules
  before engine code. Each fix gets a fix test and a keep test.
- Judge behaviour changes against a build of a fixed commit in its own worktree, not against
  a moving main; revert or narrow a change behind a line both judges call worse.
- External user dictionaries (Ehnd) are never merged into dicts/.
- Run engines and ctest with `HOME` set to a fresh temporary folder when builds run in parallel;
  the dictionary cache is per user.

## Source rules
- Standard library only, with PCRE2 in src/core/filter and libyaml in src/core/config only. UTF-8 `std::string` internally. Shift-JIS and CP949 exist only
  at the `J2K_*` boundary, converted by the OS; the engine has no code-page tables.
- Types and functions `CamelCase`, local variables `lowerCamel`, private members with a
  trailing underscore, files `snake_case`, one pipeline stage per directory under
  `src/core/`. Internal functions go in `hdTrans::internal` and in headers under
  `src/`, never in `include/`. Identifiers starting with an underscore are not used.
- Plain functions and structs; no abstraction layer unless two implementations exist.
- The library runs inside other people's processes: no global constructors with side
  effects, no stdout/stderr, no `exit`, no locale changes; `Translate` is callable from
  several threads at once on one loaded dictionary.
- Inputs of any length are handled; no fixed-size buffers.
- Tests: table tests on real inputs with `tests/unit/hdtest.h`; no framework, no mocks.
  Every stage has tests next to it.

## Comments and docs
- Comment only what a reader needs to use the code: non-obvious behaviour, invariants,
  data layouts. Short. Prefer no comment over a meta comment.
- Docs and comments say what is needed and stop: no restating tables in prose, no filler,
  no summaries of what was just said. In Korean text use commas or 와/과, not the middle dot.
- No justification or history comments ("kept because", "verified against", "used to
  be"); no references to other projects, agents, test results, machines, users or paths.

## Repository hygiene
- One coherent change per commit; the message says what changed and whether the
  expectation sets moved.
- `src/core/dict/builtin/` holds the builtin tables compiled into the library
  (`cmake/builtin_tables.cmake` turns them into C++ at build time); edit the TSVs.
- The rules program of the word stage is the `ctx` column of the dictionary rows that name a
  local rule plus the global rules in `src/core/rules/globals.txt` (docs/rule-language.md). The
  program is dumped with `hdtrans --dump-rules`; `src/core/rules/compiled_gen.cpp` is generated
  from that dump by `hdrulec cpp` and `program.hash` by `hdrulec dump -hash` (docs/rule-compiler.md).
- `dicts/` is the shipped dictionary set and is committed; each file starts with
  `#! layer=...` and a `#` description line (docs/dictionary-files.md).
- Never commit build outputs, expectation sets, machine names, user
  names or absolute paths. Locations outside the repository come from environment
  variables with relative defaults.
