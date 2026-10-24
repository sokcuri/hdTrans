import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { buildGate, releaseBot, releaseFiles, testedRun, testedTreeArtifact, versionCommit } from './build-gate.mjs';
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
const canPackage = new Function('github', 'startsWith', `return (${foldedCondition(build.slice(build.indexOf('\n  release:\n')))});`);
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

const repo = { owner: 'sokcuri', repo: 'hdTrans' };
const repositoryId = 7;
const buildRun = fields => ({
  path: '.github/workflows/build.yml', status: 'completed', conclusion: 'success',
  repository: { id: repositoryId }, head_repository: { id: repositoryId }, ...fields,
});

// records: tree → [{ artifact, run }] of tested-tree uploads; runs: further runs by id.
function fakeGithub(records = {}, runs = {}) {
  const all = { ...runs };
  const artifacts = [];
  let id = 100;
  for (const [tree, list] of Object.entries(records)) {
    for (const { artifact = {}, run = {} } of list) {
      id += 1;
      all[id] = buildRun({ id, event: 'push', head_branch: 'develop', ...run });
      artifacts.push({
        name: testedTreeArtifact(tree), expired: false,
        workflow_run: { id, repository_id: repositoryId, head_repository_id: repositoryId }, ...artifact,
      });
    }
  }
  const listArtifactsForRepo = Symbol('listArtifactsForRepo');
  return {
    rest: {
      actions: {
        listArtifactsForRepo,
        getWorkflowRun: async ({ run_id }) => {
          if (!all[run_id]) throw Object.assign(new Error('Not Found'), { status: 404 });
          return { data: all[run_id] };
        },
      },
    },
    paginate: async (method, params) => {
      assert.equal(method, listArtifactsForRepo);
      assert.equal(params.owner, repo.owner);
      return artifacts.filter(artifact => artifact.name === params.name);
    },
  };
}

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
  const someone = { name: 'Someone', email: 'someone@example.com' };
  const commit = (message, author = someone, committer = author) => {
    git('add', '-A');
    git('-c', `user.name=${committer.name}`, '-c', `user.email=${committer.email}`, 'commit', '-q', '--allow-empty',
      `--author=${author.name} <${author.email}>`, '-m', message);
    return git('rev-parse', 'HEAD');
  };
  git('init', '-q', '-b', 'main');
  git('config', 'user.name', 'Someone');
  git('config', 'user.email', 'someone@example.com');
  write('src/a.cpp', 'int a;\n');
  for (const path of releaseFiles) write(path, '0.1.0\n');
  return { dir, git, write, commit };
}

function versionCommitFixture(t, {
  changes = releaseFiles, deletes = [], author = releaseBot, committer = author, extraMessage = '', run = {},
} = {}) {
  const fixture = gitFixture(t);
  const source = fixture.commit('fix: something (#1)');
  for (const path of changes) fixture.write(path, '0.1.1\n');
  for (const path of deletes) rmSync(join(fixture.dir, path));
  const sha = fixture.commit(`chore(main): release 0.1.1\n\nRelease-Source: ${source}\nRelease-Build: 42${extraMessage}`,
    author, committer);
  const context = {
    eventName: 'push', ref: 'refs/heads/main', sha, repo,
    payload: { before: source, after: sha, repository: { id: repositoryId } },
  };
  const github = fakeGithub({}, {
    42: buildRun({ id: 42, event: 'push', head_branch: 'main', head_sha: source, ...run }),
  });
  return { ...fixture, source, sha, context, github };
}

test('the release bot version commit push to main skips the matrix', async t => {
  const { git, source, context, github } = versionCommitFixture(t);
  assert.deepEqual(versionCommit({ payload: context.payload, sha: context.sha, git }), { source, build: 42 });
  const gate = await buildGate({ github, context, git });
  assert.equal(gate.run, false);
  assert.match(gate.reason, /version commit .* \(Build run 42\)/);
});

