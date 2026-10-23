import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { findRelease, npmPublication, publishRelease, releaseVersion, verifyNpmPackage } from './publication.mjs';

const repo = { owner: 'example', repo: 'hdTrans' };
const sha = 'a'.repeat(40);
const ref = 'refs/tags/v1.2.3';
const metadata = { name: 'hdtrans', version: '1.2.3', repository: { url: 'git+https://github.com/example/hdTrans.git' } };

function fixture(t, packed = metadata) {
  const root = mkdtempSync(join(tmpdir(), 'hdtrans-publication-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const write = (path, value) => writeFileSync(join(root, path), typeof value === 'string' ? value : JSON.stringify(value));
  mkdirSync(join(root, 'npm'));
  mkdirSync(join(root, 'archive/package'), { recursive: true });
  const packageDir = join(root, 'packages');
  mkdirSync(packageDir);
  write('npm/package.json', metadata);
  write('npm/package-lock.json', { version: metadata.version, packages: { '': { version: metadata.version } } });
  write('release-version.txt', `${metadata.version}\n`);
  write('.release-please-manifest.json', { '.': metadata.version });
  write('archive/package/package.json', packed);
  const path = join(packageDir, 'hdTrans-1.2.3-wasm.tgz');
  execFileSync('tar', ['-czf', path, '-C', join(root, 'archive'), 'package']);
  const hash = createHash('sha256').update(readFileSync(path)).digest('hex');
  write('packages/SHA256SUMS.txt', `${hash}  hdTrans-1.2.3-wasm.tgz\n`);
  return { root, packageDir, ref, write };
}

for (const tag of ['0.0.0', '1.2.3', '100.20.30']) {
  test(`stable tag accepted: ${tag}`, () => assert.equal(releaseVersion(`refs/tags/v${tag}`), tag));
}
for (const tag of ['refs/heads/main', 'refs/heads/v1.2.3', 'refs/tags/nightly', 'refs/tags/v1.2',
  'refs/tags/v1.2.3-beta.1', 'refs/tags/v1.2.3+build', 'refs/tags/v01.2.3', 'refs/tags/v1.02.3', 'refs/tags/v1.2.03']) {
  test(`non-stable ref rejected: ${tag}`, () => assert.throws(() => releaseVersion(tag), /release tag/));
}

test('npm verification binds source versions, packed metadata and checksum to a stable tag', t => {
  const f = fixture(t);
  const pkg = verifyNpmPackage(f);
  assert.equal(pkg.name, 'hdtrans');
  assert.equal(pkg.version, metadata.version);
  assert.equal(pkg.integrity, `sha512-${createHash('sha512').update(readFileSync(pkg.path)).digest('base64')}`);
});

for (const [path, value] of [
  ['npm/package.json', { ...metadata, version: '1.2.4' }],
  ['npm/package.json', { ...metadata, name: 'other' }],
  ['release-version.txt', '1.2.4\n'], ['.release-please-manifest.json', {}],
  ['npm/package-lock.json', { version: '1.2.4', packages: { '': { version: '1.2.3' } } }],
  ['npm/package-lock.json', { version: '1.2.3', packages: { '': { version: '1.2.4' } } }],
]) {
  test(`source mismatch is rejected: ${path} ${JSON.stringify(value)}`, t => {
    const f = fixture(t);
    f.write(path, value);
    assert.throws(() => verifyNpmPackage(f), /source versions must match/);
  });
}
for (const patch of [{ name: 'other' }, { version: '1.2.4' }, { repository: { url: 'git+https://example.invalid/fork.git' } }]) {
  test(`packed metadata mismatch is rejected: ${JSON.stringify(patch)}`, t => {
    assert.throws(() => verifyNpmPackage(fixture(t, { ...metadata, ...patch })), /packed npm metadata/);
  });
}
test('checksum mismatch is rejected before registry access', t => {
  const f = fixture(t);
  f.write('packages/SHA256SUMS.txt', `${'0'.repeat(64)}  hdTrans-1.2.3-wasm.tgz\n`);
  assert.throws(() => verifyNpmPackage(f), /checksum does not match/);
});

const pkg = { name: 'hdtrans', version: '1.2.3', integrity: 'sha512-validated-local-tgz' };
function registry(responses, calls = []) {
  return async (url, options) => {
    calls.push(url);
    assert.equal(options.headers.authorization, undefined);
    const response = responses.shift();
    assert.ok(response, `Unexpected registry request: ${url}`);
    if (response instanceof Error) throw response;
    const [status, value] = response;
    return { status, ok: status >= 200 && status < 300, json: async () => {
      if (value instanceof Error) throw value;
      return value;
    } };
  };
}

test('an existing exact version skips only when registry integrity matches the validated local tgz', async () => {
  const calls = [];
  assert.deepEqual(await npmPublication(pkg, registry([[200, { ...pkg, dist: { integrity: pkg.integrity } }]], calls)),
    { skip: true, tag: '' });
  assert.deepEqual(calls, ['https://registry.npmjs.org/hdtrans/1.2.3']);
});
for (const value of [undefined, null, {}, { ...pkg, dist: {} }, { ...pkg, dist: { integrity: 'sha512-different' } },
  { ...pkg, version: '1.2.4', dist: { integrity: pkg.integrity } }]) {
  test(`existing registry mismatch fails closed: ${JSON.stringify(value)}`, async () => {
    await assert.rejects(npmPublication(pkg, registry([[200, value]])), /does not match.*integrity/);
  });
}
for (const status of [401, 403, 429, 500]) {
  test(`registry HTTP ${status} is never treated as absent`, async () => {
    await assert.rejects(npmPublication(pkg, registry([[status]])), new RegExp(`HTTP ${status}`));
  });
}
for (const response of [new Error('network unavailable'), [200, new SyntaxError('bad JSON')]]) {
  test(`registry transport or JSON failure is not absence: ${String(response)}`, async () => {
    await assert.rejects(npmPublication(pkg, registry([response])), /network unavailable|bad JSON/);
  });
}
for (const [version, latest, tag] of [
  ['1.2.3', '1.2.2', 'latest'], ['1.2.3', '1.3.0', 'previous'], ['1.11.0', '1.9.9', 'latest'],
  ['2.0.0', '10.0.0', 'previous'], ['0.9.0', '1.0.0', 'previous'],
  ['99999999999999999999.0.0', '99999999999999999998.0.0', 'latest'],
]) {
  test(`latest remains monotonic: ${version} after ${latest} uses ${tag}`, async () => {
    assert.deepEqual(await npmPublication({ ...pkg, version }, registry([[404], [200, { name: pkg.name, version: latest }]])),
      { skip: false, tag });
  });
}
test('only 404 permits first-version publication under latest', async () => {
  assert.deepEqual(await npmPublication(pkg, registry([[404], [404]])), { skip: false, tag: 'latest' });
  await assert.rejects(npmPublication(pkg, registry([[404], [503]])), /HTTP 503/);
});
for (const latest of [undefined, null, {}, { name: 'other', version: '1.0.0' }, { name: pkg.name, version: '2.0.0-beta' }]) {
  test(`invalid latest metadata fails closed: ${JSON.stringify(latest)}`, async () => {
    await assert.rejects(npmPublication(pkg, registry([[404], [200, latest]])), /latest metadata/);
  });
}
test('contradictory exact-version and latest responses fail closed', async () => {
  await assert.rejects(npmPublication(pkg, registry([[404], [200, { name: pkg.name, version: pkg.version }]])), /inconsistent/);
});

const missing = () => { throw Object.assign(new Error('Not found'), { status: 404 }); };
function releaseHarness(t, options = {}) {
  const f = fixture(t);
  const calls = [];
  let release = options.existing;
  const github = { rest: { repos: {
    getReleaseByTag: async () => release && !options.hiddenDraft ? { data: release } : missing(),
    listReleases: () => {}, listReleaseAssets: () => {},
    getLatestRelease: async () => {
      calls.push(['latest']);
      if (options.latestFailure) throw Object.assign(new Error('Latest API failed'), { status: 503 });
      return options.latest ? { data: { tag_name: options.latest } } : missing();
    },
    createRelease: async args => {
      calls.push(['create', args]);
      release = { id: 7, ...args };
      return { data: release };
    },
    deleteReleaseAsset: async args => { calls.push(['delete-asset', args]); },
    uploadReleaseAsset: async args => {
      calls.push(['upload', args]);
      if (options.uploadFailure) throw new Error('Upload failed');
    },
    updateRelease: async args => {
      calls.push(['publish', args]);
      release = { ...release, ...args, ...options.published };
      return { data: release };
    },
  }, git: new Proxy({}, { get: (_, name) => async args => { calls.push([`git-${String(name)}`, args]); } }),
  }, paginate: async method => method === github.rest.repos.listReleases ? (release ? [release] : []) : options.assets ?? [] };
  const run = () => publishRelease({ github, repo, ref: options.ref ?? ref, sha, packageDir: f.packageDir,
    notes: 'Release notes', root: f.root });
  return { run, github, calls, get release() { return release; } };
}

test('a stable release becomes public/latest only after every asset upload succeeds', async t => {
  const h = releaseHarness(t);
  assert.equal(await h.run(), true);
  assert.deepEqual(h.calls.map(([kind]) => kind), ['create', 'upload', 'upload', 'latest', 'publish']);
  assert.equal(h.calls[0][1].draft, true);
  assert.equal(h.calls[0][1].make_latest, 'false');
  assert.equal(h.calls.at(-1)[1].draft, false);
  assert.equal(h.calls.at(-1)[1].make_latest, 'true');
  assert.equal(h.release.tag_name, 'v1.2.3');
  assert.equal(h.release.target_commitish, sha);
});
test('upload failure leaves a new release as a draft without a stable publication output', async t => {
  const h = releaseHarness(t, { uploadFailure: true });
  await assert.rejects(h.run(), /Upload failed/);
  assert.equal(h.release.draft, true);
  assert.ok(!h.calls.some(([kind]) => kind === 'publish'));
});
for (const draft of [true, false]) {
  test(`existing ${draft ? 'draft' : 'published'} releases are reused without deletion or redrafting`, async t => {
    const h = releaseHarness(t, { existing: { id: 7, tag_name: 'v1.2.3', draft, prerelease: false }, hiddenDraft: draft,
      assets: [{ id: 9, name: 'hdTrans-1.2.3-wasm.tgz' }] });
    assert.equal(await h.run(), true);
    assert.deepEqual(h.calls.map(([kind]) => kind), ['delete-asset', 'upload', 'upload', 'latest', 'publish']);
    assert.equal(h.calls.at(-1)[1].release_id, 7);
  });
}
test('a draft or wrong-tag final API result cannot authorize npm publication', async t => {
  for (const published of [{ draft: true }, { prerelease: true }, { tag_name: 'v9.9.9' }]) {
    await assert.rejects(releaseHarness(t, { published }).run(), /not published as requested/);
  }
});
test('draft lookup fails closed on duplicate releases and non-404 API errors', async () => {
  const github = { rest: { repos: { getReleaseByTag: missing, listReleases: () => {} } },
    paginate: async () => [{ tag_name: 'v1.2.3' }, { tag_name: 'v1.2.3' }] };
  await assert.rejects(findRelease(github, repo, 'v1.2.3'), /Multiple releases/);
  github.rest.repos.getReleaseByTag = () => { throw Object.assign(new Error('unavailable'), { status: 503 }); };
  await assert.rejects(findRelease(github, repo, 'v1.2.3'), /unavailable/);
});
for (const branch of ['refs/heads/develop', 'refs/heads/main']) {
  test(`a branch push cannot create a release or move a tag: ${branch}`, async t => {
    const h = releaseHarness(t, { ref: branch, existing: { id: 7, tag_name: 'nightly' }, assets: [{ id: 9, name: 'old.zip' }] });
    await assert.rejects(h.run(), /release tag/);
    assert.deepEqual(h.calls, []);
  });
}

for (const [latest, makeLatest] of [['v1.2.2', 'true'], ['v1.2.3', 'true'], ['v1.2.4', 'false'], ['v2.0.0', 'false']]) {
  test(`GitHub latest remains monotonic after ${latest}: make_latest=${makeLatest}`, async t => {
    const h = releaseHarness(t, { latest });
    assert.equal(await h.run(), true);
    assert.equal(h.calls.at(-1)[1].make_latest, makeLatest);
    assert.equal(h.calls.at(-2)[0], 'latest');
  });
}
test('latest lookup errors and non-stable latest tags cannot publish a draft', async t => {
  for (const options of [{ latestFailure: true }, { latest: 'nightly' }, { latest: 'v2.0.0-beta' }]) {
    const h = releaseHarness(t, options);
    await assert.rejects(h.run(), /Latest API failed|release tag/);
    assert.equal(h.release.draft, true);
    assert.ok(!h.calls.some(([kind]) => kind === 'publish'));
  }
});
