import { execFileSync } from 'node:child_process';

// Chooses the jobs of a Build run from the paths a pull request changes (docs/build.md "CI").
// Pushes, tags, manual runs, release pull requests and anything unexpected get every job.

export const allPresets = ['linux', 'windows-x64', 'windows-x86', 'mac', 'wasm'];

const defaultGit = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }).trim();

// Platform code that only the Windows presets compile, or sources with sections only they compile.
const windowsDirs = ['src/cshared/', 'src/j2kengine/', 'src/gui/', 'src/frontend/', 'tests/host/', 'include/'];
const windowsFiles = ['tools/build.ps1'];
const windowsMacro = '_WIN32|_WIN64|_MSC_VER';
const compiled = /\.(c|cc|cpp|h|hpp|def|rc|manifest)$/;

// Inputs of every preset: the build itself.
const buildFiles = ['CMakeLists.txt', 'CMakePresets.json', '.github/workflows/build.yml'];
const buildDirs = ['cmake/', 'third_party/'];
// Read by the native tests at run time, compiled by nothing.
const dataDirs = ['dicts/', 'tests/rules/', 'tests/variants/', 'tests/fuzz/regressions/', 'examples/', 'ehnd/', 'user/',
  'web/examples/'];
const dataFiles = ['web/examples.tsv', 'hdtrans.config.example.yaml'];
// Tested by the release tools job only.
const releaseDirs = ['.github/', 'tools/release/'];
// Read by no build or test.
const docsDirs = ['docs/', 'spec/', 'skills/'];
const docsFiles = ['README.md', 'AGENTS.md', 'CLAUDE.md', 'SECURITY.md', 'CHANGELOG.md'];

const under = (path, dirs) => dirs.some(dir => path.startsWith(dir));

// One of build, windows, data, web, release, docs or code.
export function pathClass(path) {
  if (buildFiles.includes(path) || under(path, buildDirs)) return 'build';
  if (windowsFiles.includes(path) || under(path, windowsDirs)) return 'windows';
  if (dataFiles.includes(path) || under(path, dataDirs)) return 'data';
  if (path.startsWith('web/')) return 'web';
  if (under(path, releaseDirs)) return 'release';
  if (docsFiles.includes(path) || under(path, docsDirs)) return 'docs';
  return 'code';
}

export const fullScope = reason => ({ presets: [...allPresets], native: true, reason });

// The jobs for the classes of the changed paths: the linux build and the sanitizer and fuzz jobs
// for any change the native tests read, wasm for compiled code and the web page, Windows for
// Windows code; mac only in full runs.
export function scopeOf(classes) {
  if (classes.size === 0) return fullScope('no changed paths found; every job runs');
  if (classes.has('build')) return fullScope('the build changed; every job runs');
  const code = classes.has('code');
  const windows = classes.has('windows');
  const native = code || windows || classes.has('data');
  const presets = allPresets.filter(preset =>
    (preset === 'linux' && native) ||
    (preset === 'wasm' && (code || classes.has('web'))) ||
    (preset.startsWith('windows-') && windows));
  const reason = `changed: ${[...classes].sort().join(', ')}; presets: ${presets.join(', ') || 'none'}` +
    `${native ? '' : '; no native tests'}`;
  return { presets, native, reason };
}

// The compiled files among paths that hold a Windows-only section at either commit.
function windowsSections(git, base, paths) {
  const sources = paths.filter(path => compiled.test(path));
  if (sources.length === 0) return [];
  // One diff fetches the blobs of both sides in a partial clone; grep then reads them locally.
  git('diff', '--no-renames', '--numstat', base, 'HEAD', '--', ...sources);
  const found = new Set();
  for (const rev of [base, 'HEAD']) {
    let out = '';
    try {
      out = git('grep', '-l', '-E', windowsMacro, rev, '--', ...sources);
    } catch (error) {
      if (error.status !== 1) throw error;  // 1: no match
    }
    for (const line of out.split('\n').filter(Boolean)) found.add(line.slice(rev.length + 1));
  }
  return sources.filter(path => found.has(path));
}

export function ciScope({ context, git = defaultGit }) {
  try {
    if (context.eventName === 'schedule') {
      return { presets: [], native: true, reason: 'scheduled run: the sanitizer and the long fuzz run only' };
    }
    if (context.eventName !== 'pull_request') return fullScope(`${context.eventName}: every job runs`);
    const pr = context.payload.pull_request;
    if (pr?.base?.ref === 'main' || ['develop', 'main'].includes(pr?.head?.ref)) {
      return fullScope('a release pull request; every job runs');
    }
    const base = git('rev-parse', '--verify', 'HEAD^1');
    const paths = git('diff', '--no-renames', '--name-only', base, 'HEAD').split('\n').filter(Boolean);
    const classes = new Set(paths.map(pathClass));
    const windows = windowsSections(git, base, paths.filter(path => pathClass(path) === 'code'));
    if (windows.length) classes.add('windows');
    const scope = scopeOf(classes);
    if (windows.length) scope.reason += `; Windows sections in ${windows.join(', ')}`;
    return scope;
  } catch (error) {
    return fullScope(`every job runs (scope error: ${error.message})`);
  }
}
