import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { setLogger } from 'release-please';
import {
  applyCandidate, buildCandidate, commitMessage, mergedPromotion, promotionVersion, readVersion, releaseMain, releaseNotes, validateBuild,
} from './release.mjs';

setLogger(Object.fromEntries(['info', 'warn', 'debug', 'trace', 'error'].map(name => [name, () => {}])));
const config = JSON.parse(readFileSync(fileURLToPath(new URL('../../release-please-config.json', import.meta.url)), 'utf8'));
const repo = { owner: 'example', repo: 'hdTrans' };
const source = 'a'.repeat(40);
const bot = 'b'.repeat(40);
const previous = 'c'.repeat(40);
const newer = 'd'.repeat(40);
const annotation = 'e'.repeat(40);
const runId = '123';
const promotion = {
  number: 7, title: 'fix: promote the release', body: '', merged_at: '2026-10-19T00:00:00Z',
  merge_commit_sha: source, base: { ref: 'main', repo: { full_name: 'example/hdTrans' } },
};
const goodRun = {
  status: 'completed', conclusion: 'success', event: 'push', head_branch: 'main',
  path: '.github/workflows/build.yml', workflow_id: 42, head_sha: source,
  head_repository: { full_name: 'example/hdTrans' },
};

const publishWorkflow = readFileSync(fileURLToPath(new URL('../../.github/workflows/publish-npm.yml', import.meta.url)), 'utf8');
const dispatchScript = publishWorkflow.match(/      - name: Start npm publication after Build completes\n[\s\S]*?          script: \|\n([\s\S]*?)\n  publish:/)?.[1];
assert.ok(dispatchScript, 'The completed-Build dispatcher script is present.');
const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
const runDispatch = new AsyncFunction('github', 'context', 'core', dispatchScript.replace(/^ {12}/gm, ''));

async function dispatchFixture(overrides = {}, jobs = [{ name: 'release / release', conclusion: 'success' }]) {
  const calls = [];
  const run = { ...goodRun, id: Number(runId), head_branch: 'v0.2.0', ...overrides };
  const listJobs = () => {};
  await runDispatch({
    rest: { actions: { listJobsForWorkflowRun: listJobs, createWorkflowDispatch: async args => calls.push(args) } },
    paginate: async (method, args) => {
      assert.equal(method, listJobs);
      assert.deepEqual(args, { ...repo, run_id: Number(runId), filter: 'latest', per_page: 100 });
      return jobs;
    },
  }, { repo, payload: { workflow_run: run } }, { info() {} });
  return calls;
}

for (const event of ['push', 'workflow_dispatch']) {
  test(`completed stable ${event} Build dispatches the immutable tag`, async () => {
    assert.deepEqual(await dispatchFixture({ event }), [{
      ...repo, workflow_id: 'publish-npm.yml', ref: 'v0.2.0', inputs: { build_run_id: runId },
    }]);
  });
}

for (const [name, overrides] of [
  ['running Build', { status: 'in_progress' }],
  ['failed Build', { conclusion: 'failure' }],
  ['other workflow', { path: '.github/workflows/probe.yml' }],
  ['pull request', { event: 'pull_request' }],
  ['foreign repository', { head_repository: { full_name: 'other/hdTrans' } }],
  ['main branch', { head_branch: 'main' }],
  ['nightly', { head_branch: 'nightly' }],
  ['prerelease', { head_branch: 'v0.2.0-rc.1' }],
]) {
  test(`npm dispatcher ignores ${name}`, async () => {
    assert.deepEqual(await dispatchFixture(overrides), []);
  });
}

for (const conclusion of ['skipped', 'failure', null]) {
  test(`npm dispatcher ignores ${conclusion ?? 'pending'} release job`, async () => {
    assert.deepEqual(await dispatchFixture({}, [{ name: 'release / release', conclusion }]), []);
  });
}

test('npm dispatcher does not publish a dry-run with only packaged artifacts', async () => {
  assert.deepEqual(await dispatchFixture({}, [{ name: 'release / package', conclusion: 'success' }]), []);
});

test('npm publication runs only after Build completion or an explicit retry', () => {
  const buildWorkflow = readFileSync(fileURLToPath(new URL('../../.github/workflows/build.yml', import.meta.url)), 'utf8');
  assert.doesNotMatch(buildWorkflow, /createWorkflowDispatch|  publish-npm:/);
  assert.match(publishWorkflow, /workflow_run:\n    workflows: \[Build\]\n    types: \[completed\]/);
  assert.match(publishWorkflow, /  publish:\n    if: github.event_name == 'workflow_dispatch'/);
});

