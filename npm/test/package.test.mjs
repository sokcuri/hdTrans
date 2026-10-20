import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFile, mkdir, mkdtemp, readFile, readdir, rename, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import test from 'node:test';

const root = fileURLToPath(new URL('../../', import.meta.url));
const packageDir = join(root, 'npm');
const output = join(root, 'build/npm');
function npm(args, options = {}) {
  const executable = process.env.npm_execpath;
  return execFileSync(executable ? process.execPath : 'npm', executable ? [executable, ...args] : args, {
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
    timeout: 180_000,
    ...options,
  });
}

async function checkRuntime(HdTrans, dictionaryVersion) {
  const inspected = await HdTrans.getInfo();
  assert.equal(inspected.dictionaryVersion, dictionaryVersion);
  assert.equal(inspected.runtime, 'wasm');
  const translator = await HdTrans.init();
  const independent = await HdTrans.init();
  const sentence = '今日は本を読みます。';
  try {
    assert.match(translator.version, /\S/);
    assert.equal(translator.dictionaryVersion, dictionaryVersion);
    assert.equal(translator.runtime, 'wasm');
    assert.deepEqual(inspected, {
      version: translator.version,
      dictionaryVersion: translator.dictionaryVersion,
      runtime: translator.runtime,
    });
    assert.deepEqual(translator.getInfo(), inspected);
    translator.getInfo().version = 'changed copy';
    assert.deepEqual(translator.getInfo(), inspected);
    const translated = translator.translate(sentence);
    assert.match(translated, /[가-힣]/);
    assert.notEqual(translated, sentence);
    const base = translator.translate('学生');
    const entries = [{ ja: '学生', ko: '학우', pos: 'n', options: 'prio=2 force' }];
    translator.setUserDict(entries);
    assert.equal(translator.translate('学生'), '학우');
    assert.deepEqual(translator.getUserDict(), entries);
    assert.notEqual(independent.translate('学生'), '학우');
    translator.getUserDict()[0].ko = '외부수정';
    assert.equal(translator.getUserDict()[0].ko, '학우');
    const raw = '# npm local fixture\n学生\t동료\tn\tprio=3 force\n';
    translator.setUserDictRaw(raw);
    assert.equal(translator.getUserDictRaw(), raw);
    assert.equal(translator.translate('学生'), '동료');
    translator.setUserDictRaw('学生\t실패\tn\tbogus\n');
    assert.match(translator.userDictError, /bogus/);
    assert.equal(translator.translate('学生'), '동료');
    translator.clearUserDict();
    assert.deepEqual(translator.getUserDict(), []);
    assert.equal(translator.getUserDictRaw(), '');
    assert.equal(translator.translate('学生'), base);
    assert.equal(translator.dictionaryVersion, dictionaryVersion);

    const ehnd = {
      'PreFilter_npm.txt': '魔導具\t学生\t1\t0\n',
      'PostFilter_npm.txt': `${base}\t필터값\t1\t0\n`,
      'UserDict_npm.txt': 'ポチ\t바둑이\t1\n',
    };
    assert.deepEqual(await HdTrans.getInfo({ ehnd }), inspected);
    assert.notEqual(independent.translate('魔導具'), '필터값');
    translator.setEhnd(ehnd);
    assert.deepEqual(translator.getInfo(), inspected);
    assert.equal(translator.translate('魔導具'), '필터값');
    assert.equal(translator.translate('ポチ'), '바둑이');
    assert.equal(translator.dictionaryVersion, dictionaryVersion);
    const filtered = translator.translate('魔導具', { json: true });
    assert.equal(filtered.text, '필터값');
    assert(filtered.logs.some((line) => line.includes('PreFilter_npm.txt')));
    assert(filtered.logs.some((line) => line.includes('PostFilter_npm.txt')));
    assert.notEqual(independent.translate('魔導具'), '필터값');
    translator.clearEhnd();
    assert.deepEqual(translator.filterErrors, []);
    assert.equal(translator.dictionaryVersion, dictionaryVersion);
    assert.equal(translator.translate('学生'), base);
    translator.reset();
    const ordinary = translator.translate(sentence);
    translator.reset();
    const explained = translator.translate(sentence, { json: true });
    assert.equal(explained.text, ordinary);
    assert(explained.choices.length > 0);
    assert(Array.isArray(explained.logs));
    for (const choice of explained.choices) {
      assert.equal(typeof choice.surface, 'string');
      assert.equal(typeof choice.choice.provenance, 'string');
      assert.equal(typeof choice.choice.source, 'string');
      assert(Number.isInteger(choice.line) && Number.isInteger(choice.start));
    }
    const configured = await HdTrans.init({ ehnd });
    try {
      assert.equal(configured.translate('魔導具'), '필터값');
    } finally {
      configured.close();
    }
    return { translated: ordinary, engineVersion: translator.version };
  } finally {
    translator.close();
    independent.close();
    translator.close();
    assert.deepEqual(translator.getInfo(), inspected);
    assert.equal(translator.version, inspected.version);
    assert.equal(translator.dictionaryVersion, inspected.dictionaryVersion);
  }
}

