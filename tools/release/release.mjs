import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { GitHub, Manifest } from 'release-please';
import { Version } from 'release-please/build/src/version.js';
import { parseConventionalCommits } from 'release-please/build/src/commit.js';
import { DefaultVersioningStrategy } from 'release-please/build/src/versioning-strategies/default.js';
import { releaseBot, releaseFiles } from './build-gate.mjs';
import { findRelease } from './publication.mjs';

const stableVersion = /^\d+\.\d+\.\d+$/;

export function validateBuild(run, repo, workflowId) {
  if (run.status !== 'completed' || run.conclusion !== 'success' ||
      run.event !== 'push' || run.head_branch !== 'main' ||
      run.path !== '.github/workflows/build.yml' || run.workflow_id !== workflowId ||
      run.head_repository?.full_name?.toLowerCase() !== `${repo.owner}/${repo.repo}`.toLowerCase() ||
      !/^[a-f0-9]{40}$/.test(run.head_sha)) {
    throw new Error('A successful Build for a main push in this repository is required.');
  }
  return run.head_sha;
}

export function promotionVersion(promotion, baselineVersion) {
  const [title] = parseConventionalCommits([{ sha: promotion.merge_commit_sha, message: promotion.title }]);
  if (!title) throw new Error('The merged main PR must have a Conventional Commit title.');
  const commits = parseConventionalCommits([{
    sha: promotion.merge_commit_sha, message: `${promotion.title}\n\n${promotion.body ?? ''}`,
  }]);
  const primary = commits.findLast(commit => commit.message === title.message);
  const breaking = title.breaking || primary?.breaking;
  if (!breaking && !['feat', 'fix', 'perf', 'revert'].includes(title.type)) return;
  const strategy = new DefaultVersioningStrategy();
  return strategy.bump(Version.parse(baselineVersion), [{ ...title, breaking, notes: [] }]).toString();
}

export function mergedPromotion(pullRequests, repo, source) {
  const matches = pullRequests.filter(pr => pr.merged_at && pr.base?.ref === 'main' &&
    pr.base.repo?.full_name?.toLowerCase() === `${repo.owner}/${repo.repo}`.toLowerCase() &&
    pr.merge_commit_sha === source);
  if (matches.length !== 1) throw new Error('Expected exactly one merged main PR at the validated Build commit.');
  return matches[0];
}