function fixture(messages, baseline, persisted = baseline ?? '0.1.0') {
  const files = {
    'release-please-config.json': JSON.stringify(config),
    '.release-please-manifest.json': baseline ? JSON.stringify({ '.': persisted }) : '{}',
    'release-version.txt': `${persisted}\n`,
    'npm/package.json': JSON.stringify({ name: 'hdtrans', version: persisted }),
    'npm/package-lock.json': JSON.stringify({ name: 'hdtrans', version: persisted, lockfileVersion: 3,
      packages: { '': { name: 'hdtrans', version: persisted }, 'node_modules/example': { version: '9.8.7' } } }),
  };
  const scm = {
    repository: { ...repo, defaultBranch: 'main' },
    getFileJson: async path => JSON.parse(files[path]),
    getFileContentsOnBranch: async path => ({ parsedContent: files[path], content: Buffer.from(files[path] ?? '').toString('base64'), sha: source }),
    async *releaseIterator() {
      if (baseline) yield { tagName: `v${baseline}`, name: `v${baseline}`, sha: previous, notes: '' };
    },
    async *tagIterator() {
      if (baseline) yield { name: `v${baseline}`, sha: previous };
    },
    async *mergeCommitIterator() {
      for (const [index, message] of messages.entries()) {
        yield { sha: index === 0 ? source : String(index).padStart(40, '0'), message, files: ['src/main.cpp'] };
      }
      if (baseline) yield { sha: previous, message: `chore: release ${baseline}`, files: [] };
    },
  };
  return { files, scm };
}

