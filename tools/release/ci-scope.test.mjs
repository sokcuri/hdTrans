import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { allPresets, ciScope, pathClass, scopeOf } from './ci-scope.mjs';

const pr = (head = 'fix/x', base = 'develop') => ({
  eventName: 'pull_request', payload: { pull_request: { head: { ref: head }, base: { ref: base } } },
});

// A repository whose HEAD changes the given files on top of a base commit holding before.
function repository(t, before, after) {
  const dir = mkdtempSync(join(tmpdir(), 'hdtrans-ci-scope-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const git = (...args) => execFileSync('git', args, {
    cwd: dir, encoding: 'utf8',
    env: { ...process.env, GIT_DIR: undefined, GIT_WORK_TREE: undefined, GIT_INDEX_FILE: undefined },
  }).trim();
  git('init', '-q', '-b', 'develop');
  git('config', 'user.email', 'test@example.invalid');
  git('config', 'user.name', 'test');
  git('config', 'commit.gpgsign', 'false');
  const write = files => {
    for (const [path, text] of Object.entries(files)) {
      mkdirSync(join(dir, dirname(path)), { recursive: true });
      writeFileSync(join(dir, path), text);
    }
    git('add', '-A');
    git('commit', '-q', '--no-verify', '-m', 'commit');
  };
  write({ 'README.md': 'readme\n', ...before });
  write(after);
  return git;
}

test('every path has one class', () => {
  for (const [path, kind] of [
    ['CMakeLists.txt', 'build'], ['CMakePresets.json', 'build'], ['cmake/version.cmake', 'build'],
    ['third_party/pcre2/src/pcre2_compile.c', 'build'], ['.github/workflows/build.yml', 'build'],
    ['src/cshared/hdtrans_c.cpp', 'windows'], ['src/j2kengine/j2kengine.cpp', 'windows'], ['src/gui/main.cpp', 'windows'],
    ['tests/host/admin_priority.cpp', 'windows'],
    ['tools/build.ps1', 'windows'],
    ['dicts/base.tsv', 'data'], ['tests/rules/cases/greetings.tsv', 'data'], ['tests/variants/x.tsv', 'data'],
    ['tests/fuzz/regressions/a.txt', 'data'], ['examples/a.ja.txt', 'data'], ['ehnd/a.txt', 'data'],
    ['web/examples.tsv', 'data'], ['web/examples/a.txt', 'data'], ['hdtrans.config.example.yaml', 'data'],
    ['web/src/page.ts', 'web'], ['web/test/documents.test.mjs', 'web'], ['web/index.html', 'web'],
    ['.github/workflows/deploy-web.yml', 'release'], ['tools/release/release.mjs', 'release'],
    ['docs/build.md', 'docs'], ['spec/words.md', 'docs'], ['skills/a/SKILL.md', 'docs'], ['README.md', 'docs'],
    ['AGENTS.md', 'docs'], ['CHANGELOG.md', 'docs'],
    ['docs/pipeline-layers.md', 'data'],
    ['include/core/engine.h', 'code'], ['include/core/hdtrans.h', 'code'], ['src/frontend/dictionary_version.cpp', 'code'],
    ['src/frontend/module_dir.cpp', 'code'], ['src/core/word/select.cpp', 'code'], ['tests/unit/word/select_test.cpp', 'code'], ['tests/long_line.sh', 'code'],
    ['tools/rule-cases.sh', 'code'], ['tools/wasm_smoke.mjs', 'code'], ['src/wasm/main.cpp', 'code'],
    ['npm/package.json', 'code'], ['README.txt', 'code'], ['third-party.md', 'code'], ['LICENSE', 'code'],
  ]) {
    assert.equal(pathClass(path), kind, path);
  }
});

test('the classes of a pull request choose its presets', () => {
  const cases = [
    [['web'], ['wasm'], false],
    [['web', 'docs'], ['wasm'], false],
    [['data'], ['linux'], true],
    [['data', 'docs', 'release'], ['linux'], true],
    [['code'], ['linux', 'wasm'], true],
    [['code', 'data', 'web'], ['linux', 'wasm'], true],
    [['windows'], ['linux', 'windows-x64', 'windows-x86'], true],
    [['windows', 'code'], ['linux', 'windows-x64', 'windows-x86', 'wasm'], true],
    [['docs'], [], false],
    [['release'], [], false],
    [['build'], allPresets, true],
    [['build', 'docs'], allPresets, true],
    [[], allPresets, true],
  ];
  for (const [classes, presets, native] of cases) {
    const scope = scopeOf(new Set(classes));
    assert.deepEqual(scope.presets, presets, classes.join());
    assert.equal(scope.native, native, classes.join());
    assert.ok(scope.reason, classes.join());
  }
  assert.ok(!scopeOf(new Set(['code', 'windows', 'data', 'web'])).presets.includes('mac'));
});