export async function buildCandidate(scm, baselineVersion, releaseAs, promotion, now) {
  const releaseScm = Object.create(scm);
  releaseScm.mergeCommitIterator = async function* (...args) {
    let first = true;
    for await (const commit of scm.mergeCommitIterator(...args)) {
      if (first && promotion && !commit.message.split('\n').includes(promotion.title)) {
        yield { sha: promotion.merge_commit_sha, message: `${promotion.title}\n\n${promotion.body ?? ''}`, files: [] };
      }
      first = false;
      yield commit;
    }
  };
  const manifest = await Manifest.fromManifest(releaseScm, 'main', undefined, undefined, {}, undefined, releaseAs);
  // Only tagged versions delimit the changelog history.
  manifest.releasedVersions = baselineVersion ? { '.': Version.parse(baselineVersion) } : {};
  const candidates = await manifest.buildPullRequests();
  if (candidates.length > 1) throw new Error('Expected one root release candidate.');
  const candidate = candidates[0];
  if (candidate) {
    const changelog = candidate.updates.find(update => update.path === 'CHANGELOG.md')?.updater;
    const data = candidate.body.releaseData;
    if (typeof changelog?.changelogEntry !== 'string' || data.length !== 1 || data[0].notes !== changelog.changelogEntry) {
      throw new Error('Expected one matching generated changelog entry.');
    }
    const date = new Intl.DateTimeFormat('sv-SE', {
      timeZone: 'Asia/Seoul', year: 'numeric', month: '2-digit', day: '2-digit',
    }).format(now ?? new Date());
    const heading = /^(#{1,3} [^\r\n]+) \(\d{4}-\d{2}-\d{2}\)(?=\r?\n|$)/;
    if (!heading.test(changelog.changelogEntry)) throw new Error('The generated changelog date heading is missing.');
    // Only the freshly generated heading is dated here.
    const notes = changelog.changelogEntry.replace(heading, (_, prefix) => `${prefix} (${date})`);
    changelog.changelogEntry = notes;
    data[0].notes = notes;
  }
  return candidate;
}

export function readVersion(root, requireManifest = true) {
  const version = readFileSync(join(root, 'release-version.txt'), 'utf8').trim();
  const manifest = JSON.parse(readFileSync(join(root, '.release-please-manifest.json'), 'utf8'));
  const pkg = JSON.parse(readFileSync(join(root, 'npm/package.json'), 'utf8'));
  const lock = JSON.parse(readFileSync(join(root, 'npm/package-lock.json'), 'utf8'));
  if (!stableVersion.test(version) || (manifest['.'] !== version && (requireManifest || manifest['.'] !== undefined)) || pkg.version !== version ||
      lock.version !== version || lock.packages[''].version !== version) {
    throw new Error('The persisted release versions do not match.');
  }
  return version;
}

export function applyCandidate(root, candidate) {
  const paths = new Set(candidate.updates.map(update => update.path));
  if (paths.size !== releaseFiles.length || releaseFiles.some(path => !paths.has(path))) {
    throw new Error('The release candidate must update only the root release files.');
  }
  for (const update of candidate.updates) {
    const path = join(root, update.path);
    if (!existsSync(path) && !update.createIfMissing) throw new Error(`Missing ${update.path}`);
    let before = existsSync(path) ? readFileSync(path, 'utf8') : undefined;
    if (update.path === 'CHANGELOG.md' && before) {
      const entries = changelogEntries(before);
      if (entries[0]?.[1] === candidate.version?.toString()) {
        before = entries[1] ? before.slice(0, entries[0].index) + before.slice(entries[1].index) : undefined;
      }
    }
    writeFileSync(path, update.updater.updateContent(before));
  }
  const version = readVersion(root);
  if (candidate.version?.toString() !== version) throw new Error('The candidate version does not match.');
  return version;
}

export function commitMessage(version, source, runId) {
  return `chore(main): release ${version}\n\nRelease-Source: ${source}\nRelease-Build: ${runId}`;
}

function changelogEntries(changelog) {
  return [...changelog.matchAll(/^##? v?\[?(\d+\.\d+\.\d+)[^\n]*$/gm)];
}

export function releaseNotes(root, version) {
  const changelog = readFileSync(join(root, 'CHANGELOG.md'), 'utf8');
  const entries = changelogEntries(changelog);
  if (entries[0]?.[1] !== version) throw new Error('The changelog does not start with this release.');
  return changelog.slice(entries[0].index, entries[1]?.index).trim();
}

async function optional(request) {
  try { return (await request()).data; }
  catch (error) { if (error.status !== 404) throw error; }
}

export async function releaseMain({ github, repo, runId, token, root = process.cwd(),
  git = (...args) => execFileSync('git', args, { cwd: root, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }).trim(),
  candidateBuilder = async (baseline, releaseAs, promotion) =>
    buildCandidate(await GitHub.create({ ...repo, token, defaultBranch: 'main' }), baseline, releaseAs, promotion),
  log = console.log }) {
  if (!/^[1-9]\d*$/.test(String(runId))) throw new Error('A Build run ID is required.');
  const workflow = (await github.rest.actions.getWorkflow({ ...repo, workflow_id: 'build.yml' })).data;
  const run = (await github.rest.actions.getWorkflowRun({ ...repo, run_id: Number(runId) })).data;
  const source = validateBuild(run, repo, workflow.id);
  const mainSha = async () => (await github.rest.git.getRef({ ...repo, ref: 'heads/main' })).data.object.sha;
  const head = git('rev-parse', 'HEAD');
  const isCurrent = async sha => {
    if (await mainSha() === sha) return true;
    log('main advanced; leave this release to a newer successful Build.');
    return false;
  };
  if (!await isCurrent(head)) return;
  if (git('status', '--porcelain', '--untracked-files=no')) throw new Error('The checkout must be clean.');
  const associatedPRs = await github.paginate(github.rest.repos.listPullRequestsAssociatedWithCommit, {
    ...repo, commit_sha: source, per_page: 100,
  });
  if (associatedPRs.length === 0) {
    log('A merged main PR is required for a release; skipping this direct push.');
    return;
  }
  const promotion = mergedPromotion(associatedPRs, repo, source);

  let version;
  let releaseSha = head;
  if (head !== source) {
    const parents = git('show', '-s', '--format=%P', 'HEAD');
    const changed = git('diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD').split('\n');
    const message = git('show', '-s', '--format=%B', 'HEAD');
    if (parents !== source || changed.some(path => !releaseFiles.includes(path)) ||
        !message.includes(`\nRelease-Source: ${source}\n`)) {
      log('The validated Build is no longer at main; wait for a newer Build.');
      return;
    }
    version = readVersion(root);
    const expectedMessage = commitMessage(version, source, runId);
    if (message !== expectedMessage && message !== expectedMessage.replace('\n\n', ' [skip ci]\n\n')) {
      throw new Error('Not this Build\'s release commit.');
    }
    log(`Resuming v${version} at ${head}.`);
  } else {
    const baseline = git('tag', '--merged', source, '--list', 'v*', '--sort=-version:refname')
      .split('\n').find(tag => /^v\d+\.\d+\.\d+$/.test(tag));
    const nextVersion = promotionVersion(promotion, baseline?.slice(1) ?? readVersion(root, false));
    if (!nextVersion) { log('The main PR title does not request a release.'); return; }
    const candidate = await candidateBuilder(baseline?.slice(1), nextVersion, promotion);
    if (!candidate || candidate.version?.toString() !== nextVersion) {
      throw new Error('Release-please did not produce the version requested by the main PR.');
    }
    if (!await isCurrent(source)) return;
    version = applyCandidate(root, candidate);
    git('add', '--', ...releaseFiles);
    git('-c', `user.name=${releaseBot.name}`, '-c', `user.email=${releaseBot.email}`,
      'commit', '-m', commitMessage(version, source, runId));
    releaseSha = git('rev-parse', 'HEAD');
    if (!await isCurrent(source)) return;
    try {
      git('push', 'origin', 'HEAD:refs/heads/main');
    } catch (error) {
      const remote = await mainSha();
      if (remote !== releaseSha) {
        if (remote !== source) { log('main advanced before the normal push; skipping.'); return; }
        throw error;
      }
    }
  }

  const tag = `v${version}`;
  if (!await isCurrent(releaseSha)) return;
  const tagRef = () => optional(() => github.rest.git.getRef({ ...repo, ref: `tags/${tag}` }));
  const verifyTag = async existing => {
    let target = existing.object;
    while (target.type === 'tag') {
      target = (await github.rest.git.getTag({ ...repo, tag_sha: target.sha })).data.object;
    }
    if (target.type !== 'commit' || target.sha !== releaseSha) throw new Error(`${tag} already targets another commit.`);
  };
  let pushed = false;
  const existingTag = await tagRef();
  if (existingTag) {
    await verifyTag(existingTag);
  } else {
    if (git('tag', '--list', tag)) {
      if (git('rev-parse', `${tag}^{commit}`) !== releaseSha || git('cat-file', '-t', `refs/tags/${tag}`) !== 'tag') {
        throw new Error(`${tag} is not the expected local annotated tag.`);
      }
    } else {
      git('-c', `user.name=${releaseBot.name}`, '-c', `user.email=${releaseBot.email}`,
        'tag', '-a', tag, releaseSha, '-m', `Release ${tag}`);
    }
    if (!await isCurrent(releaseSha)) return;
    try {
      git('push', 'origin', `refs/tags/${tag}:refs/tags/${tag}`);
      pushed = true;
    } catch (error) {
      const remote = await tagRef();
      if (!remote) throw error;
      await verifyTag(remote);
    }
  }
  const existingRelease = await findRelease(github, repo, tag);
  if (!existingRelease) {
    await github.rest.repos.createRelease({
      ...repo, tag_name: tag, target_commitish: releaseSha, name: tag,
      body: releaseNotes(root, version), draft: true, prerelease: false, make_latest: 'false',
    });
  }
  log(pushed ? `Pushed ${tag}; its tag pipeline builds and publishes the release and npm package.` :
    `${tag} already exists; recover publication by running Publish npm manually at this tag with dry_run=false.`);
}