function tempRoot(t, files) {
  const root = mkdtempSync(join(tmpdir(), 'hdtrans-release-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  for (const [path, content] of Object.entries(files)) {
    mkdirSync(dirname(join(root, path)), { recursive: true });
    writeFileSync(join(root, path), content);
  }
  return root;
}

for (const [message, expected] of [
  ['fix: correct output', '0.1.2'], ['perf: load faster', '0.1.2'],
  ['revert: restore old behavior', '0.1.2'], ['feat: add snapshots', '0.2.0'],
  ['feat!: change the API', '1.0.0'],
  ['fix: change defaults\n\nBREAKING CHANGE: callers must adapt', '1.0.0'],
  ['docs: explain snapshots', undefined], ['ci: tune caching', undefined],
]) {
  test(`release-please handles ${message.split('\n')[0]}`, async () => {
    const { scm } = fixture([message], '0.1.1');
    assert.equal((await buildCandidate(scm, '0.1.1'))?.version.toString(), expected);
  });
}

for (const [title, expected] of [
  ['fix: promote', '0.1.1'], ['perf: promote', '0.1.1'], ['revert: promote', '0.1.1'],
  ['feat: promote', '0.2.0'], ['feat!: promote', '1.0.0'],
  ['docs: promote', undefined], ['ci: promote', undefined], ['chore: promote', undefined],
  ['docs!: promote', '1.0.0'],
]) {
  test(`main PR title ${title} controls the first bump from persisted 0.1.0`, () => {
    assert.equal(promotionVersion({ ...promotion, title }, '0.1.0'), expected);
  });
}

test('a promotion breaking footer bumps major but a Release-As footer cannot override the title', () => {
  assert.equal(promotionVersion({ ...promotion, body: 'BREAKING CHANGE: callers must adapt' }, '0.1.1'), '1.0.0');
  assert.equal(promotionVersion({ ...promotion, body: 'Release-As: 9.0.0' }, '0.1.1'), '0.1.2');
  assert.equal(promotionVersion({ ...promotion, body: 'feat!: an internal commit title' }, '0.1.1'), '0.1.2');
});

test('promotion title overrides internal breaking changes while retaining them in the changelog', async () => {
  const { scm } = fixture(['feat!: add the new API', 'fix: correct output']);
  const version = promotionVersion(promotion, '0.1.0');
  const candidate = await buildCandidate(scm, undefined, version, promotion);
  assert.equal(candidate.version.toString(), '0.1.1');
  assert.match(candidate.body.releaseData[0].notes, /add the new API/);
});

test('a feature promotion can release CI-only commits through the standard changelog updater', async () => {
  const { scm } = fixture(['ci: update the workflow']);
  const pr = { ...promotion, title: 'feat: standalone release' };
  const candidate = await buildCandidate(scm, undefined, promotionVersion(pr, '0.1.0'), pr);
  assert.equal(candidate.version.toString(), '0.2.0');
  assert.match(candidate.body.releaseData[0].notes, /standalone release/);
});

test('main PR lookup rejects missing, unmerged, wrong-commit and ambiguous associations', () => {
  assert.equal(mergedPromotion([promotion], repo, source), promotion);
  for (const prs of [[], [{ ...promotion, merged_at: null }], [{ ...promotion, merge_commit_sha: previous }],
    [{ ...promotion, base: { ref: 'develop', repo: promotion.base.repo } }],
    [{ ...promotion, base: { ref: 'main', repo: { full_name: 'fork/hdTrans' } } }], [promotion, promotion]]) {
    assert.throws(() => mergedPromotion(prs, repo, source), /exactly one merged main PR/);
  }
});

test('candidate persists changelog and every version without touching dependency versions', async t => {
  const { files, scm } = fixture(['fix: correct output'], '0.1.1');
  const root = tempRoot(t, files);
  const candidate = await buildCandidate(scm, '0.1.1');
  assert.equal(applyCandidate(root, candidate), '0.1.2');
  assert.equal(readVersion(root), '0.1.2');
  const lock = JSON.parse(readFileSync(join(root, 'npm/package-lock.json'), 'utf8'));
  assert.equal(lock.packages['node_modules/example'].version, '9.8.7');
  assert.match(releaseNotes(root, '0.1.2'), /correct output/);
  assert.throws(() => applyCandidate(root, { updates: [{ path: 'src/main.cpp' }] }), /only the root release files/);
});

test('regenerating an untagged version replaces its changelog entry instead of duplicating it', async t => {
  const { files, scm } = fixture(['fix: correct output'], '0.1.1');
  const root = tempRoot(t, files);
  const candidate = await buildCandidate(scm, '0.1.1', '0.1.2', promotion);
  applyCandidate(root, candidate);
  applyCandidate(root, candidate);
  const changelog = readFileSync(join(root, 'CHANGELOG.md'), 'utf8');
  assert.equal((changelog.match(/^## .*0\.1\.2/gm) ?? []).length, 1);
  assert.equal((changelog.match(/^# Changelog/gm) ?? []).length, 1);
});

test('Build validation rejects failures, forks, wrong workflows, branches and events', () => {
  assert.equal(validateBuild(goodRun, repo, 42), source);
  for (const patch of [
    { conclusion: 'failure' }, { status: 'in_progress' }, { event: 'pull_request' },
    { event: 'workflow_dispatch' }, { head_branch: 'develop' }, { workflow_id: 43 },
    { path: '.github/workflows/other.yml' }, { head_sha: 'bad' },
    { head_repository: { full_name: 'fork/hdTrans' } }, { head_repository: null },
  ]) assert.throws(() => validateBuild({ ...goodRun, ...patch }, repo, 42), /successful Build/);
});

async function harness(t, options = {}) {
  const { files, scm } = fixture(['feat: add snapshots']);
  const root = tempRoot(t, files);
  const pr = { ...promotion, ...options.promotion };
  const candidate = await buildCandidate(scm, undefined, promotionVersion(pr, '0.1.0') ?? '0.1.1', pr);
  let head = options.resume ? bot : source;
  let main = options.advanced ? newer : head;
  let checks = 0;
  let tag = options.existingTag;
  let published;
  let releaseFailed = false;
  const calls = [];
  if (options.resume) applyCandidate(root, candidate);
  const missing = () => { throw Object.assign(new Error('Not found'), { status: 404 }); };
  const github = { paginate: async () => options.promotions ?? [pr], rest: {
    actions: {
      getWorkflow: async () => ({ data: { id: 42 } }),
      getWorkflowRun: async () => ({ data: { ...goodRun, ...options.run } }),
      listWorkflowRuns: async () => ({ data: { workflow_runs: options.builds ?? [] } }),
      createWorkflowDispatch: async args => { calls.push(['dispatch', args]); },
    },
    git: {
      getRef: async ({ ref }) => {
        if (ref === 'heads/main') {
          checks++;
          if (checks === options.advanceOnCheck) main = newer;
          return { data: { ref: 'refs/heads/main', object: { type: 'commit', sha: main } } };
        }
        if (!tag) return missing();
        return { data: { ref: `refs/${ref}`, object: tag } };
      },
      getTag: async args => {
        assert.equal(args.tag_sha, annotation);
        return { data: { tag: 'v0.1.1', sha: annotation, message: 'Release v0.1.1',
          object: { type: 'commit', sha: options.taggedCommit ?? bot } } };
      },
      createTag: async args => {
        calls.push(['tag-object', args]);
        return { data: { tag: args.tag, sha: annotation, message: args.message,
          object: { type: args.type, sha: args.object } } };
      },
      createRef: async args => { calls.push(['tag', args]); tag = { type: 'tag', sha: args.sha }; },
    },
    repos: {
      listPullRequestsAssociatedWithCommit: () => {},
      getReleaseByTag: async () => published ? { data: published } : missing(),
      createRelease: async args => {
        if (options.releaseFailure && !releaseFailed) {
          releaseFailed = true;
          throw new Error('API unavailable');
        }
        calls.push(['release', args]); published = args;
      },
    },
  } };
  const git = (...args) => {
    if (args[0] === 'rev-parse') return head;
    if (args[0] === 'status') return '';
    if (args[0] === 'tag') return '';
    if (args[0] === 'show') return args.includes('--format=%P') ? source : commitMessage('0.1.1', source, runId);
    if (args[0] === 'diff-tree') return options.badResume ? 'src/main.cpp' : 'CHANGELOG.md\nnpm/package.json';
    if (args[0] === 'add') { calls.push(['add', args]); return ''; }
    if (args.includes('commit')) { calls.push(['commit', args]); head = bot; return ''; }
    if (args[0] === 'push') {
      calls.push(['push', args]);
      if (options.pushRace) { main = newer; throw new Error('non-fast-forward'); }
      main = head;
      return '';
    }
    throw new Error(`Unexpected git command ${args.join(' ')}`);
  };
  const run = () => releaseMain({ github, repo, runId, root, git,
    candidateBuilder: async (baseline, version) => {
      assert.equal(baseline, undefined);
      assert.equal(version, candidate.version.toString());
      return candidate;
    }, log: () => {} });
  return { run, calls, root };
}

test('release uses only a normal main push, then tag, Release and explicit non-dry-run Build', async t => {
  const h = await harness(t);
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag', 'release', 'dispatch']);
  assert.deepEqual(h.calls.find(call => call[0] === 'push')[1], ['push', 'origin', 'HEAD:refs/heads/main']);
  assert.match(h.calls.find(call => call[0] === 'commit')[1].at(-1), /\[skip ci\]/);
  assert.deepEqual(h.calls.find(call => call[0] === 'tag-object')[1], {
    ...repo, tag: 'v0.1.1', message: 'Release v0.1.1', object: bot, type: 'commit',
  });
  assert.equal(h.calls.find(call => call[0] === 'tag')[1].sha, annotation);
  assert.deepEqual(h.calls.at(-1)[1], { ...repo, workflow_id: 'build.yml', ref: 'v0.1.1', inputs: { dry_run: 'false' } });
  assert.equal(readVersion(h.root), '0.1.1');
});

for (const options of [{ advanced: true }, { advanceOnCheck: 2 }, { advanceOnCheck: 3 }, { pushRace: true }, { promotion: { title: 'docs: promote docs' } }]) {
  test(`no release for stale main or no changes: ${JSON.stringify(options)}`, async t => {
    const h = await harness(t, options);
    await h.run();
    assert.ok(!h.calls.some(call => ['tag', 'release', 'dispatch'].includes(call[0])));
    assert.ok(!h.calls.some(call => call[1].includes?.('--force')));
  });
}

test('manual retry resumes only the validated Build\'s exact version-only bot child', async t => {
  const h = await harness(t, { resume: true });
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['tag-object', 'tag', 'release', 'dispatch']);
  const bad = await harness(t, { resume: true, badResume: true });
  await bad.run();
  assert.deepEqual(bad.calls, []);
});

test('retry after an API failure never pushes or retags again', async t => {
  const h = await harness(t, { releaseFailure: true });
  await assert.rejects(h.run(), /API unavailable/);
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag']);
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag', 'release', 'dispatch']);
});

for (const type of ['commit', 'tag']) {
  test(`retry accepts an existing ${type === 'tag' ? 'annotated' : 'lightweight'} tag targeting the release commit`, async t => {
    const h = await harness(t, { resume: true, existingTag: { type, sha: type === 'tag' ? annotation : bot } });
    await h.run();
    assert.deepEqual(h.calls.map(call => call[0]), ['release', 'dispatch']);
  });

  test(`retry rejects an existing ${type === 'tag' ? 'annotated' : 'lightweight'} tag targeting a different commit`, async t => {
    const h = await harness(t, { resume: true, existingTag: { type, sha: type === 'tag' ? annotation : newer }, taggedCommit: newer });
    await assert.rejects(h.run(), /already targets another commit/);
    assert.deepEqual(h.calls, []);
  });
}

test('main advancing after annotation creation leaves the tag ref unpublished', async t => {
  const h = await harness(t, { advanceOnCheck: 5 });
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object']);
});

test('a successful dry-run tag Build does not suppress the publishing Build', async t => {
  const h = await harness(t, { resume: true, builds: [{
    head_sha: bot, head_branch: 'v0.1.1', display_title: 'Build', status: 'completed', conclusion: 'success',
  }] });
  await h.run();
  assert.ok(h.calls.some(call => call[0] === 'dispatch'));
});

test('an already successful stable Build is not dispatched twice', async t => {
  const h = await harness(t, { resume: true, builds: [{
    head_sha: bot, head_branch: 'v0.1.1', display_title: 'Release v0.1.1', status: 'completed', conclusion: 'success',
  }] });
  await h.run();
  assert.ok(!h.calls.some(call => call[0] === 'dispatch'));
});