test('anything else about a version-like commit builds everything', async t => {
  const someone = { name: 'Someone', email: 'someone@example.com' };
  const cases = {
    'another author': versionCommitFixture(t, { author: { name: 'Someone', email: releaseBot.email } }),
    'another email': versionCommitFixture(t, { author: { name: releaseBot.name, email: 'bot@example.com' } }),
    'only the author is the bot': versionCommitFixture(t, { committer: someone }),
    'only the committer is the bot': versionCommitFixture(t, { author: someone, committer: releaseBot }),
    'a source file': versionCommitFixture(t, { changes: [...releaseFiles, 'src/a.cpp'] }),
    'a deleted release file': versionCommitFixture(t, { changes: releaseFiles.slice(1), deletes: [releaseFiles[0]] }),
    'no change': versionCommitFixture(t, { changes: [] }),
    'another message': versionCommitFixture(t, { extraMessage: '\nExtra: 1' }),
    'a failed source Build': versionCommitFixture(t, { run: { conclusion: 'failure' } }),
    'a source Build in progress': versionCommitFixture(t, { run: { status: 'in_progress', conclusion: null } }),
    'a Build of another commit': versionCommitFixture(t, { run: { head_sha: 'c'.repeat(40) } }),
    'a Build of a pull request': versionCommitFixture(t, { run: { event: 'pull_request' } }),
    'a Build of develop': versionCommitFixture(t, { run: { head_branch: 'develop' } }),
    'another workflow': versionCommitFixture(t, { run: { path: '.github/workflows/publish-npm.yml' } }),
    'a fork Build': versionCommitFixture(t, { run: { head_repository: { id: 8 } } }),
    'a run that is not there': versionCommitFixture(t, { run: { id: 43 } }),
  };
  for (const [name, { git, context, github }] of Object.entries(cases)) {
    assert.equal((await buildGate({ github, context, git })).run, true, name);
  }
  const missing = versionCommitFixture(t);
  const noRun = await buildGate({ github: fakeGithub(), context: missing.context, git: missing.git });
  assert.equal(noRun.run, true);
  assert.match(noRun.reason, /Not Found/);
  const { git, context, source, sha, github } = versionCommitFixture(t);
  const variants = {
    'a develop push': { ...context, ref: 'refs/heads/develop' },
    'a tag push': { ...context, ref: 'refs/tags/v0.1.1' },
    'a pull request': { ...context, eventName: 'pull_request' },
    'a manual run': { ...context, eventName: 'workflow_dispatch' },
    'a push of several commits': { ...context, payload: { ...context.payload, before: '1'.repeat(40) } },
    'another head': { ...context, sha: source, payload: { ...context.payload, before: source, after: source } },
  };
  for (const [name, variant] of Object.entries(variants)) {
    assert.equal((await buildGate({ github, context: variant, git })).run, true, name);
  }
  const broken = await buildGate({ github, context, git: () => { throw new Error('no git'); } });
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
  const sha = fixture.commit(`chore(main): release 0.1.1\n\nRelease-Source: ${base}\nRelease-Build: 42`, releaseBot);
  assert.equal(fixture.git('show', '-s', '--format=%P', 'HEAD').split(' ').length, 2);
  const context = {
    eventName: 'push', ref: 'refs/heads/main', sha, repo,
    payload: { before: base, after: sha, repository: { id: repositoryId } },
  };
  const github = fakeGithub({}, { 42: buildRun({ id: 42, event: 'push', head_branch: 'main', head_sha: base }) });
  assert.equal((await buildGate({ github, context, git: fixture.git })).run, true);
});

