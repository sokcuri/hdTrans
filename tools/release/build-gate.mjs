import { execFileSync } from 'node:child_process';

// Decides whether a Build run needs its matrix. Anything unexpected answers yes.

export const releaseFiles = [
  'CHANGELOG.md', '.release-please-manifest.json', 'release-version.txt',
  'npm/package.json', 'npm/package-lock.json',
];
export const releaseBot = { name: 'github-actions[bot]', email: '41898282+github-actions[bot]@users.noreply.github.com' };

const versionMessage = /^chore\(main\): release \d+\.\d+\.\d+\n\nRelease-Source: ([0-9a-f]{40})\nRelease-Build: [1-9]\d*$/;

const defaultGit = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }).trim();

function changedFiles(git, ...range) {
  return git('diff-tree', '--no-commit-id', '--name-only', '-r', ...range).split('\n').filter(Boolean);
}

// The release bot's version commit, pushed alone onto the merge commit it versions.
// The tag at this commit builds and tests it before anything is published.
export function versionCommit({ payload, sha, git }) {
  const [author, authorEmail, committer, committerEmail, parents] =
    git('show', '-s', '--format=%an%n%ae%n%cn%n%ce%n%P', 'HEAD').split('\n');
  const source = git('show', '-s', '--format=%B', 'HEAD').match(versionMessage)?.[1];
  const changed = changedFiles(git, 'HEAD');
  if (!source || git('rev-parse', 'HEAD') !== sha || payload.after !== sha || payload.before !== source ||
      parents !== source || author !== releaseBot.name || committer !== releaseBot.name ||
      authorEmail !== releaseBot.email || committerEmail !== releaseBot.email ||
      changed.length === 0 || changed.some(path => !releaseFiles.includes(path))) {
    return;
  }
  return source;
}

export async function buildGate({ context, git = defaultGit }) {
  try {
    if (context.eventName === 'push' && context.ref === 'refs/heads/main') {
      const source = versionCommit({ payload: context.payload, sha: context.sha, git });
      if (source) {
        return { run: false, reason: `release version commit on ${source}; the tag pipeline builds and tests it` };
      }
    }
    return { run: true, reason: 'full build' };
  } catch (error) {
    return { run: true, reason: `full build (gate error: ${error.message})` };
  }
}
