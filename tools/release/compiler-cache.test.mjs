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