test('the gate runs first and only a definite no skips the release tools and the matrix', () => {
  const gate = build.slice(build.indexOf('\n  gate:\n'), build.indexOf('\n  release-tools:\n'));
  assert.match(gate, /fetch-depth: 2/);
  assert.match(gate, /buildGate\(\{ github, context \}\)/);
  assert.doesNotMatch(gate, /secrets\.|contents: write/);
  // Outputs are set last, so a step that fails anywhere leaves run empty.
  const script = gate.slice(gate.indexOf('script: |'));
  assert.ok(script.indexOf('core.summary') < script.indexOf("core.setOutput('run'"));
  const steps = gate.split('\n      - ').slice(1);
  assert.equal(steps.length, 2);
  const startsWith = (a, b) => a.startsWith(b);
  for (const step of steps) {
    assert.match(step, /\n        continue-on-error: true\n/);
    const runs = new Function('github', 'startsWith', `return (${step.match(/\n        if: (.*)\n/)[1]});`);
    assert.equal(runs({ event_name: 'pull_request', ref: 'refs/pull/1/merge' }, startsWith), true);
    assert.equal(runs({ event_name: 'push', ref: 'refs/heads/main' }, startsWith), true);
    assert.equal(runs({ event_name: 'push', ref: 'refs/heads/develop' }, startsWith), true);
    assert.equal(runs({ event_name: 'push', ref: 'refs/tags/v1.2.3' }, startsWith), false);
    assert.equal(runs({ event_name: 'workflow_dispatch', ref: 'refs/tags/v1.2.3' }, startsWith), false);
    assert.equal(runs({ event_name: 'workflow_dispatch', ref: 'refs/heads/main' }, startsWith), false);
  }
  for (const job of ['release-tools', 'build']) {
    const body = build.slice(build.indexOf(`\n  ${job}:\n`));
    assert.match(body, /^    needs: gate\n    if: \$\{\{ !cancelled\(\) && needs\.gate\.outputs\.run != 'false' \}\}$/m, job);
    const runs = new Function('needs', 'cancelled', `return (${body.match(/^    if: \$\{\{ (.*) \}\}$/m)[1]});`);
    for (const run of ['true', '', undefined]) assert.equal(runs({ gate: { outputs: { run } } }, () => false), true, `${job} ${run}`);
    assert.equal(runs({ gate: { outputs: { run: 'false' } } }, () => false), false);
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

function releaseFixture(t, {
  version = '1.2.3', npmName = `hdTrans-${version}-wasm.tgz`, engineVersion = version, cliVersion = version,
  tamper, missing, unsummed, registry = 'match', viewFails = false,
} = {}) {
  const base = mkdtempSync(join(tmpdir(), 'hdtrans-web-release-'));
  t.after(() => rmSync(base, { recursive: true, force: true }));
  const assets = join(base, 'assets');
  const stage = join(base, 'stage');
  const work = join(base, 'work');
  const bin = join(base, 'bin');
  for (const dir of [assets, `${stage}/hdTrans-${version}`, `${stage}/package/dist`, work, bin]) mkdirSync(dir, { recursive: true });
  writeFileSync(`${stage}/hdTrans-${version}/hdtrans`,
    `#!/bin/sh\n[ "$1 $3" = "--dicts --version" ] || exit 64\necho "hdTrans v${cliVersion} (native) / dict ${version}+abc"\n`, { mode: 0o755 });
  writeFileSync(`${stage}/hdTrans-${version}/README.txt`, 'readme\n');
  writeFileSync(`${stage}/package/dist/hdtrans_engine.js`,
    `export default async function create() { return { version: () => '${engineVersion}' }; }\n`);
  writeFileSync(`${stage}/package/dist/hdtrans_engine.wasm`, 'wasm');
  writeFileSync(`${stage}/package/dist/index.js`, '');
  const tar = (...args) => assert.equal(spawnSync('tar', args, { cwd: stage }).status, 0);
  tar('-czf', join(assets, `hdTrans-${version}-linux-x64.tar.gz`), `hdTrans-${version}`);
  tar('-czf', join(assets, npmName), 'package');
  writeFileSync(join(assets, `hdTrans-${version}-windows.zip`), 'zip');
  const integrity = registry === 'match'
    ? `sha512-${createHash('sha512').update(readFileSync(join(assets, npmName))).digest('base64')}`
    : 'sha512-AAAA';
  const sums = spawnSync('sh', ['-c', 'sha256sum *'], { cwd: assets, encoding: 'utf8' }).stdout;
  writeFileSync(join(assets, 'SHA256SUMS.txt'), sums.split('\n').filter(line => !unsummed || !line.endsWith(unsummed)).join('\n'));
  if (tamper) writeFileSync(join(assets, tamper), 'tampered');
  if (missing) rmSync(join(assets, missing));
  writeFileSync(join(bin, 'gh'), `#!/bin/bash
[ "$1 $2 $3" = "release view v${version}" ] && { ${viewFails ? 'echo "HTTP 502" >&2; exit 1' : `ls '${assets}'; exit 0`}; }
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
  const npmView = {
    match: `echo ${integrity}`, mismatch: `echo ${integrity}`,
    absent: 'echo "npm error code E404" >&2; exit 1', down: 'echo "npm error code ECONNRESET" >&2; exit 1',
  }[registry];
  writeFileSync(join(bin, 'npm'), `#!/bin/bash
[ "$1 $2 $3" = "view hdtrans@${version} dist.integrity" ] || exit 64
${npmView}
`, { mode: 0o755 });
  const run = name => spawnSync('bash', ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', stepScript(deployWeb, name)], {
    cwd: work,
    env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, TAG: `v${version}`, RUNNER_TEMP: base, GITHUB_REPOSITORY: 'o/r' },
    encoding: 'utf8',
  });
  return { work, run };
}

test('the web deploy takes the CLI and the engine from the checked release packages', t => {
  for (const [npmName, registry] of [['hdTrans-1.2.3-wasm.tgz', 'match'], ['hdtrans-1.2.3.tgz', 'match'],
    ['hdTrans-1.2.3-wasm.tgz', 'absent']]) {
    const { work, run } = releaseFixture(t, { npmName, registry });
    const download = run('Download the release packages');
    assert.equal(download.status, 0, download.stderr);
    if (registry === 'absent') assert.match(download.stdout, /not on npm/);
    assert.deepEqual(readdirSync(join(work, 'build/linux')), ['hdtrans']);
    assert.deepEqual(readdirSync(join(work, 'build/wasm')).sort(), ['hdtrans_engine.js', 'hdtrans_engine.wasm']);
    const check = run('Check the release versions');
    assert.equal(check.status, 0, check.stderr);
  }
  const refused = {
    'a tampered CLI': [{ tamper: 'hdTrans-1.2.3-linux-x64.tar.gz' }, /does not match SHA256SUMS/],
    'a tampered package': [{ tamper: 'hdTrans-1.2.3-wasm.tgz' }, /does not match SHA256SUMS/],
    'no CLI': [{ missing: 'hdTrans-1.2.3-linux-x64.tar.gz' }, /has no hdTrans-1\.2\.3-linux-x64\.tar\.gz/],
    'no package under either name': [{ missing: 'hdTrans-1.2.3-wasm.tgz' }, /has no hdtrans-1\.2\.3\.tgz/],
    'no checksum list': [{ missing: 'SHA256SUMS.txt' }, /has no SHA256SUMS\.txt/],
    'no checksum line for the CLI': [{ unsummed: 'hdTrans-1.2.3-linux-x64.tar.gz' }, /does not match SHA256SUMS/],
    'no checksum line for the package': [{ unsummed: 'hdTrans-1.2.3-wasm.tgz' }, /does not match SHA256SUMS/],
    'another package on npm': [{ registry: 'mismatch' }, /does not match hdtrans@1\.2\.3 on npm/],
    'an unreachable registry': [{ registry: 'down' }, /Cannot read hdtrans@1\.2\.3 from npm/],
    'an unreadable release': [{ viewFails: true }, /Cannot list the assets of the v1\.2\.3 release/],
  };
  for (const [name, [options, message]] of Object.entries(refused)) {
    const { work, run } = releaseFixture(t, options);
    const download = run('Download the release packages');
    assert.notEqual(download.status, 0, name);
    assert.match(download.stderr, message, name);
    assert.equal(existsSync(join(work, 'build/wasm/hdtrans_engine.wasm')), false, name);
  }
  for (const [options, message] of [[{ engineVersion: '0.0.0-abcdef0' }, /engine reports version 0\.0\.0-abcdef0, not 1\.2\.3/],
    [{ cliVersion: '0.0.0-abcdef0' }, /CLI reports hdTrans v0\.0\.0-abcdef0 \(native\) \/ dict 1\.2\.3\+abc, not version 1\.2\.3/]]) {
    const { run } = releaseFixture(t, options);
    assert.equal(run('Download the release packages').status, 0);
    const check = run('Check the release versions');
    assert.notEqual(check.status, 0);
    assert.match(check.stderr, message);
  }
});

test('the web deploy builds nothing and checks the live engine against the release engine', () => {
  assert.doesNotMatch(deployWeb, /\n  build:\n|cmake|emsdk|sccache|ccache|actions\/cache|download-artifact/);
  assert.match(deployJob, /^    needs: release$/m);
  const order = ['Verify the release source', 'Download the release packages', 'Check the release versions', 'Build the site']
    .map(name => deployJob.indexOf(`- name: ${name}\n`));
  assert.deepEqual([...order].sort((a, b) => a - b), order);
  assert.ok(order[0] > 0);
  assert.match(stepScript(deployWeb, 'Build the site'), /HDTRANS_CLI=build\/linux\/hdtrans HDTRANS_WASM=build\/wasm/);
  const live = stepScript(deployWeb, 'Check the live site');
  assert.match(live, /for asset in engine wasm; do/);
  assert.match(live, /cmp "\$live\/\$asset" "build\/wasm\//);
});

test('only a successful develop push or develop pull request Build of this repository vouches for a tree', async () => {
  const tree = 'a'.repeat(40);
  const lookup = records => testedRun({ github: fakeGithub({ [tree]: records }), repo, repositoryId, tree });
  assert.equal(await lookup([{}]), 101, 'a develop push');
  assert.equal(await lookup([{ run: { event: 'pull_request' } }]), 101, 'the develop to main pull request');
  const refused = {
    expired: { artifact: { expired: true } },
    'another repository': { artifact: { workflow_run: { id: 101, repository_id: 8, head_repository_id: 8 } } },
    'a fork pull request': { artifact: { workflow_run: { id: 101, repository_id: repositoryId, head_repository_id: 8 } } },
    'a fork run named develop': { run: { event: 'pull_request', head_repository: { id: 8 } } },
    'a feature branch pull request': { run: { event: 'pull_request', head_branch: 'feat/x' } },
    'a pull request whose build.yml changed': { run: { event: 'pull_request', head_branch: 'ci/build-change' } },
    'a main push': { run: { head_branch: 'main' } },
    'a manual run on develop': { run: { event: 'workflow_dispatch' } },
    'another workflow': { run: { path: '.github/workflows/publish-npm.yml' } },
    'a run in progress': { run: { status: 'in_progress', conclusion: null } },
    'a failed run': { run: { conclusion: 'failure' } },
    'another run id': { run: { id: 999 } },
    'another name': { artifact: { name: testedTreeArtifact('b'.repeat(40)) } },
  };
  for (const [name, record] of Object.entries(refused)) assert.equal(await lookup([record]), undefined, name);
  assert.equal(await lookup([refused['a feature branch pull request'], {}]), 102);
  assert.equal(await lookup([]), undefined);
});

function mergeFixture(t) {
  const fixture = gitFixture(t);
  fixture.write('npm/package.json', '{"name":"hdtrans","version":"0.1.0","devDependencies":{"typescript":"^7.0.2"}}\n');
  fixture.write('npm/package-lock.json', '{"name":"hdtrans","version":"0.1.0","packages":{"":{"version":"0.1.0"}}}\n');
  fixture.commit('base');
  fixture.git('checkout', '-q', '-b', 'develop');
  fixture.write('src/b.cpp', 'int b;\n');
  const head = fixture.commit('feat: b (#2)');
  fixture.git('checkout', '-q', 'main');
  fixture.write('npm/package.json', '{"name":"hdtrans","version":"0.2.0","devDependencies":{"typescript":"^7.0.2"}}\n');
  fixture.write('npm/package-lock.json', '{"name":"hdtrans","version":"0.2.0","packages":{"":{"version":"0.2.0"}}}\n');
  fixture.write('CHANGELOG.md', '## 0.2.0\n');
  const versioned = fixture.commit('chore(main): release 0.2.0', releaseBot);
  const merge = (change = () => {}) => {
    fixture.git('checkout', '-q', '--detach', versioned);
    fixture.git('merge', '-q', '--no-ff', '--no-commit', head);
    change();
    return fixture.commit('Merge develop');
  };
  const prContext = ({ sha, base = 'main', ref = 'develop', owner = 'sokcuri/hdTrans', headSha = head } = {}) => ({
    eventName: 'pull_request', ref: 'refs/pull/3/merge', sha, repo,
    payload: {
      repository: { id: repositoryId },
      pull_request: {
        base: { ref: base, repo: { full_name: 'sokcuri/hdTrans' } },
        head: { ref, sha: headSha, repo: { full_name: owner } },
      },
    },
  });
  return { ...fixture, head, versioned, headTree: fixture.git('rev-parse', `${head}^{tree}`), merge, prContext };
}

test('a main push skips the matrix for a tree that a successful Build tested', async t => {
  const { git, merge, headTree } = mergeFixture(t);
  const sha = merge();
  const tree = git('rev-parse', 'HEAD^{tree}');
  const context = { eventName: 'push', ref: 'refs/heads/main', sha, repo, payload: { before: '0'.repeat(40), after: sha, repository: { id: repositoryId } } };
  const skipped = await buildGate({ github: fakeGithub({ [tree]: [{ run: { event: 'pull_request' } }] }), context, git });
  assert.equal(skipped.run, false);
  assert.equal(skipped.tree, tree);
  assert.equal(skipped.testedBy, undefined);
  assert.match(skipped.reason, /passed in Build run 101/);
  assert.equal((await buildGate({ github: fakeGithub({ [headTree]: [{}] }), context, git })).run, true);
  assert.equal((await buildGate({ github: fakeGithub({ [tree]: [{ run: { conclusion: 'failure' } }] }), context, git })).run, true);
  assert.equal((await buildGate({ github: fakeGithub({ [tree]: [{}] }), context: { ...context, ref: 'refs/heads/develop' }, git })).run, true);
  const noRepository = { ...context, payload: { ...context.payload, repository: undefined } };
  assert.equal((await buildGate({ github: fakeGithub({ [tree]: [{}] }), context: noRepository, git })).run, true);
  const failing = { rest: { actions: {} }, paginate: async () => { throw new Error('API down'); } };
  const broken = await buildGate({ github: failing, context, git });
  assert.equal(broken.run, true);
  assert.equal(broken.tree, tree);
  assert.match(broken.reason, /API down/);
});

test('a develop to main pull request that adds only release versions reuses the develop record', async t => {
  const { git, write, commit, merge, prContext, head, headTree, versioned } = mergeFixture(t);
  const github = fakeGithub({ [headTree]: [{}] });
  const sha = merge();
  const skipped = await buildGate({ github, context: prContext({ sha }), git });
  assert.equal(skipped.run, false);
  assert.equal(skipped.testedBy, 101);
  assert.equal(skipped.tree, git('rev-parse', 'HEAD^{tree}'));
  assert.notEqual(skipped.tree, headTree);
  for (const [name, context] of Object.entries({
    'a feature branch': prContext({ sha, ref: 'feature' }),
    'a pull request to develop': prContext({ sha, base: 'develop' }),
    'a fork': prContext({ sha, owner: 'someone/hdTrans' }),
  })) {
    assert.equal((await buildGate({ github, context, git })).run, true, name);
  }
  assert.equal((await buildGate({ github: fakeGithub(), context: prContext({ sha }), git })).run, true, 'no record');
  const changes = {
    'a source file only on main': () => write('src/hotfix.cpp', 'int h;\n'),
    'a release file deleted on main': () => git('rm', '-q', 'release-version.txt'),
    'a dependency only on main': () => write('npm/package.json',
      '{"name":"hdtrans","version":"0.2.0","devDependencies":{"typescript":"^7.0.3"}}\n'),
    'a lock entry only on main': () => write('npm/package-lock.json',
      '{"name":"hdtrans","version":"0.2.0","packages":{"":{"version":"0.2.0","license":"x"}}}\n'),
  };
  for (const [name, change] of Object.entries(changes)) {
    const changed = merge(change);
    assert.equal((await buildGate({ github, context: prContext({ sha: changed }), git })).run, true, name);
  }
  assert.equal((await buildGate({ github, context: prContext({ sha, headSha: versioned }), git })).run, true,
    'a second parent that is not the pull request head');
  git('checkout', '-q', '--detach', head);
  write('CHANGELOG.md', '## 0.2.0\n');
  const linear = commit('one parent');
  assert.equal((await buildGate({ github, context: prContext({ sha: linear }), git })).run, true, 'a merge ref with one parent');
});

test('the record job runs only after a passing matrix or a matched release-files-only tree', () => {
  const job = build.slice(build.indexOf('\n  tested:\n'), build.indexOf('\n  release:\n'));
  const tested = new Function('github', 'needs', 'startsWith', 'cancelled',
    `return (${foldedCondition(job).replaceAll('needs.release-tools', "needs['release-tools']")});`);
  const startsWith = (a, b) => a.startsWith(b);
  const needs = (run, result = 'success', tested_by = '', tree = 'a'.repeat(40)) => ({
    gate: { outputs: { run, tree, tested_by } }, 'release-tools': { result }, build: { result },
  });
  const pr = (owner = 'sokcuri/hdTrans', head_ref = 'develop') => ({
    event_name: 'pull_request', ref: 'refs/pull/1/merge', repository: 'sokcuri/hdTrans', head_ref,
    event: { pull_request: { head: { repo: { full_name: owner } } } },
  });
  const push = ref => ({ event_name: 'push', ref, repository: 'sokcuri/hdTrans', event: {} });
  const check = (github, n, cancelled = false) => tested(github, n, startsWith, () => cancelled);
  assert.equal(check(pr(), needs('true')), true);
  assert.equal(check(pr('someone/hdTrans'), needs('true')), false, 'a fork');
  assert.equal(check(pr('sokcuri/hdTrans', 'feat/x'), needs('true')), false, 'a feature branch');
  assert.equal(check(push('refs/heads/develop'), needs('true')), true);
  assert.equal(check(push('refs/heads/main'), needs('true')), false);
  assert.equal(check(push('refs/tags/v1.2.3'), needs('true')), false);
  assert.equal(check({ ...push('refs/heads/develop'), event_name: 'workflow_dispatch' }, needs('true')), false);
  assert.equal(check(pr(), needs('true', 'failure')), false);
  assert.equal(check(pr(), needs('true', 'skipped')), false);
  assert.equal(check(pr(), needs('true', 'success', '', '')), false);
  assert.equal(check(pr(), needs('', 'success', '', '')), false);
  assert.equal(check(pr(), needs('true'), true), false);
  assert.equal(check(pr(), needs('false', 'skipped', '101')), true);
  assert.equal(check(pr('someone/hdTrans'), needs('false', 'skipped', '101')), false);
  assert.equal(check(pr(), needs('false', 'skipped')), false);
  assert.equal(check(push('refs/heads/main'), needs('false', 'skipped')), false);
  assert.match(job, /name: tested-tree-\$\{\{ needs\.gate\.outputs\.tree \}\}/);
  assert.match(job, /retention-days: 30/);
  assert.match(job, /\n    permissions: \{\}\n/);
  assert.doesNotMatch(job, /secrets\./);
  const gate = build.slice(build.indexOf('\n  gate:\n'), build.indexOf('\n  release-tools:\n'));
  assert.match(gate, /permissions:\n      contents: read\n      actions: read\n/);
  assert.match(publisher, /  build:\n    needs: validate\n    permissions:\n      contents: write\n      actions: read\n/);
});

test('branch pushes build only main and develop; superseded pull request runs are cancelled', () => {
  const triggers = build.slice(build.indexOf('\non:\n'), build.indexOf('\npermissions:'));
  assert.match(triggers, /  push:\n    branches: \[main, develop\]\n  pull_request:\n/);
  assert.match(build, /concurrency:\n  group: build-\$\{\{ github\.workflow \}\}-\$\{\{ github\.ref \}\}\n  cancel-in-progress: \$\{\{ github\.event_name == 'pull_request' \}\}\n/);
});

const fuzzJob = build.slice(build.indexOf('\n  fuzz:\n'), build.indexOf('\n  release:\n'));
function fuzzStep(name) {
  const start = fuzzJob.indexOf(`      - name: ${name}\n`);
  assert.notEqual(start, -1, name);
  const end = fuzzJob.indexOf('\n      - name:', start + 1);
  return fuzzJob.slice(start, end === -1 ? undefined : end);
}
const fuzzBlocking = new Function('github', 'startsWith',
  `return (${fuzzJob.match(/^      FUZZ_BLOCKING: \$\{\{ (.*) \}\}$/m)[1]});`);

test('the random fuzz run fails branch pushes and only warns on pull requests, tags and manual runs', () => {
  const startsWith = (s, prefix) => s.startsWith(prefix);
  // Publish npm calls Build on the tag push, so a tag build has the push event and the tag ref.
  for (const [event_name, ref, blocking] of [
    ['push', 'refs/heads/develop', true],
    ['push', 'refs/heads/main', true],
    ['pull_request', 'refs/pull/7/merge', false],
    ['push', 'refs/tags/v1.2.3', false],
    ['workflow_dispatch', 'refs/tags/v1.2.3', false],
    ['workflow_dispatch', 'refs/heads/develop', false],
  ]) {
    assert.equal(fuzzBlocking({ event_name, ref }, startsWith), blocking, `${event_name} ${ref}`);
  }
  assert.match(fuzzStep('Fuzz'), /\n        continue-on-error: \$\{\{ env\.FUZZ_BLOCKING != 'true' \}\}\n/);
  assert.doesNotMatch(fuzzStep('Replay the seeds and regression inputs'), /continue-on-error/);
  assert.match(fuzzStep('Upload the failing inputs and fuzzer logs'), /steps\.fuzz\.outcome == 'failure'/);
});

for (const [status, blocking] of [[0, 'true'], [0, 'false'], [1, 'true'], [1, 'false']]) {
  test(`the fuzz step exits with the run's status and warns only when it does not block: ${status}, ${blocking}`, t => {
    const dir = mkdtempSync(join(tmpdir(), 'hdtrans-ci-fuzz-'));
    t.after(() => rmSync(dir, { recursive: true, force: true }));
    mkdirSync(join(dir, 'tools'));
    mkdirSync(join(dir, 'bin'));
    writeFileSync(join(dir, 'bin/timeout'), '#!/bin/sh\nshift\nexec "$@"\n', { mode: 0o700 });
    writeFileSync(join(dir, 'tools/fuzz.py'), `import os, sys
assert sys.argv[1:3] == ["build/linux-fuzz/hdtrans_fuzz", os.environ["RUNNER_TEMP"] + "/dicts.hdsnap"]
assert "--seed" in sys.argv and "--out" in sys.argv
print("fuzz: fixture")
sys.exit(int(os.environ["FIXTURE_STATUS"]))
`);
    const script = fuzzStep('Fuzz').match(/        run: \|\n([\s\S]*)$/)[1].replace(/^ {10}/gm, '');
    const summary = join(dir, 'summary');
    const result = spawnSync('bash', ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script], {
      cwd: dir,
      env: {
        ...process.env, PATH: `${join(dir, 'bin')}:${process.env.PATH}`, RUNNER_TEMP: dir, GITHUB_STEP_SUMMARY: summary,
        GITHUB_RUN_NUMBER: '5', FUZZ_BLOCKING: blocking, FIXTURE_STATUS: String(status),
      },
      encoding: 'utf8',
    });
    assert.ifError(result.error);
    assert.equal(result.status, status, result.stderr);
    assert.match(readFileSync(summary, 'utf8'), /### Fuzz\n```text\nfuzz: fixture\n```/);
    const warned = /::warning title=Fuzz::/.test(result.stdout);
    assert.equal(warned, status !== 0 && blocking !== 'true');
  });
}
