import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { buildGate, releaseBot, releaseFiles, versionCommit } from './build-gate.mjs';
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
    ['push', 'refs/heads/develop', false, false],
    ['workflow_dispatch', 'refs/heads/develop', false, false],
  ];
  for (const [event_name, ref, dry_run, expected] of cases) {
    const github = { event_name, ref };
    assert.equal(canRelease(github, { dry_run }, (a, b) => a.startsWith(b)), expected);
    if (expected) assert.equal(canPackage(github, (a, b) => a.startsWith(b)), true);
  }
});

test('a develop push packages as a dry run and never publishes or moves a tag', () => {
  const startsWith = (a, b) => a.startsWith(b);
  const passDryRun = new Function('github', 'inputs', 'startsWith',
    `return (${build.match(/^      dry_run: \$\{\{ (.*) \}\}$/m)[1]});`);
  const develop = { event_name: 'push', ref: 'refs/heads/develop' };
  assert.equal(canPackage(develop, startsWith), true);
  assert.equal(passDryRun(develop, {}, startsWith), true);
  assert.equal(canRelease(develop, { dry_run: false }, startsWith), false);
  assert.equal(passDryRun({ event_name: 'push', ref: 'refs/tags/v1.2.3' }, { dry_run: false }, startsWith), false);
  assert.equal(passDryRun({ event_name: 'workflow_dispatch', ref: 'refs/heads/main' }, { dry_run: false }, startsWith), true);
  assert.doesNotMatch(build + release, /nightly|updateRef|createRef|prerelease: true/);
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

test('only the release mutation job enters the release queue', () => {
  const mutation = release.slice(release.indexOf('\n  release:\n'));
  assert.doesNotMatch(release.slice(0, release.indexOf('\n  release:\n')), /concurrency:/);
  assert.match(mutation, /group: github-release\n/);
  assert.match(mutation, /queue: max\n      cancel-in-progress: false/);
});

test('the packed npm package is uploaded and summed under the name the publisher verifies', () => {
  const name = npmAssetName('$version');
  assert.ok(release.includes(`mv "build/release/hdtrans-$version.tgz" "build/release/${name}"`));
  assert.ok(release.includes(`sha256sum "${name}" >> SHA256SUMS.txt`));
});

const deployWeb = readFileSync(join(root, '.github/workflows/deploy-web.yml'), 'utf8');
const deployJob = deployWeb.slice(deployWeb.indexOf('\n  deploy:\n'));
const canDeployWeb = new Function('github', `return (${foldedCondition(deployWeb)});`);

test('the web deploy follows a successful tag pipeline of this repository or a manual run on develop', () => {
  const triggers = deployWeb.slice(deployWeb.indexOf('\non:\n'), deployWeb.indexOf('\npermissions:'));
  assert.match(triggers, /  workflow_run:\n    workflows: \[Publish npm\]\n    types: \[completed\]/);
  assert.match(triggers, /  workflow_dispatch:/);
  assert.doesNotMatch(triggers, /pull_request|push:|release:/);
  const run = (event, conclusion, owner = 'sokcuri/hdTrans') => ({
    repository: 'sokcuri/hdTrans', event_name: 'workflow_run', ref: 'refs/heads/develop',
    event: { workflow_run: { event, conclusion, head_repository: { full_name: owner } } },
  });
  assert.equal(canDeployWeb(run('push', 'success')), true);
  assert.equal(canDeployWeb(run('workflow_dispatch', 'success')), false);
  assert.equal(canDeployWeb(run('push', 'failure')), false);
  assert.equal(canDeployWeb(run('pull_request', 'success')), false);
  assert.equal(canDeployWeb(run('push', 'success', 'someone/hdTrans')), false);
  assert.equal(canDeployWeb({ ...run('push', 'success'), repository: 'someone/hdTrans' }), false);
  const dispatch = ref => ({ repository: 'sokcuri/hdTrans', event_name: 'workflow_dispatch', ref, event: {} });
  assert.equal(canDeployWeb(dispatch('refs/heads/develop')), true);
  assert.equal(canDeployWeb(dispatch('refs/heads/ci/x')), false);
  assert.equal(canDeployWeb(dispatch('refs/tags/v1.2.3')), false);
});

test('only the serialized web deploy job sees the deploy secrets and the example texts', () => {
  assert.equal((deployWeb.match(/secrets\./g) ?? []).length, (deployJob.match(/secrets\./g) ?? []).length);
  assert.match(deployJob, /environment:\n      name: web/);
  assert.match(deployJob, /concurrency:\n      group: deploy-web\$\{\{ inputs.dry_run && '-dry-run' \|\| '' \}\}\n      cancel-in-progress: false/);
  assert.doesNotMatch(deployJob, /upload-artifact/);
  assert.match(deployJob, /web\/deploy\.sh examples "\$RUNNER_TEMP\/web-examples"/);
  assert.match(deployJob, /StrictHostKeyChecking=yes/);
  assert.doesNotMatch(deployJob.slice(0, deployJob.indexOf('\n    steps:\n')), /HDTRANS_WEB_HOST/);
  assert.match(deployJob, /npm --prefix web ci --ignore-scripts/);
  const recheck = deployJob.indexOf('- name: Confirm the release is still the latest');
  assert.ok(recheck > 0 && recheck < deployJob.indexOf('- name: Deploy\n'));
  assert.match(deployJob, /- name: Deploy\n        if: env.DRY_RUN != 'true' && env.SKIP_DEPLOY != 'true'/);
  assert.match(deployWeb, /^permissions:\n  contents: read\n/m);
  assert.doesNotMatch(deployWeb, /contents: write|id-token|secrets: inherit/);
});

function gitFixture(t) {
  const dir = mkdtempSync(join(tmpdir(), 'hdtrans-gate-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const env = { ...process.env, GIT_CONFIG_GLOBAL: '/dev/null', GIT_CONFIG_SYSTEM: '/dev/null' };
  const git = (...args) => {
    const result = spawnSync('git', args, { cwd: dir, env, encoding: 'utf8' });
    if (result.status !== 0) throw new Error(result.stderr);
    return result.stdout.trim();
  };
  const write = (path, text) => {
    mkdirSync(join(dir, path, '..'), { recursive: true });
    writeFileSync(join(dir, path), text);
  };
  const commit = (message, { name = 'Someone', email = 'someone@example.com' } = {}) => {
    git('add', '-A');
    git('-c', `user.name=${name}`, '-c', `user.email=${email}`, 'commit', '-q', '--allow-empty', '-m', message);
    return git('rev-parse', 'HEAD');
  };
  git('init', '-q', '-b', 'main');
  git('config', 'user.name', 'Someone');
  git('config', 'user.email', 'someone@example.com');
  write('src/a.cpp', 'int a;\n');
  for (const path of releaseFiles) write(path, '0.1.0\n');
  return { dir, git, write, commit };
}

function versionCommitFixture(t, { changes = releaseFiles, identity = releaseBot, extraMessage = '' } = {}) {
  const fixture = gitFixture(t);
  const source = fixture.commit('fix: something (#1)');
  for (const path of changes) fixture.write(path, '0.1.1\n');
  const sha = fixture.commit(`chore(main): release 0.1.1\n\nRelease-Source: ${source}\nRelease-Build: 42${extraMessage}`, identity);
  const context = { eventName: 'push', ref: 'refs/heads/main', sha, payload: { before: source, after: sha } };
  return { ...fixture, source, sha, context };
}

test('the release bot version commit push to main skips the matrix', async t => {
  const { git, source, context } = versionCommitFixture(t);
  assert.equal(versionCommit({ payload: context.payload, sha: context.sha, git }), source);
  const gate = await buildGate({ context, git });
  assert.equal(gate.run, false);
  assert.match(gate.reason, /version commit/);
});

test('anything else about a version-like commit builds everything', async t => {
  const cases = {
    'another author': versionCommitFixture(t, { identity: { name: 'Someone', email: releaseBot.email } }),
    'another email': versionCommitFixture(t, { identity: { name: releaseBot.name, email: 'bot@example.com' } }),
    'a source file': versionCommitFixture(t, { changes: [...releaseFiles, 'src/a.cpp'] }),
    'no change': versionCommitFixture(t, { changes: [] }),
    'another message': versionCommitFixture(t, { extraMessage: '\nExtra: 1' }),
  };
  for (const [name, { git, context }] of Object.entries(cases)) {
    assert.equal((await buildGate({ context, git })).run, true, name);
  }
  const { git, context, source, sha } = versionCommitFixture(t);
  const variants = {
    'a develop push': { ...context, ref: 'refs/heads/develop' },
    'a tag push': { ...context, ref: 'refs/tags/v0.1.1' },
    'a pull request': { ...context, eventName: 'pull_request' },
    'a manual run': { ...context, eventName: 'workflow_dispatch' },
    'a push of several commits': { ...context, payload: { before: '1'.repeat(40), after: sha } },
    'another head': { ...context, sha: source, payload: { before: source, after: source } },
  };
  for (const [name, variant] of Object.entries(variants)) {
    assert.equal((await buildGate({ context: variant, git })).run, true, name);
  }
  const broken = await buildGate({ context, git: () => { throw new Error('no git'); } });
  assert.equal(broken.run, true);
  assert.match(broken.reason, /no git/);
});

test('a version commit on a merge of two parents builds everything', async t => {
  const fixture = gitFixture(t);
  const base = fixture.commit('base');
  fixture.git('checkout', '-q', '-b', 'side');
  fixture.write('src/b.cpp', 'int b;\n');
  fixture.commit('side');
  fixture.git('checkout', '-q', 'main');
  fixture.git('merge', '-q', '--no-ff', '--no-commit', 'side');
  for (const path of releaseFiles) fixture.write(path, '0.1.1\n');
  fixture.git('add', '--', ...releaseFiles);
  const sha = fixture.commit(`chore(main): release 0.1.1\n\nRelease-Source: ${base}\nRelease-Build: 42`, releaseBot);
  assert.equal(fixture.git('show', '-s', '--format=%P', 'HEAD').split(' ').length, 2);
  const context = { eventName: 'push', ref: 'refs/heads/main', sha, payload: { before: base, after: sha } };
  assert.equal((await buildGate({ context, git: fixture.git })).run, true);
});

test('the gate runs first and only a definite no skips the release tools and the matrix', () => {
  const gate = build.slice(build.indexOf('\n  gate:\n'), build.indexOf('\n  release-tools:\n'));
  assert.match(gate, /fetch-depth: 2/);
  assert.match(gate, /buildGate\(\{ github, context \}\)/);
  assert.doesNotMatch(gate, /secrets\.|contents: write/);
  for (const job of ['release-tools', 'build']) {
    const body = build.slice(build.indexOf(`\n  ${job}:\n`));
    assert.match(body, /^    needs: gate\n    if: \$\{\{ !cancelled\(\) && needs\.gate\.outputs\.run != 'false' \}\}$/m, job);
  }
  assert.doesNotMatch(build + publisher, /skip ci/);
});

function stepScript(workflow, name) {
  const marker = `      - name: ${name}\n`;
  const start = workflow.indexOf(marker);
  assert.notEqual(start, -1, name);
  const end = workflow.indexOf('\n      - ', start + marker.length);
  const body = workflow.slice(start, end === -1 ? undefined : end);
  return body.match(/        run: \|\n([\s\S]*)$/)[1].replace(/^ {10}/gm, '');
}

function releaseFixture(t, { version = '1.2.3', npmName = `hdTrans-${version}-wasm.tgz`, engineVersion = version, tamper } = {}) {
  const base = mkdtempSync(join(tmpdir(), 'hdtrans-web-release-'));
  t.after(() => rmSync(base, { recursive: true, force: true }));
  const assets = join(base, 'assets');
  const stage = join(base, 'stage');
  const work = join(base, 'work');
  const bin = join(base, 'bin');
  for (const dir of [assets, `${stage}/hdTrans-${version}`, `${stage}/package/dist`, work, bin]) mkdirSync(dir, { recursive: true });
  writeFileSync(`${stage}/hdTrans-${version}/hdtrans`, '#!/bin/sh\n', { mode: 0o755 });
  writeFileSync(`${stage}/hdTrans-${version}/README.txt`, 'readme\n');
  writeFileSync(`${stage}/package/dist/hdtrans_engine.js`,
    `export default async function create() { return { version: () => '${engineVersion}' }; }\n`);
  writeFileSync(`${stage}/package/dist/hdtrans_engine.wasm`, 'wasm');
  writeFileSync(`${stage}/package/dist/index.js`, '');
  const tar = (...args) => assert.equal(spawnSync('tar', args, { cwd: stage }).status, 0);
  tar('-czf', join(assets, `hdTrans-${version}-linux-x64.tar.gz`), `hdTrans-${version}`);
  tar('-czf', join(assets, npmName), 'package');
  writeFileSync(join(assets, `hdTrans-${version}-windows.zip`), 'zip');
  const sums = spawnSync('sh', ['-c', 'sha256sum *'], { cwd: assets, encoding: 'utf8' }).stdout;
  writeFileSync(join(assets, 'SHA256SUMS.txt'), sums);
  if (tamper) writeFileSync(join(assets, tamper), 'tampered');
  writeFileSync(join(bin, 'gh'), `#!/bin/bash
[ "$1 $2 $3" = "release view v${version}" ] && { ls '${assets}'; exit 0; }
[ "$1 $2 $3" = "release download v${version}" ] || exit 64
shift 3
while [ $# -gt 0 ]; do
  case "$1" in
    -D) dir="$2"; shift 2 ;;
    -p) cp '${assets}'/"$2" "$dir/" || exit 1; shift 2 ;;
    *) shift ;;
  esac
done
`, { mode: 0o755 });
  const run = name => spawnSync('bash', ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', stepScript(deployWeb, name)], {
    cwd: work,
    env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, TAG: `v${version}`, RUNNER_TEMP: base, GITHUB_REPOSITORY: 'o/r' },
    encoding: 'utf8',
  });
  return { work, run };
}

test('the web deploy takes the CLI and the engine from the checked release packages', t => {
  for (const npmName of ['hdTrans-1.2.3-wasm.tgz', 'hdtrans-1.2.3.tgz']) {
    const { work, run } = releaseFixture(t, { npmName });
    const download = run('Download the release packages');
    assert.equal(download.status, 0, download.stderr);
    assert.deepEqual(readdirSync(join(work, 'build/linux')), ['hdtrans']);
    assert.deepEqual(readdirSync(join(work, 'build/wasm')).sort(), ['hdtrans_engine.js', 'hdtrans_engine.wasm']);
    const check = run('Check the engine version');
    assert.equal(check.status, 0, check.stderr);
  }
  for (const tamper of ['hdTrans-1.2.3-linux-x64.tar.gz', 'hdTrans-1.2.3-wasm.tgz']) {
    const { work, run } = releaseFixture(t, { tamper });
    const download = run('Download the release packages');
    assert.notEqual(download.status, 0, tamper);
    assert.match(download.stderr, /does not match SHA256SUMS/);
    assert.equal(existsSync(join(work, 'build/wasm/hdtrans_engine.wasm')), false);
  }
  const { run } = releaseFixture(t, { engineVersion: '0.0.0-abcdef0' });
  assert.equal(run('Download the release packages').status, 0);
  const check = run('Check the engine version');
  assert.notEqual(check.status, 0);
  assert.match(check.stderr, /reports version 0\.0\.0-abcdef0, not 1\.2\.3/);
});

test('the web deploy builds nothing and checks the live engine against the release engine', () => {
  assert.doesNotMatch(deployWeb, /\n  build:\n|cmake|emsdk|sccache|ccache|actions\/cache|download-artifact/);
  assert.match(deployJob, /^    needs: release$/m);
  const order = ['Verify the release source', 'Download the release packages', 'Check the engine version', 'Build the site']
    .map(name => deployJob.indexOf(`- name: ${name}\n`));
  assert.deepEqual([...order].sort((a, b) => a - b), order);
  assert.ok(order[0] > 0);
  assert.match(stepScript(deployWeb, 'Build the site'), /HDTRANS_CLI=build\/linux\/hdtrans HDTRANS_WASM=build\/wasm/);
  const live = stepScript(deployWeb, 'Check the live site');
  assert.match(live, /for asset in engine wasm; do/);
  assert.match(live, /cmp "\$live\/\$asset" "build\/wasm\//);
});
