import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import {
  chmodSync, copyFileSync, existsSync, lstatSync, mkdirSync, mkdtempSync, readFileSync, readdirSync,
  readlinkSync, realpathSync, rmSync, statSync, symlinkSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const command = join(root, 'web/deploy-command.sh');
const version = spawnSync('rsync', ['--version'], { encoding: 'utf8' });
// The host runs GNU rsync; openrsync has no --munge-links, so elsewhere only the
// commands deploy.sh sends are checked, not run.
const gnuRsync = version.status === 0 && /^rsync\s+version/.test(version.stdout);

function sandbox(t) {
  const base = realpathSync(mkdtempSync(join(tmpdir(), 'hdtrans-web-deploy-')));
  t.after(() => rmSync(base, { recursive: true, force: true }));
  const repo = join(base, 'repo');
  const site = join(base, 'site');
  const dist = join(repo, 'build/web/dist');
  mkdirSync(join(repo, 'web/examples'), { recursive: true });
  mkdirSync(join(dist, 'examples'), { recursive: true });
  mkdirSync(join(site, 'examples'), { recursive: true });
  copyFileSync(join(root, 'web/deploy.sh'), join(repo, 'web/deploy.sh'));
  writeFileSync(join(repo, 'web/examples.tsv'), 'inrepo.txt\tIn repository\nhosted.txt\tHosted only\n');
  writeFileSync(join(repo, 'web/examples/inrepo.txt'), 'in repository\n');
  writeFileSync(join(dist, 'index.html'), 'new page\n');
  writeFileSync(join(dist, 'examples/inrepo.txt'), 'in repository\n');
  writeFileSync(join(dist, 'examples/hosted.txt'), 'hosted text\n');
  symlinkSync('/etc', join(dist, 'escape'));
  writeFileSync(join(site, 'index.html'), 'previous page\n');
  writeFileSync(join(site, 'stale.html'), 'stale\n');
  writeFileSync(join(site, 'examples/hosted.txt'), 'hosted text\n');
  writeFileSync(join(site, 'examples/unlisted.txt'), 'unlisted text\n');
  writeFileSync(join(site, 'examples/unlisted.txt.br'), 'compressed\n');
  for (const day of ['01', '02', '03']) mkdirSync(`${site}.bak-200001${day}T000000Z`);
  const ssh = join(base, 'ssh');
  const log = join(base, 'commands');
  writeFileSync(ssh, `#!/bin/bash
[ "$1" = -l ] && shift 2
shift
cmd="$*"
if ! SSH_ORIGINAL_COMMAND="$cmd" bash '${command}' --check 2>/dev/null; then
  printf 'REJECTED %s\\n' "$cmd" >> '${log}'
  exit 1
fi
printf '%s\\n' "$cmd" >> '${log}'
[[ $cmd != 'rsync --server'* ]] || [ "$RUN_RSYNC" = 1 ] || exit 1
SSH_ORIGINAL_COMMAND="$cmd" exec bash '${command}'
`);
  chmodSync(ssh, 0o755);
  const env = {
    ...process.env, HDTRANS_WEB_HOST: 'web', HDTRANS_WEB_SSH: ssh, HDTRANS_WEB_DIR: site,
    HDTRANS_DEPLOY_COMMAND_DIR: site, HDTRANS_WEB_KEEP: '2', RUN_RSYNC: gnuRsync ? '1' : '0',
  };
  const deploy = (...args) => spawnSync('bash', [join(repo, 'web/deploy.sh'), ...args], { env, encoding: 'utf8' });
  const commands = () => (existsSync(log) ? readFileSync(log, 'utf8').trim().split('\n') : []);
  return { base, site, deploy, commands };
}

test('the forced command accepts every command deploy.sh sends', t => {
  const { base, site, deploy, commands } = sandbox(t);

  const fetched = deploy('examples', join(base, 'fetched'));
  assert.equal(fetched.status, 0, fetched.stderr);
  assert.deepEqual(readdirSync(join(base, 'fetched')), ['hosted.txt']);
  assert.equal(readFileSync(join(base, 'fetched/hosted.txt'), 'utf8'), 'hosted text\n');

  const dry = deploy('dry-run');
  const deployed = deploy();
  assert.match(deployed.stdout + deployed.stderr, new RegExp(`rollback: web/deploy.sh rollback ${site}\\.bak-\\d{8}T\\d{6}Z`));
  const sent = commands();
  assert.deepEqual(sent.filter(c => c.startsWith('REJECTED')), []);
  assert.equal(sent.filter(c => c.startsWith('rsync --server')).length, 2);
  assert.ok(sent.some(c => c.startsWith('set -e; if [ -d ')));
  if (!gnuRsync) {
    assert.notEqual(deployed.status, 0);
    t.diagnostic('GNU rsync is not installed; the transfer itself was not run');
    return;
  }

  assert.equal(dry.status, 0, dry.stderr);
  assert.match(dry.stdout, /^<f\S* index\.html$/m);
  assert.match(dry.stdout, /^<f\+* examples\/inrepo\.txt$/m);
  // Same content with another time is not a change; hosted-only examples are kept.
  assert.doesNotMatch(dry.stdout, /hosted\.txt|deleting\s+examples/);
  assert.equal(deployed.status, 0, deployed.stderr);
  assert.equal(readFileSync(join(site, 'index.html'), 'utf8'), 'new page\n');
  assert.equal(existsSync(join(site, 'stale.html')), false);
  assert.equal(readFileSync(join(site, 'examples/unlisted.txt'), 'utf8'), 'unlisted text\n');
  assert.equal(existsSync(join(site, 'examples/unlisted.txt.br')), true);
  assert.equal(readFileSync(join(site, 'examples/inrepo.txt'), 'utf8'), 'in repository\n');
  assert.ok(lstatSync(join(site, 'escape')).isSymbolicLink());
  assert.notEqual(readlinkSync(join(site, 'escape')), '/etc');
  assert.equal(statSync(join(site, 'index.html')).mode & 0o444, 0o444);
  const backups = readdirSync(base).filter(n => n.startsWith('site.bak-')).sort();
  assert.equal(backups.length, 2);
  assert.equal(backups[0], 'site.bak-20000103T000000Z');
  assert.equal(readFileSync(join(base, backups[1], 'index.html'), 'utf8'), 'previous page\n');
});

test('the forced command accepts both rsync clients and refuses anything else', t => {
  const { site } = sandbox(t);
  const run = (cmd, ...args) => spawnSync('bash', [command, ...args], {
    env: { ...process.env, HDTRANS_DEPLOY_COMMAND_DIR: site, SSH_ORIGINAL_COMMAND: cmd }, encoding: 'utf8', input: '',
  });
  const accepted = [
    `rsync --server -ltprze.iLsfxCIvu --delete . ${site}/`,
    `rsync --server -nltprcze.iLsfxCIvu --log-format=%i --delete . ${site}/`,
    `rsync --server --delete-before -l -p -r -t -z --dirs . ${site}/`,
    `rsync --server --delete-before -c -l -n -p -r -t -z --dirs --log-format=%i . ${site}/`,
    `cat '${site}/examples/hosted.txt'`,
  ];
  for (const cmd of accepted) assert.equal(run(cmd, '--check').status, 0, cmd);

  symlinkSync('/etc/hosts', join(site, 'examples/link.txt'));
  const backup = `set -e; if [ -d '${site}' ]; then cp -a '${site}' '${site}.bak-20260101T000000Z'; echo 'backup: ${site}.bak-20260101T000000Z'; fi; mkdir -p '${site}'`;
  const refused = [
    '', 'id', `chmod -R a+rX '${site}'; id`, `${backup}\nid`,
    backup.replace("echo 'backup: " + site + '.bak-20260101', "echo 'backup: " + site + '.bak-20260102'),
    `rsync --server -ltprzKe.iLsfxCIvu --delete . ${site}/`,
    `rsync --server -Lltprze.iLsfxCIvu --delete . ${site}/`,
    `rsync --server -kltprze.iLsfxCIvu --delete . ${site}/`,
    `rsync --server --sender -ltprze.iLsfxCIvu . ${site}/`,
    `rsync --server -ltprze.iLsfxCIvu --delete --log-file=/tmp/x . ${site}/`,
    `rsync --server -ltprze.iLsfxCIvu --delete . ${site}/../`,
    `ls -1d '${site}'.bak-* 2>/dev/null | sort -r | tail -n +1 | xargs -r rm -rf --`,
    `cat '${site}/examples/../../etc/passwd'`,
    `cat '${site}/examples/link.txt'`,
    `cat '${site}/examples/missing.txt'`,
  ];
  for (const cmd of refused) {
    const result = run(cmd);
    assert.equal(result.status, 1, cmd);
    assert.match(result.stderr, /^deploy-command: /, cmd);
    assert.equal(result.stdout, '', cmd);
  }
  assert.equal(existsSync(`${site}.bak-20260101T000000Z`), false);
});