test('pushes, manual runs, release pull requests and errors run every job; scheduled runs only the native tests', () => {
  const failing = () => { throw new Error('no git'); };
  for (const context of [
    { eventName: 'push', payload: {} }, { eventName: 'workflow_dispatch', payload: {} },
    pr('develop', 'main'), pr('main', 'develop'), pr('fix/x', 'main'),
  ]) {
    assert.deepEqual(ciScope({ context, git: failing }).presets, allPresets, JSON.stringify(context));
    assert.equal(ciScope({ context, git: failing }).native, true);
  }
  const broken = ciScope({ context: pr(), git: failing });
  assert.deepEqual(broken.presets, allPresets);
  assert.equal(broken.native, true);
  assert.match(broken.reason, /scope error: no git/);
  const scheduled = ciScope({ context: { eventName: 'schedule', payload: {} }, git: failing });
  assert.deepEqual(scheduled.presets, []);
  assert.equal(scheduled.native, true);
});

test('a pull request is scoped by the paths its merge commit changes', t => {
  for (const [after, presets, native] of [
    [{ 'web/src/page.ts': 'page\n', 'docs/web.md': 'web\n' }, ['wasm'], false],
    [{ 'dicts/base.tsv': 'a\tb\n', 'tests/rules/cases/a.tsv': 'a\tb\n' }, ['linux'], true],
    [{ 'src/core/word/select.cpp': 'int f();\n' }, ['linux', 'wasm'], true],
    [{ 'docs/build.md': 'build\n' }, [], false],
    [{ 'CMakeLists.txt': 'project(x)\n' }, allPresets, true],
  ]) {
    const git = repository(t, {}, after);
    const scope = ciScope({ context: pr(), git });
    assert.deepEqual(scope.presets, presets, Object.keys(after).join());
    assert.equal(scope.native, native, Object.keys(after).join());
  }
});

test('a changed source with a Windows-only section at either commit runs the Windows presets', t => {
  const windows = '#ifdef _WIN32\nint g();\n#endif\n';
  for (const [before, after, expected] of [
    [{}, { 'src/core/config/path.cpp': windows }, true],
    [{ 'src/core/config/path.cpp': windows }, { 'src/core/config/path.cpp': `${windows}int h();\n` }, true],
    [{ 'src/core/config/path.cpp': windows }, { 'src/core/config/path.cpp': 'int h();\n' }, true],
    [{ 'src/core/dict/load.h': '#if defined(_MSC_VER)\n#endif\n' }, { 'src/core/dict/load.h': '#if defined(_MSC_VER)\nint x;\n#endif\n' }, true],
    [{ 'src/core/config/path.cpp': windows }, { 'src/core/word/select.cpp': 'int f();\n' }, false],
    [{}, { 'tools/notes.txt': '_WIN32\n' }, false],
  ]) {
    const git = repository(t, before, after);
    const scope = ciScope({ context: pr(), git });
    assert.equal(scope.presets.includes('windows-x64') && scope.presets.includes('windows-x86'), expected,
      `${JSON.stringify(after)}: ${scope.reason}`);
    if (expected) assert.match(scope.reason, /Windows sections in /);
  }
});

test('a docs, spec or skills file that a test reads is not classed as docs', () => {
  const root = fileURLToPath(new URL('../../', import.meta.url));
  const files = [join(root, 'CMakeLists.txt')];
  const walk = dir => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) walk(path);
      else if (/\.(cpp|h|sh|cmake|py|mjs)$/.test(entry.name)) files.push(path);
    }
  };
  walk(join(root, 'tests'));
  const read = new Set();
  for (const file of files) {
    for (const [, path] of readFileSync(file, 'utf8').matchAll(/["'`/}]((?:docs|spec|skills)\/[\w./-]+\.\w+)["'`]/g)) {
      read.add(path);
    }
  }
  assert.ok(read.has('docs/pipeline-layers.md'), [...read].join());
  for (const path of read) {
    if (!existsSync(join(root, path))) continue;  // a scratch file a test writes
    assert.notEqual(pathClass(path), 'docs', `${path} (read by a test)`);
  }
});
