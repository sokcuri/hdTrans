import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const stableVersion = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;

export function releaseVersion(ref) {
  const version = ref.replace(/^refs\/tags\/v/, '');
  if (!ref.startsWith('refs/tags/v') || !stableVersion.test(version)) {
    throw new Error('Select a v<major>.<minor>.<patch> release tag.');
  }
  return version;
}

export function verifyNpmPackage({ root = process.cwd(), packageDir, ref }) {
  const version = releaseVersion(ref);
  const json = path => JSON.parse(readFileSync(join(root, path), 'utf8'));
  const expected = json('npm/package.json');
  const lock = json('npm/package-lock.json');
  if (expected.name !== 'hdtrans' || expected.version !== version ||
      readFileSync(join(root, 'release-version.txt'), 'utf8').trim() !== version ||
      json('.release-please-manifest.json')['.'] !== version ||
      lock.version !== version || lock.packages?.['']?.version !== version) {
    throw new Error('The source versions must match the release tag.');
  }
  const name = `hdtrans-${version}.tgz`;
  const path = join(packageDir, name);
  const bytes = readFileSync(path);
  const hash = createHash('sha256').update(bytes).digest('hex');
  const sums = readFileSync(join(packageDir, 'SHA256SUMS.txt'), 'utf8');
  if (!sums.split(/\r?\n/).includes(`${hash}  ${name}`)) {
    throw new Error('The npm package checksum does not match.');
  }
  const packed = JSON.parse(execFileSync('tar', ['-xOf', path, 'package/package.json'], { encoding: 'utf8' }));
  if (packed.name !== expected.name || packed.version !== version ||
      packed.repository?.url !== expected.repository.url) {
    throw new Error('The packed npm metadata does not match the release source.');
  }
  return { path, name: expected.name, version,
    integrity: `sha512-${createHash('sha512').update(bytes).digest('base64')}` };
}

function compareVersions(left, right) {
  const current = left.split('.').map(BigInt);
  const published = right.split('.').map(BigInt);
  const difference = current.findIndex((part, index) => part !== published[index]);
  return difference === -1 ? 0 : current[difference] < published[difference] ? -1 : 1;
}

export async function npmPublication(pkg, fetch = globalThis.fetch) {
  const absent = Symbol('absent');
  const metadata = async suffix => {
    const response = await fetch(`https://registry.npmjs.org/${encodeURIComponent(pkg.name)}/${suffix}`, {
      headers: { accept: 'application/json' }, signal: AbortSignal.timeout(30000),
    });
    if (response.status === 404) return absent;
    if (!response.ok) throw new Error(`npm registry returned HTTP ${response.status}.`);
    return response.json();
  };
  const existing = await metadata(pkg.version);
  if (existing !== absent) {
    if (existing?.name !== pkg.name || existing.version !== pkg.version || existing.dist?.integrity !== pkg.integrity) {
      throw new Error('The existing npm version does not match the validated package integrity.');
    }
    return { skip: true, tag: '' };
  }
  const latest = await metadata('latest');
  if (latest === absent) return { skip: false, tag: 'latest' };
  if (latest?.name !== pkg.name || !stableVersion.test(latest.version)) {
    throw new Error('The npm latest metadata must name a stable version of this package.');
  }
  const comparison = compareVersions(pkg.version, latest.version);
  if (comparison === 0) throw new Error('The npm registry returned inconsistent version metadata.');
  return { skip: false, tag: comparison < 0 ? 'previous' : 'latest' };
}

export async function findRelease(github, repo, tag) {
  try { return (await github.rest.repos.getReleaseByTag({ ...repo, tag })).data; }
  catch (error) { if (error.status !== 404) throw error; }
  const matches = (await github.paginate(github.rest.repos.listReleases, { ...repo, per_page: 100 }))
    .filter(release => release.tag_name === tag);
  if (matches.length > 1) throw new Error(`Multiple releases target ${tag}.`);
  return matches[0];
}

export async function publishRelease({ github, repo, ref, sha, packageDir, notes, root = process.cwd() }) {
  const nightly = ref === 'refs/heads/develop';
  const tag = nightly ? 'nightly' : `v${releaseVersion(ref)}`;
  if (!nightly) verifyNpmPackage({ root, packageDir, ref });
  const files = readdirSync(packageDir).sort();
  if (!files.includes('SHA256SUMS.txt') || files.length < 2) throw new Error('Release packages are missing.');
  const currentNightly = async () => {
    const head = (await github.rest.git.getRef({ ...repo, ref: 'heads/develop' })).data.object;
    return head.type === 'commit' && head.sha === sha;
  };
  if (nightly) {
    if (!await currentNightly()) return false;
    try { await github.rest.git.updateRef({ ...repo, ref: 'tags/nightly', sha, force: true }); }
    catch (error) {
      if (error.status !== 404) throw error;
      await github.rest.git.createRef({ ...repo, ref: 'refs/tags/nightly', sha });
    }
  }
  let release = await findRelease(github, repo, tag);
  if (nightly && !await currentNightly()) return false;
  if (!release) {
    release = (await github.rest.repos.createRelease({
      ...repo, tag_name: tag, target_commitish: sha, name: tag,
      body: notes, draft: true, prerelease: nightly, make_latest: 'false',
    })).data;
  }
  const assets = await github.paginate(github.rest.repos.listReleaseAssets, {
    ...repo, release_id: release.id, per_page: 100,
  });
  for (const asset of assets) {
    if (nightly || files.includes(asset.name)) {
      await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: asset.id });
    }
  }
  for (const name of files) {
    const data = readFileSync(join(packageDir, name));
    await github.rest.repos.uploadReleaseAsset({ ...repo, release_id: release.id, name, data,
      headers: { 'content-type': 'application/octet-stream', 'content-length': data.length },
    });
  }
  let makeLatest = false;
  if (!nightly) {
    let latest;
    let absent = false;
    try { latest = (await github.rest.repos.getLatestRelease(repo)).data; }
    catch (error) {
      if (error.status !== 404) throw error;
      absent = true;
    }
    makeLatest = absent || compareVersions(releaseVersion(ref), releaseVersion(`refs/tags/${latest?.tag_name}`)) >= 0;
  }
  const published = (await github.rest.repos.updateRelease({
    ...repo, release_id: release.id, name: tag, body: notes,
    draft: false, prerelease: nightly, make_latest: String(makeLatest),
  })).data;
  if (published.tag_name !== tag || published.draft !== false || published.prerelease !== nightly) {
    throw new Error('The release was not published as requested.');
  }
  return !nightly;
}
