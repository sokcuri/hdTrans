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

test('npm publication has no Build-completion trigger or dispatch job', () => {
  assert.doesNotMatch(publishWorkflow, /workflow_run:|  dispatch:|createWorkflowDispatch|Start npm publication after Build completes/);
  assert.match(publishWorkflow, /workflow_dispatch:/);
  const buildWorkflow = readFileSync(fileURLToPath(new URL('../../.github/workflows/build.yml', import.meta.url)), 'utf8');
  assert.doesNotMatch(buildWorkflow, /createWorkflowDispatch|  publish-npm:/);
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

for (const [instant, expected] of [
  ['2026-10-26T20:42:00Z', '2026-10-27'],
  ['2026-10-26T14:59:59.999Z', '2026-10-26'],
  ['2026-10-26T15:00:00Z', '2026-10-27'],
  ['2026-10-26T23:59:59.999Z', '2026-10-27'],
  ['2026-10-27T00:00:00Z', '2026-10-27'],
  ['2026-12-31T15:00:00Z', '2027-01-01'],
  ['2024-02-28T15:00:00Z', '2024-02-29'],
  ['2024-02-29T15:00:00Z', '2024-03-01'],
]) {
  test(`release date uses the Seoul calendar at ${instant}`, async () => {
    const { scm } = fixture(['fix: correct output'], '0.6.2');
    const candidate = await buildCandidate(scm, '0.6.2', undefined, undefined, new Date(instant));
    const notes = candidate.body.releaseData[0].notes;
    const changelog = candidate.updates.find(update => update.path === 'CHANGELOG.md').updater;
    assert.equal(changelog.changelogEntry, notes);
    const heading = notes.split('\n')[0];
    assert.match(heading, /^#{1,3} \[0\.6\.3\]\([^\n]+\) \(\d{4}-\d{2}-\d{2}\)$/);
    assert.ok(heading.endsWith(`(${expected})`), heading);
    assert.ok(heading.includes('/compare/v0.6.2...v0.6.3'), heading);
  });
}

test('release date preserves prior entries byte-for-byte and matches published notes', async t => {
  const { files, scm } = fixture(['fix: preserve the body date (2026-10-26)'], '0.6.2');
  const previousEntries = '## [0.6.2](https://example.invalid/compare) (2026-10-26)\n\n' +
    '### Bug Fixes\n\n* Existing text at 2026-10-26T20:42Z.\n\n' +
    '## 0.6.1 (2026-10-19)\n\n* Existing older text.\n';
  files['CHANGELOG.md'] = '# Changelog\n\n' + previousEntries;
  const root = tempRoot(t, files);
  const candidate = await buildCandidate(scm, '0.6.2', undefined, undefined, new Date('2026-10-26T20:42Z'));
  const notes = candidate.body.releaseData[0].notes;
  assert.ok(notes.split('\n')[0].endsWith('(2026-10-27)'));
  assert.match(notes, /preserve the body date \(2026-10-26\)/);
  assert.equal(applyCandidate(root, candidate), '0.6.3');
  const changelog = readFileSync(join(root, 'CHANGELOG.md'), 'utf8');
  assert.equal(changelog.slice(changelog.indexOf(previousEntries.split('\n')[0])), previousEntries);
  assert.equal(releaseNotes(root, '0.6.3'), notes);
});

test('release date retains the first-release heading format without a comparison link', async () => {
  const { scm } = fixture(['fix: first release']);
  const candidate = await buildCandidate(scm, undefined, '0.6.3', promotion, new Date('2026-10-26T20:42Z'));
  const heading = candidate.body.releaseData[0].notes.split('\n')[0];
  assert.match(heading, /^#{1,3} 0\.6\.3 \(2026-10-27\)$/);
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
  let localTag = options.localTag;
  let published = options.existingRelease;
  let failed = false;
  const calls = [];
  const logs = [];
  const lookups = [];
  if (options.resume) applyCandidate(root, candidate);
  const missing = () => { throw Object.assign(new Error('Not found'), { status: 404 }); };
  const github = { paginate: async (method, args) => {
    if (method === github.rest.repos.listReleases) return published ? [published] : [];
    assert.equal(method, github.rest.repos.listPullRequestsAssociatedWithCommit);
    lookups.push(args);
    return options.promotions ?? [pr];
  }, rest: {
    actions: {
      getWorkflow: async () => ({ data: { id: 42 } }),
      getWorkflowRun: async () => ({ data: { ...goodRun, ...options.run } }),
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
    },
    repos: {
      listPullRequestsAssociatedWithCommit: () => {},
      listReleases: () => {},
      getReleaseByTag: async () => published && !options.draftHidden ? { data: published } : missing(),
      createRelease: async args => {
        if (options.releaseFailure && !failed) {
          failed = true;
          throw new Error('API unavailable');
        }
        calls.push(['release', args]); published = { ...args, id: 12 };
        if (options.advanceAfterRelease) main = newer;
      },
    },
  } };
  const git = (...args) => {
    if (args[0] === 'rev-parse') return args[1] === 'HEAD' ? head : options.localTaggedCommit ?? bot;
    if (args[0] === 'status') return '';
    if (args[0] === 'tag') return args[1] === '--list' && localTag ? 'v0.1.1' : '';
    if (args[0] === 'cat-file') return options.localTagType ?? 'tag';
    if (args[0] === 'show') {
      if (args.includes('--format=%P')) return source;
      const message = commitMessage('0.1.1', source, options.wrongBuild ? '999' : runId);
      return options.skipMessage ? message.replace('\n\n', ' [skip ci]\n\n') : message;
    }
    if (args[0] === 'diff-tree') return options.badResume ? 'src/main.cpp' : 'CHANGELOG.md\nnpm/package.json';
    if (args[0] === 'add') { calls.push(['add', args]); return ''; }
    if (args.includes('commit')) { calls.push(['commit', args]); head = bot; return ''; }
    if (args.includes('tag')) { calls.push(['tag-object', args]); localTag = true; return ''; }
    if (args[0] === 'push') {
      const tagging = args[2].startsWith('refs/tags/');
      calls.push([tagging ? 'tag-push' : 'push', args]);
      if (tagging) {
        if (options.tagPushFailure && !failed) { failed = true; throw new Error('SSH unavailable'); }
        if (options.tagPushRace) {
          tag = { type: 'commit', sha: options.tagPushRace === 'owned' ? bot : newer };
          throw new Error('tag already exists');
        }
        tag = { type: 'tag', sha: annotation };
        if (options.advanceAfterTag) main = newer;
      } else {
        if (options.pushRace) { main = newer; throw new Error('non-fast-forward'); }
        main = head;
      }
      return '';
    }
    throw new Error(`Unexpected git command ${args.join(' ')}`);
  };
  const run = () => releaseMain({ github, repo, runId, root, git,
    candidateBuilder: async (baseline, version) => {
      assert.equal(baseline, undefined);
      assert.equal(version, candidate.version.toString());
      return candidate;
    }, log: message => logs.push(message) });
  return { run, calls, logs, lookups, root, get main() { return main; } };
}

for (const title of ['ci: recover main workflows', 'feat: add snapshots']) {
  test(`a direct main push without a PR cannot release: ${title}`, async t => {
    const h = await harness(t, { promotions: [], promotion: { title } });
    const version = readVersion(h.root, false);
    await h.run();
    assert.deepEqual(h.calls, []);
    assert.equal(readVersion(h.root, false), version);
    assert.deepEqual(h.lookups, [{ ...repo, commit_sha: source, per_page: 100 }]);
    assert.deepEqual(h.logs, ['A merged main PR is required for a release; skipping this direct push.']);
  });
}

test('nonempty invalid PR associations still fail without release mutations', async t => {
  for (const promotions of [
    [{}], [{ ...promotion, merged_at: null }], [{ ...promotion, merge_commit_sha: previous }],
    [{ ...promotion, base: { ref: 'develop', repo: promotion.base.repo } }],
    [{ ...promotion, base: { ref: 'main', repo: { full_name: 'fork/hdTrans' } } }], [promotion, promotion],
  ]) {
    const h = await harness(t, { promotions });
    await assert.rejects(h.run(), /exactly one merged main PR/);
    assert.deepEqual(h.calls, []);
    assert.equal(h.lookups.length, 1);
  }
});

test('release uses normal SSH pushes and a local annotated tag, without dispatch', async t => {
  const h = await harness(t);
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag-push', 'release']);
  assert.deepEqual(h.calls.find(call => call[0] === 'push')[1], ['push', 'origin', 'HEAD:refs/heads/main']);
  assert.doesNotMatch(h.calls.find(call => call[0] === 'commit')[1].at(-1), /\[(?:skip ci|ci skip|no ci|skip actions|actions skip)\]/i);
  assert.deepEqual(h.calls.find(call => call[0] === 'tag-object')[1].slice(-6),
    ['tag', '-a', 'v0.1.1', bot, '-m', 'Release v0.1.1']);
  assert.deepEqual(h.calls.find(call => call[0] === 'tag-push')[1], ['push', 'origin', 'refs/tags/v0.1.1:refs/tags/v0.1.1']);
  const release = h.calls.at(-1)[1];
  assert.equal(release.target_commitish, bot);
  assert.equal(release.draft, true);
  assert.equal(release.make_latest, 'false');
  assert.equal(readVersion(h.root), '0.1.1');
  assert.match(h.logs.at(-1), /tag pipeline builds and publishes/);
  assert.doesNotMatch(readFileSync(new URL('./release.mjs', import.meta.url), 'utf8'), /createWorkflowDispatch|listWorkflowRuns|createTag|createRef/);
  const workflow = readFileSync(new URL('../../.github/workflows/release-please.yml', import.meta.url), 'utf8');
  assert.match(workflow, /ssh-key: \$\{\{ secrets\.HDTRANS_RELEASE_PUSH_KEY \}\}/);
});

for (const options of [{ advanced: true }, { advanceOnCheck: 2 }, { advanceOnCheck: 3 }, { advanceOnCheck: 4 },
  { pushRace: true }, { promotion: { title: 'docs: promote docs' } }]) {
  test(`no release for stale main or no changes: ${JSON.stringify(options)}`, async t => {
    const h = await harness(t, options);
    await h.run();
    assert.ok(!h.calls.some(call => ['tag-object', 'tag-push', 'release'].includes(call[0])));
    assert.ok(!h.calls.some(call => call[1].includes?.('--force')));
  });
}

test("manual retry resumes only the validated Build's exact version-only bot child", async t => {
  const h = await harness(t, { resume: true });
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['tag-object', 'tag-push', 'release']);
  const bad = await harness(t, { resume: true, badResume: true });
  await bad.run();
  assert.deepEqual(bad.calls, []);
});

test('retry after an API failure never pushes or retags again and reports manual tag recovery', async t => {
  const h = await harness(t, { releaseFailure: true });
  await assert.rejects(h.run(), /API unavailable/);
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag-push']);
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag-push', 'release']);
  assert.match(h.logs.at(-1), /Publish npm manually at this tag with dry_run=false/);
});

for (const type of ['commit', 'tag']) {
  test(`retry accepts an existing ${type === 'tag' ? 'annotated' : 'lightweight'} tag targeting the release commit`, async t => {
    const h = await harness(t, { resume: true, existingTag: { type, sha: type === 'tag' ? annotation : bot } });
    await h.run();
    assert.deepEqual(h.calls.map(call => call[0]), ['release']);
    assert.match(h.logs.at(-1), /Publish npm manually/);
  });

  test(`retry rejects an existing ${type === 'tag' ? 'annotated' : 'lightweight'} tag targeting a different commit`, async t => {
    const h = await harness(t, { resume: true, existingTag: { type, sha: type === 'tag' ? annotation : newer }, taggedCommit: newer });
    await assert.rejects(h.run(), /already targets another commit/);
    assert.deepEqual(h.calls, []);
  });
}

test('main advancing after local annotation creation leaves the remote tag unpublished', async t => {
  const h = await harness(t, { advanceOnCheck: 5 });
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object']);
});

for (const when of ['advanceAfterTag', 'advanceAfterRelease']) {
  test(`main advancing after the immutable tag cannot abandon pinned publication: ${when}`, async t => {
    const h = await harness(t, { [when]: true });
    await h.run();
    assert.equal(h.main, newer);
    assert.deepEqual(h.calls.map(call => call[0]), ['add', 'commit', 'push', 'tag-object', 'tag-push', 'release']);
    assert.equal(h.calls.at(-1)[1].target_commitish, bot);
  });
}

test('a tag push retry reuses its exact local annotation without retagging or forcing', async t => {
  const h = await harness(t, { tagPushFailure: true });
  await assert.rejects(h.run(), /SSH unavailable/);
  await h.run();
  assert.equal(h.calls.filter(call => call[0] === 'tag-object').length, 1);
  assert.equal(h.calls.filter(call => call[0] === 'tag-push').length, 2);
  assert.equal(h.calls.at(-1)[0], 'release');
});

for (const patch of [{ localTaggedCommit: newer }, { localTagType: 'commit' }]) {
  test(`an unexpected local tag cannot be overwritten: ${JSON.stringify(patch)}`, async t => {
    const h = await harness(t, { resume: true, localTag: true, ...patch });
    await assert.rejects(h.run(), /expected local annotated tag/);
    assert.deepEqual(h.calls, []);
  });
}

test('a competing remote tag is accepted only at this exact release commit', async t => {
  const owned = await harness(t, { resume: true, tagPushRace: 'owned' });
  await owned.run();
  assert.deepEqual(owned.calls.map(call => call[0]), ['tag-object', 'tag-push', 'release']);
  assert.match(owned.logs.at(-1), /Publish npm manually/);
  const other = await harness(t, { resume: true, tagPushRace: 'other' });
  await assert.rejects(other.run(), /already targets another commit/);
  assert.deepEqual(other.calls.map(call => call[0]), ['tag-object', 'tag-push']);
});

test('retry preserves both draft and published releases without recreating or deleting them', async t => {
  for (const draft of [true, false]) {
    const h = await harness(t, { resume: true, existingTag: { type: 'commit', sha: bot }, draftHidden: draft,
      existingRelease: { tag_name: 'v0.1.1', draft, id: 12 } });
    await h.run();
    assert.deepEqual(h.calls, []);
    assert.match(h.logs.at(-1), /Publish npm manually/);
  }
});

test('the version commit Build skips without a new promotion PR or another version bump', async t => {
  const h = await harness(t, { resume: true, run: { head_sha: bot }, promotions: [] });
  await h.run();
  assert.deepEqual(h.calls, []);
  assert.equal(readVersion(h.root), '0.1.1');
  assert.deepEqual(h.lookups, [{ ...repo, commit_sha: bot, per_page: 100 }]);
  assert.match(h.logs.at(-1), /skipping this direct push/);
});

test('an exact existing skip-marker release commit remains recoverable without retagging', async t => {
  const h = await harness(t, { resume: true, skipMessage: true, existingTag: { type: 'commit', sha: bot } });
  await h.run();
  assert.deepEqual(h.calls.map(call => call[0]), ['release']);
  assert.match(h.logs.at(-1), /Publish npm manually/);
  for (const skipMessage of [false, true]) {
    const wrong = await harness(t, { resume: true, skipMessage, wrongBuild: true });
    await assert.rejects(wrong.run(), /Not this Build/);
    assert.deepEqual(wrong.calls, []);
  }
});