test('packed install includes a ready-to-use engine, dictionary, types and CLI', { timeout: 600_000 }, async () => {
  await mkdir(output, { recursive: true });
  const [dry] = JSON.parse(npm(['pack', '--dry-run', '--json'], { cwd: packageDir }));
  const dictionary = JSON.parse(await readFile(join(packageDir, 'dist/dictionary.json'), 'utf8'));
  const expected = [
    'package.json', 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.txt',
    'dist/index.js', 'dist/index.d.ts', 'dist/cli.js', 'dist/cli.d.ts',
    'dist/runtime.js', 'dist/runtime.d.ts', 'dist/dictionary.json',
    'dist/hdtrans_engine.js', 'dist/hdtrans_engine.wasm', 'dist/dicts.hdsnap', 'dist/dicts.txt',
  ].sort();
  assert.deepEqual(dictionary.files, []);
  assert.deepEqual(dry.files.map((file) => file.path).sort(), expected);
  assert(!dry.files.some((file) => /\.tsv$/i.test(file.path) || file.path.startsWith('dist/dicts/')));
  const sourceHash = createHash('sha256');
  const sourceNames = (await readdir(join(root, 'dicts'))).filter((name) => name.endsWith('.tsv'))
    .sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
  assert(sourceNames.includes('kana.tsv'));
  for (const name of sourceNames) {
    sourceHash.update(name).update('\0').update(await readFile(join(root, 'dicts', name))).update('\0');
  }
  const { version } = JSON.parse(await readFile(join(packageDir, 'package.json'), 'utf8'));
  assert.equal(dictionary.version, `${version}+${sourceHash.digest('hex').slice(0, 12)}`);
  const [packed] = JSON.parse(npm(['pack', '--json', '--pack-destination', output], { cwd: packageDir }));
  assert.deepEqual(packed.files.map((file) => file.path).sort(), expected);
  const tarball = join(output, packed.filename);
  const home = await mkdtemp(join(tmpdir(), 'hdtrans-npm-test-'));
  try {
    const project = join(home, 'project');
    const cwd = join(home, 'unrelated');
    await mkdir(project);
    await mkdir(cwd);
    await writeFile(join(project, 'package.json'), '{"private":true,"type":"module"}\n');
    npm(['install', '--offline', '--no-audit', '--no-fund', tarball], { cwd: project });
    const installed = join(project, 'node_modules/hdtrans');
    const manifest = JSON.parse(await readFile(join(installed, 'package.json'), 'utf8'));
    assert.equal(manifest.name, 'hdtrans');
    assert.equal(manifest.license, 'AGPL-3.0-or-later');
    assert.equal(manifest.engines.node, '>=22');
    assert.equal(manifest.bin.hdtrans, './dist/cli.js');
    assert.deepEqual(manifest.exports['.'], {
      types: './dist/index.d.ts', import: './dist/index.js', default: './dist/index.js',
    });
    assert.equal(manifest.main, './dist/index.js');
    assert.equal(manifest.types, './dist/index.d.ts');
    assert.match(await readFile(join(installed, 'dist/cli.js'), 'utf8'), /^#!\/usr\/bin\/env node\n/);
    for (const file of ['LICENSE', 'THIRD-PARTY-NOTICES.txt']) {
      assert.deepEqual(await readFile(join(installed, file)), await readFile(join(root, file)));
    }
    assert.deepEqual(JSON.parse(await readFile(join(installed, 'dist/dictionary.json'), 'utf8')), dictionary);
    const snapshot = await readFile(join(installed, 'dist/dicts.hdsnap'));
    assert.equal(createHash('sha256').update(snapshot).digest('hex'), dictionary.snapshot.sha256);
    assert.equal(snapshot.subarray(96, 128).toString('hex'), dictionary.snapshot.builtinFingerprint);
    assert.equal(snapshot.subarray(128, 160).toString('hex'), dictionary.snapshot.dataFingerprint);
    assert.equal(await readFile(join(installed, 'dist/dicts.txt'), 'utf8'), `${dictionary.version}\n`);
    assert(!(await readdir(join(installed, 'dist'))).includes('dicts'));
    // Import through package exports from the fresh project, with an unrelated working directory.
    const probe = join(project, 'probe.mjs');
    await writeFile(probe, "export * from 'hdtrans';\n");
    const previousCwd = process.cwd();
    let translated, engineVersion;
    try {
      process.chdir(cwd);
      const exports = await import(pathToFileURL(probe));
      assert.deepEqual(Object.keys(exports), ['HdTrans']);
      ({ translated, engineVersion } = await checkRuntime(exports.HdTrans, dictionary.version));
    } finally {
      process.chdir(previousCwd);
    }

    await copyFile(join(packageDir, 'test/types.ts'), join(project, 'types.ts'));
    execFileSync(process.execPath, [
      join(packageDir, 'node_modules/typescript/bin/tsc'), '--noEmit', '--strict',
      '--target', 'es2022', '--module', 'nodenext', '--moduleResolution', 'nodenext',
      join(project, 'types.ts'),
    ], { cwd, encoding: 'utf8', timeout: 60_000 });

    const bin = join(project, 'node_modules/.bin', process.platform === 'win32' ? 'hdtrans.cmd' : 'hdtrans');
    const run = (args, input) => execFileSync(bin, args, { cwd, input, encoding: 'utf8', timeout: 90_000 });
    assert.equal(run(['今日は本を読みます。']), translated);
    assert.equal(run([], '今日は本を読みます。'), translated);
    assert.equal(run(['--version']), `hdTrans v${engineVersion.replace(/^v/, '')} (wasm) / dict ${dictionary.version}\n`);
    assert.equal(run(['-v']), run(['--version']));
    assert.match(run(['--help']), /^hdTrans\nAuthor: Sokcuri \(https:\/\/sokcuri\.neko\.kr\/\)\n\nUsage:/);
    assert.match(run(['--help']), /--json/);
    assert(!run(['--', '--version']).includes('(wasm) / dict'));
    const stdinJson = spawnSync(bin, ['--json'], { cwd, input: '今日は本を読みます。', encoding: 'utf8', timeout: 90_000 });
    assert.equal(stdinJson.status, 0);
    assert.equal(stdinJson.stderr, '');
    assert.equal(JSON.parse(stdinJson.stdout).text, translated);
    await writeFile(join(cwd, 'input.txt'), '今日は本を読みます。', 'utf8');
    const json = JSON.parse(run(['--in', 'input.txt', '--json']));
    assert.equal(json.text, translated);
    assert(json.choices.length > 0 && Array.isArray(json.logs));
    assert.equal(run(['--file', 'input.txt', '--out', 'output.txt']), '');
    assert.equal((await readFile(join(cwd, 'output.txt'), 'utf8')).trimEnd(), translated.trimEnd());
    assert.equal(run(['--in', 'input.txt', '--json', '--out', 'result.json']), '');
    assert.equal(JSON.parse(await readFile(join(cwd, 'result.json'), 'utf8')).text, translated);
    assert.equal(run(['--in', 'input.txt', '--out', 'input.txt']), '');
    assert.equal((await readFile(join(cwd, 'input.txt'), 'utf8')).trimEnd(), translated.trimEnd());
    const badOutput = spawnSync(bin, ['--out', 'missing/output.txt', '学生'], { cwd, encoding: 'utf8', timeout: 90_000 });
    assert.notEqual(badOutput.status, 0);
    assert.equal(badOutput.stdout, '');
    assert.match(badOutput.stderr, /^hdtrans: /);
    await writeFile(join(cwd, 'userdict.tsv'), '学生\t학우\tn\tforce\n', 'utf8');
    assert.equal(run(['--userdict', 'userdict.tsv', '学生']).trim(), '학우');
    const repeated = spawnSync(bin, ['--userdict', 'userdict.tsv', '--userdict', 'userdict.tsv', '学生'], { cwd, encoding: 'utf8', timeout: 90_000 });
    assert.notEqual(repeated.status, 0);
    assert.equal(repeated.stdout, '');
    assert.match(repeated.stderr, /only one --userdict/);
    await mkdir(join(cwd, 'Ehnd'));
    await writeFile(join(cwd, 'Ehnd/PreFilter_cli.txt'), '魔導具\t学生\t1\t0\n', 'utf8');
    await writeFile(join(cwd, 'Ehnd/PostFilter_cli.txt'), `${run(['学生']).trim()}\t필터값\t1\t0\n`, 'utf8');
    const why = spawnSync(bin, ['--why', '--ehnd', 'Ehnd', '魔導具'], { cwd, encoding: 'utf8', timeout: 90_000 });
    assert.equal(why.status, 0);
    assert.equal(why.stdout.trim(), '필터값');
    assert.match(why.stderr, /^PreFilter : \[PreFilter_cli\.txt:/m);
    assert.match(why.stderr, /^PostFIlter : \[PostFilter_cli\.txt:/m);
    assert.match(why.stderr, /^WHY\t\{/m);
    const failed = spawnSync(bin, ['--in', 'missing.txt'], { cwd, encoding: 'utf8', timeout: 90_000 });
    assert.notEqual(failed.status, 0);
    assert.equal(failed.stdout, '');
    assert.match(failed.stderr, /^hdtrans: /);
    const snapshotFile = join(installed, 'dist/dicts.hdsnap');
    await rename(snapshotFile, `${snapshotFile}.hidden`);
    try {
      const versionWithoutData = spawnSync(bin, ['--version'], { cwd, encoding: 'utf8', timeout: 90_000 });
      assert.notEqual(versionWithoutData.status, 0, '--version must actually load the dictionary');
      assert.equal(versionWithoutData.stdout, '');
      const probeUrl = JSON.stringify(pathToFileURL(probe).href);
      const importOnly = spawnSync(process.execPath, ['--input-type=module', '-e',
        `await import(${probeUrl});`], { cwd, encoding: 'utf8', timeout: 90_000 });
      assert.equal(importOnly.status, 0, 'import must not load missing engine data');
      const infoWithoutData = spawnSync(process.execPath, ['--input-type=module', '-e',
        `const { HdTrans } = await import(${probeUrl}); console.log(await HdTrans.getInfo());`],
      { cwd, encoding: 'utf8', timeout: 90_000 });
      assert.notEqual(infoWithoutData.status, 0, 'getInfo must actually load the dictionary');
      assert.equal(infoWithoutData.stdout, '');
    } finally {
      await rename(`${snapshotFile}.hidden`, snapshotFile);
    }
    const executed = npm(['exec', '--offline', '--prefix', project, '--', 'hdtrans', '今日は本を読みます。'], { cwd });
    assert.equal(executed.trimEnd(), translated.trimEnd());
    console.log(`PASS installed package/API/types/bin/stdin/file/JSON/userdict/npm exec; ${tarball} (${(await stat(tarball)).size} bytes)`);
  } finally {
    await rm(home, { recursive: true, force: true });
  }
});
