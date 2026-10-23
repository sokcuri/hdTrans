import { execFileSync } from 'node:child_process';

// Decides whether a Build run needs its matrix. Anything unexpected answers yes.

export const releaseFiles = [
  'CHANGELOG.md', '.release-please-manifest.json', 'release-version.txt',
  'npm/package.json', 'npm/package-lock.json',
];
export const releaseBot = { name: 'github-actions[bot]', email: '41898282+github-actions[bot]@users.noreply.github.com' };

const versionMessage = /^chore\(main\): release \d+\.\d+\.\d+\n\nRelease-Source: ([0-9a-f]{40})\nRelease-Build: ([1-9]\d*)$/;
const buildPath = '.github/workflows/build.yml';

const defaultGit = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }).trim();

// Paths that differ, or undefined when one is deleted: release files are only added or edited.
function releaseChanges(lines) {
  const changes = lines.split('\n').filter(Boolean).map(line => line.split('\t'));
  if (changes.some(([status, path]) => !['A', 'M'].includes(status) || !releaseFiles.includes(path))) return;
  return changes.map(([, path]) => path);
}

// The release bot's version commit, pushed alone onto the merge commit it versions:
// its source and the Build run named in its trailers. The tag at this commit builds and
// tests it before anything is published.
export function versionCommit({ payload, sha, git }) {
  const [author, authorEmail, committer, committerEmail, parents] =
    git('show', '-s', '--format=%an%n%ae%n%cn%n%ce%n%P', 'HEAD').split('\n');
  const [, source, build] = git('show', '-s', '--format=%B', 'HEAD').match(versionMessage) ?? [];
  const changed = releaseChanges(git('diff-tree', '--no-commit-id', '--no-renames', '--name-status', '-r', 'HEAD'));
  if (!source || git('rev-parse', 'HEAD') !== sha || payload.after !== sha || payload.before !== source ||
      parents !== source || author !== releaseBot.name || committer !== releaseBot.name ||
      authorEmail !== releaseBot.email || committerEmail !== releaseBot.email || !changed?.length) {
    return;
  }
  return { source, build: Number(build) };
}

// The successful main push Build of the source that the release bot versioned.
export async function sourceBuildPassed({ github, repo, repositoryId, source, build }) {
  const run = (await github.rest.actions.getWorkflowRun({ ...repo, run_id: build })).data;
  return run.id === build && run.path === buildPath && run.event === 'push' && run.head_branch === 'main' &&
    run.status === 'completed' && run.conclusion === 'success' && run.head_sha === source &&
    run.repository?.id === repositoryId && run.head_repository?.id === repositoryId;
}

export const testedTreeArtifact = tree => `tested-tree-${tree}`;

// A completed, successful Build that recorded the tree as tested, from a develop push or the
// develop→main pull request of this repository: runs whose workflow file was reviewed onto develop.
export async function testedRun({ github, repo, repositoryId, tree }) {
  const name = testedTreeArtifact(tree);
  const artifacts = await github.paginate(github.rest.actions.listArtifactsForRepo, { ...repo, name, per_page: 100 });
  for (const artifact of artifacts) {
    const source = artifact.workflow_run;
    if (artifact.name !== name || artifact.expired || !source ||
        source.repository_id !== repositoryId || source.head_repository_id !== repositoryId) {
      continue;
    }
    const run = (await github.rest.actions.getWorkflowRun({ ...repo, run_id: source.id })).data;
    if (run.id === source.id && run.path === buildPath && run.status === 'completed' && run.conclusion === 'success' &&
        ['push', 'pull_request'].includes(run.event) && run.head_branch === 'develop' &&
        run.repository?.id === repositoryId && run.head_repository?.id === repositoryId) {
      return run.id;
    }
  }
}

function withoutVersion(text) {
  const json = JSON.parse(text);
  delete json.version;
  if (json.packages?.['']) delete json.packages[''].version;
  return JSON.stringify(json);
}

// The develop→main merge ref, which differs from the develop head only in the release files,
// with npm/package.json and its lock differing only in their versions.
export function releaseFilesOnly({ git, head }) {
  const parents = git('show', '-s', '--format=%P', 'HEAD').split(' ');
  if (parents.length !== 2 || parents[1] !== head) return false;
  const changed = releaseChanges(git('diff', '--no-renames', '--name-status', head, 'HEAD'));
  if (!changed) return false;
  return changed.filter(path => path.endsWith('.json') && path.startsWith('npm/'))
    .every(path => withoutVersion(git('show', `${head}:${path}`)) === withoutVersion(git('show', `HEAD:${path}`)));
}

export async function buildGate({ github, context, git = defaultGit }) {
  let tree = '';
  try {
    tree = git('rev-parse', 'HEAD^{tree}');
    const repo = context.repo;
    const repositoryId = context.payload.repository?.id;
    if (!Number.isInteger(repositoryId)) return { run: true, tree, reason: 'full build' };
    const mainPush = context.eventName === 'push' && context.ref === 'refs/heads/main';
    if (mainPush) {
      const version = versionCommit({ payload: context.payload, sha: context.sha, git });
      if (version && await sourceBuildPassed({ github, repo, repositoryId, ...version })) {
        return {
          run: false, tree,
          reason: `release version commit on ${version.source} (Build run ${version.build}); the tag pipeline builds and tests it`,
        };
      }
      const tested = await testedRun({ github, repo, repositoryId, tree });
      if (tested) return { run: false, tree, reason: `tree ${tree} passed in Build run ${tested}` };
    }
    const pr = context.payload.pull_request;
    const full = `${repo.owner}/${repo.repo}`.toLowerCase();
    if (context.eventName === 'pull_request' && pr?.base?.ref === 'main' && pr.head?.ref === 'develop' &&
        pr.base.repo?.full_name?.toLowerCase() === full && pr.head.repo?.full_name?.toLowerCase() === full &&
        /^[0-9a-f]{40}$/.test(pr.head.sha ?? '') && releaseFilesOnly({ git, head: pr.head.sha })) {
      const headTree = git('rev-parse', `${pr.head.sha}^{tree}`);
      const tested = await testedRun({ github, repo, repositoryId, tree: headTree });
      if (tested) {
        return {
          run: false, tree, testedBy: tested,
          reason: `develop tree ${headTree} passed in Build run ${tested}; this tree adds only release files`,
        };
      }
    }
    return { run: true, tree, reason: 'full build' };
  } catch (error) {
    return { run: true, tree, reason: `full build (gate error: ${error.message})` };
  }
}
