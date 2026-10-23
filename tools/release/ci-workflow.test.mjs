import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { npmAssetName } from './publication.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const build = readFileSync(join(root, '.github/workflows/build.yml'), 'utf8');
const start = build.indexOf('      - name: Compare with the expected outputs\n');
assert.notEqual(start, -1);
const end = build.indexOf('\n      - name:', start + 1);
const comparison = build.slice(start, end === -1 ? undefined : end);
const script = comparison.match(/        run: \|\n([\s\S]*)$/)[1].replace(/^ {10}/gm, '');

test('expected-data comparison belongs to the existing Linux build', () => {
  assert.equal(existsSync(join(root, '.github/workflows/e2e.yml')), false);
  assert.match(comparison, /if: matrix\.preset == 'linux' && env\.HAS_TESTDATA_KEY == 'true'/);
  assert.match(script, /tools\/e2e\.sh --expected testdata build\/linux\/hdtrans/);
  assert.doesNotMatch(script, /cmake|createWorkflowDispatch/);
  assert.match(build, /needs: \[build, release-tools\]/);
});

for (const status of [0, 1, 2]) {
  test(`expected-data step preserves exit policy and private logs: ${status}`, t => {
    const dir = mkdtempSync(join(tmpdir(), 'hdtrans-ci-expected-'));
    t.after(() => rmSync(dir, { recursive: true, force: true }));
    mkdirSync(join(dir, 'tools'));
    mkdirSync(join(dir, 'dicts'));
    writeFileSync(join(dir, 'tools/e2e.sh'), `#!/bin/sh
[ "$1" = --expected ] && [ "$2" = testdata ] && [ "$3" = build/linux/hdtrans ] || exit 2
printf '%s\n' 'fixture: 1 changed line' '  1 A: fixture-private-marker' 'e2e: 1 documents compared, 1 changed lines in 1 documents'
exit "$FIXTURE_STATUS"
`, { mode: 0o700 });
    const summary = join(dir, 'summary');
    const result = spawnSync('bash', ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script], {
      cwd: dir,
      env: { ...process.env, RUNNER_TEMP: dir, GITHUB_WORKSPACE: dir, GITHUB_STEP_SUMMARY: summary, FIXTURE_STATUS: String(status) },
      encoding: 'utf8',
    });
    assert.ifError(result.error);
    assert.equal(result.status, status <= 1 ? 0 : 1, result.stderr);
    assert.doesNotMatch(result.stdout + result.stderr, /fixture-private-marker/);
    assert.match(readFileSync(join(dir, 'e2e.log'), 'utf8'), /fixture-private-marker/);
    const report = readFileSync(summary, 'utf8');
    assert.doesNotMatch(report, /fixture-private-marker/);
    if (status === 0) assert.match(report, /No translation changed/);
    if (status === 1) assert.match(report, /Translations changed; accept them/);
    assert.deepEqual(readdirSync(dir).filter(name => name.startsWith('hdtrans-e2e-home.')), []);
  });
}

const publisher = readFileSync(join(root, '.github/workflows/publish-npm.yml'), 'utf8');
const release = readFileSync(join(root, '.github/workflows/release.yml'), 'utf8');
const publisherJob = publisher.slice(publisher.indexOf('\n  publish:\n'));

function foldedCondition(workflow) {
  return workflow.match(/^    if: >-\n((?:^      .*\n)+)/m)[1].trim().replace(/\n\s*/g, ' ');
}
const canRelease = new Function('github', 'inputs', 'startsWith', `return (${foldedCondition(release)});`);
const canPackage = new Function('github', 'startsWith', `return (${foldedCondition(build)});`);
const canPublish = new Function('needs', `return (${publisherJob.match(/^    if: (.*)$/m)[1]});`);

test('tag publication owns the only tag trigger and calls Build without dispatch or completion listeners', () => {
  const triggers = publisher.slice(publisher.indexOf('\non:\n'), publisher.indexOf('\npermissions:'));
  assert.match(triggers, /  push:\n    tags: \['v\*'\]/);
  assert.match(triggers, /  workflow_dispatch:/);
  assert.doesNotMatch(triggers, /branches:|workflow_run:|workflow_call:/);
  assert.doesNotMatch(publisher, /build_run_id|createWorkflowDispatch|workflow_run:|getWorkflowRun|run\.status|Start npm publication after Build completes/);
  assert.match(publisher, /  build:\n    needs: validate[\s\S]*uses: \.\/\.github\/workflows\/build.yml/);
  assert.match(publisher, /dry_run: \$\{\{ github.event_name == 'workflow_dispatch' && inputs.dry_run \}\}/);
  assert.doesNotMatch(build.slice(0, build.indexOf('\npermissions:')), /tags:/);
  assert.match(build, /  workflow_call:/);
  assert.match(build, /group: build-\$\{\{ github.workflow \}\}-\$\{\{ github.ref \}\}/);
});

