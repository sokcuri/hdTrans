import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { test } from 'node:test';

const workflow = readFileSync(new URL('../../.github/workflows/build.yml', import.meta.url), 'utf8');

function step(name) {
  const marker = `      - name: ${name}\n`;
  const start = workflow.indexOf(marker);
  assert.notEqual(start, -1);
  const end = workflow.indexOf('\n      - ', start + marker.length);
  return workflow.slice(start, end < 0 ? undefined : end);
}

test('sccache post-action does not publish statistics after server shutdown', () => {
  assert.match(step('Set up sccache'), /disable_annotations: true/);
  const report = step('Report compiler cache');
  const capture = report.indexOf('sccache --show-stats');
  const summary = report.indexOf('} >> "$GITHUB_STEP_SUMMARY"');
  const stop = report.indexOf('sccache --stop-server');
  assert.ok(capture >= 0 && summary > capture && stop > summary);
});

for (const preset of ['linux', 'mac', 'windows-x64', 'windows-x86']) {
  test(`${preset} cache summary appends the captured real statistics`, t => {
    const report = step('Report compiler cache');
    const block = report.match(/^            \{\n[\s\S]*?^            \} >> "\$GITHUB_STEP_SUMMARY"$/m);
    assert.ok(block);
    const script = block[0].replace(/^            /gm, '').replaceAll('${{ matrix.preset }}', preset);
    const root = mkdtempSync(join(tmpdir(), 'hdtrans-cache-summary-'));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    mkdirSync(join(root, 'build', preset), { recursive: true });
    const stats = 'Compile requests                    220\nCache hits                          216\nCache misses                          4\n';
    writeFileSync(join(root, 'build', preset, 'sccache.log'), stats);
    const summary = join(root, 'summary.txt');
    writeFileSync(summary, 'Existing test results\n');
    const result = spawnSync('bash', ['-eo', 'pipefail', '-c', script], {
      cwd: root,
      env: { ...process.env, GITHUB_STEP_SUMMARY: summary },
      encoding: 'utf8',
    });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(readFileSync(summary, 'utf8'),
      `Existing test results\n### ${preset} compiler cache\n\`\`\`text\n${stats}\`\`\`\n`);
  });
}

function condition(name) {
  const body = step(name);
  const folded = body.match(/^        if: >-\n((?:^          .*\n?)+)/m);
  const expression = (folded ? folded[1].trim().replace(/\n\s*/g, ' ') : body.match(/^        if: (.*)$/m)[1])
    .replace(/steps\.([\w-]+)\.(outcome|outputs\.([\w-]+))/g, (_, id, field, output) =>
      output ? `steps['${id}'].outputs['${output}']` : `steps['${id}'].${field}`);
  return new Function('github', 'matrix', 'steps', 'cancelled', `return (${expression});`);
}

const events = {
  'develop push': [{ event_name: 'push', ref: 'refs/heads/develop' }, true],
  'pull request': [{ event_name: 'pull_request', ref: 'refs/pull/1/merge' }, true],
  'main push': [{ event_name: 'push', ref: 'refs/heads/main' }, false],
  'tag pipeline': [{ event_name: 'push', ref: 'refs/tags/v1.2.3' }, false],
  'manual run on main': [{ event_name: 'workflow_dispatch', ref: 'refs/heads/main' }, false],
  'manual run on a tag': [{ event_name: 'workflow_dispatch', ref: 'refs/tags/v1.2.3' }, false],
};

test('only develop pushes and pull requests save compiler and Emscripten caches', () => {
  const steps = {
    compile: { outcome: 'success' },
    'native-cache': { outputs: {} }, 'wasm-cache': { outputs: {} }, 'emsdk-cache': { outputs: { 'cache-hit': 'false' } },
  };
  const saves = { 'Save native compiler cache': 'linux', 'Save WASM compiler cache': 'wasm', 'Save Emscripten': 'wasm' };
  for (const [name, preset] of Object.entries(saves)) {
    const save = condition(name);
    for (const [event, [github, expected]] of Object.entries(events)) {
      assert.equal(save(github, { preset }, steps, () => false), expected, `${name}: ${event}`);
    }
    const develop = events['develop push'][0];
    assert.equal(save(develop, { preset: preset === 'wasm' ? 'linux' : 'wasm' }, steps, () => false), false, name);
  }
  const develop = events['develop push'][0];
  for (const name of ['Save native compiler cache', 'Save WASM compiler cache']) {
    const preset = saves[name];
    assert.equal(condition(name)(develop, { preset }, { ...steps, compile: { outcome: 'failure' } }, () => false), false);
    assert.equal(condition(name)(develop, { preset }, steps, () => true), false);
  }
  const hit = { ...steps, 'emsdk-cache': { outputs: { 'cache-hit': 'true' } } };
  assert.equal(condition('Save Emscripten')(develop, { preset: 'wasm' }, hit, () => false), false);
  assert.equal(condition('Install Emscripten')(develop, { preset: 'wasm' }, hit, () => false), false);
  assert.equal(condition('Install Emscripten')(develop, { preset: 'wasm' }, steps, () => false), true);
});

test('caches are restored separately and saved under the restore step key', () => {
  for (const file of ['build.yml', 'deploy-web.yml', 'release.yml', 'publish-npm.yml']) {
    const text = readFileSync(new URL(`../../.github/workflows/${file}`, import.meta.url), 'utf8');
    assert.doesNotMatch(text, /actions\/cache@|setup-emsdk/, file);
  }
  for (const [restore, save] of [['Restore native compiler cache', 'Save native compiler cache'],
    ['Restore WASM compiler cache', 'Save WASM compiler cache'], ['Restore Emscripten', 'Save Emscripten']]) {
    assert.match(step(restore), /uses: actions\/cache\/restore@v6/);
    const id = step(restore).match(/id: ([\w-]+)/)[1];
    assert.match(step(save), new RegExp(`key: \\$\\{\\{ steps\\.${id}\\.outputs\\.cache-primary-key \\}\\}`));
    assert.equal(step(save).match(/path: (.*)/)[1], step(restore).match(/path: (.*)/)[1]);
  }
  // A pinned key: every workflow and run reads the same Emscripten entry.
  assert.match(step('Restore Emscripten'), /key: emsdk-\$\{\{ env\.EMSDK_VERSION \}\}-\$\{\{ runner\.os \}\}-\$\{\{ runner\.arch \}\}$/m);
  assert.doesNotMatch(step('Restore Emscripten'), /restore-keys|github\.(workflow|run_id)/);
  assert.match(step('Set up ccache for Emscripten'), /CCACHE_COMPILERCHECK=string:emsdk-\$EMSDK_VERSION/);
});