test('manual recovery requires a stable tag before any reusable build', () => {
  const script = publisher.match(/          script: \|\n([\s\S]*?)\n\n  build:/)[1].replace(/^ {12}/gm, '');
  const validate = new Function('context', script);
  assert.doesNotThrow(() => validate({ ref: 'refs/tags/v1.2.3' }));
  for (const ref of ['refs/heads/main', 'refs/heads/develop', 'refs/tags/nightly', 'refs/tags/v1.2.3-beta', 'refs/tags/v01.2.3']) {
    assert.throws(() => validate({ ref }), /release tag/);
  }
});

test('only successful stable release output authorizes the hosted OIDC publisher', () => {
  for (const result of ['success', 'failure', 'cancelled', 'skipped']) {
    for (const release_published of ['true', 'false', '', undefined]) {
      assert.equal(canPublish({ build: { result, outputs: { release_published } } }),
        result === 'success' && release_published === 'true');
    }
  }
  assert.match(build, /value: \$\{\{ jobs.release.outputs.release_published \}\}/);
  assert.match(release, /value: \$\{\{ jobs.release.outputs.release_published \}\}/);
  assert.match(release, /release_published: \$\{\{ steps.publish.outputs.release_published \}\}/);
  assert.match(release, /core.setOutput\('release_published', String\(published\)\)/);
  assert.match(publisherJob, /runs-on: ubuntu-latest/);
  assert.match(publisherJob, /environment: npm/);
  assert.match(publisherJob, /id-token: write/);
  assert.doesNotMatch(publisherJob, /self-hosted|NODE_AUTH_TOKEN|NPM_TOKEN/);
});

test('dry runs, PRs and branch builds cannot publish stable releases or npm', () => {
  const cases = [
    ['push', 'refs/tags/v1.2.3', false, true],
    ['workflow_dispatch', 'refs/tags/v1.2.3', false, true],
    ['workflow_dispatch', 'refs/tags/v1.2.3', true, false],
    ['workflow_dispatch', 'refs/heads/main', false, false],
    ['push', 'refs/heads/main', false, false],
    ['pull_request', 'refs/pull/1/merge', false, false],
    ['push', 'refs/heads/develop', false, true],
  ];
  for (const [event_name, ref, dry_run, expected] of cases) {
    const github = { event_name, ref };
    assert.equal(canRelease(github, { dry_run }, (a, b) => a.startsWith(b)), expected);
    if (expected) assert.equal(canPackage(github, (a, b) => a.startsWith(b)), true);
  }
  assert.match(build, /dry_run: \$\{\{ inputs.dry_run \|\| false \}\}/);
});

test('publication uses this run artifact and only its job enters the global non-cancelling queue', () => {
  const download = publisherJob.match(/      - uses: actions\/download-artifact@v8\n([\s\S]*?)(?=      - )/)[1];
  assert.match(download, /name: release-packages/);
  assert.doesNotMatch(download, /run-id:|github-token:/);
  assert.equal((publisher.match(/\bconcurrency:/g) ?? []).length, 1);
  assert.match(publisherJob, /concurrency:\n      group: publish-npm\n      queue: max\n      cancel-in-progress: false/);
  assert.doesNotMatch(publisher.slice(0, publisher.indexOf('\n  publish:\n')), /concurrency:/);
  assert.match(publisher, /secrets:\n      HDTRANS_TESTDATA_KEY: \$\{\{ secrets.HDTRANS_TESTDATA_KEY \}\}/);
  assert.doesNotMatch(publisher, /secrets: inherit|HDTRANS_RELEASE_PUSH_KEY/);
});

test('source checks bind both tag pipelines to the checked out commit and main ancestry', () => {
  for (const workflow of [publisher, release]) {
    assert.match(workflow, /test "\$\(git rev-parse HEAD\)" = "\$\(git rev-parse "\$GITHUB_SHA\^\{commit\}"\)"/);
    assert.match(workflow, /test "\$\(git rev-parse HEAD\)" = "\$\(git rev-parse "\$GITHUB_REF\^\{commit\}"\)"/);
    assert.match(workflow, /git merge-base --is-ancestor HEAD origin\/main/);
  }
  assert.match(publisherJob, /verifyNpmPackage\(\{ packageDir: process.env.PACKAGE_DIR, ref: process.env.GITHUB_REF \}\)/);
  assert.match(publisherJob, /if: steps.package.outputs.skip != 'true'/);
  assert.match(publisherJob, /npm publish "\$PACKAGE" --access public --provenance --tag "\$DIST_TAG"/);
});

test('only the release mutation job serializes stable and nightly publication separately', () => {
  const mutation = release.slice(release.indexOf('\n  release:\n'));
  assert.doesNotMatch(release.slice(0, release.indexOf('\n  release:\n')), /concurrency:/);
  assert.match(mutation, /group: github-release-\$\{\{ github.ref == 'refs\/heads\/develop' && 'nightly' \|\| 'stable' \}\}/);
  assert.match(mutation, /queue: max\n      cancel-in-progress: false/);
});

test('the packed npm package is uploaded and summed under the name the publisher verifies', () => {
  const name = npmAssetName('$version');
  assert.ok(release.includes(`mv "build/release/hdtrans-$version.tgz" "build/release/${name}"`));
  assert.ok(release.includes(`sha256sum "${name}" >> SHA256SUMS.txt`));
});
