#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmod, copyFile, mkdir, mkdtemp, readFile, readdir, rename, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';

const root = fileURLToPath(new URL('../', import.meta.url));
const packageDir = join(root, 'npm');
const { values } = parseArgs({
  options: {
    cli: { type: 'string' },
    snapshot: { type: 'string' },
    wasm: { type: 'string' },
    wrapper: { type: 'string' },
    help: { type: 'boolean' },
  },
});
if (values.help) {
  console.log('Usage: node tools/package-npm.mjs (--cli CLI | --snapshot FILE) --wasm WASM_DIR [--wrapper TRANSLATOR.js]');
  process.exit(0);
}
if (values.cli && values.snapshot) throw new Error('provide only one of --cli and --snapshot');
const cliArg = values.cli ?? (values.snapshot ? undefined : process.env.HDTRANS_CLI);
const wasmArg = values.wasm ?? process.env.HDTRANS_WASM;
if ((!cliArg && !values.snapshot) || !wasmArg) throw new Error('provide --cli or --snapshot, and --wasm, from the same source build');
const cli = cliArg ? resolve(cliArg) : undefined;
const wasm = resolve(wasmArg);
const wrapper = resolve(values.wrapper ?? process.env.HDTRANS_NPM_WRAPPER ?? join(root, 'build/web/translator.js'));
const dictionary = join(root, 'dicts');
const stage = await mkdtemp(join(packageDir, '.stage-'));
const home = await mkdtemp(join(tmpdir(), 'hdtrans-npm-build-'));

try {
  const source = join(stage, 'source');
  await mkdir(source);
  for (const name of ['index.ts', 'cli.ts', 'hdtrans_engine.d.ts']) {
    await copyFile(join(packageDir, 'src', name), join(source, name));
  }
  await copyFile(join(dirname(wrapper), 'translator.d.ts'), join(source, 'runtime.d.ts'));
  execFileSync(process.execPath, [
    join(packageDir, 'node_modules/typescript/bin/tsc'),
    '--strict', '--target', 'es2022', '--module', 'nodenext', '--moduleResolution', 'nodenext',
    '--declaration', '--types', 'node', '--typeRoots', join(packageDir, 'node_modules/@types'),
    '--rootDir', source, '--outDir', stage, join(source, 'index.ts'), join(source, 'cli.ts'),
  ], { cwd: packageDir, stdio: 'inherit' });
  await rm(source, { recursive: true });
  await chmod(join(stage, 'cli.js'), 0o755);
  await copyFile(wrapper, join(stage, 'runtime.js'));
  await copyFile(join(dirname(wrapper), 'translator.d.ts'), join(stage, 'runtime.d.ts'));
  for (const name of ['hdtrans_engine.js', 'hdtrans_engine.wasm']) {
    await copyFile(join(wasm, name), join(stage, name));
  }
  const names = (await readdir(dictionary)).filter((name) => name.endsWith('.tsv'))
    .sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
  const sourceHash = createHash('sha256');
  for (const name of names) {
    sourceHash.update(name).update('\0').update(await readFile(join(dictionary, name))).update('\0');
  }
  const { version } = JSON.parse(await readFile(join(packageDir, 'package.json'), 'utf8'));
  const dictionaryVersion = `${version}+${sourceHash.digest('hex').slice(0, 12)}`;
  await writeFile(join(stage, 'dicts.txt'), `${dictionaryVersion}\n`);
  if (values.snapshot) {
    await copyFile(resolve(values.snapshot), join(stage, 'dicts.hdsnap'));
  } else {
    execFileSync(cli, ['-dicts', dictionary, '-compile-snapshot', join(stage, 'dicts.hdsnap')], {
      cwd: home,
      env: { ...process.env, HOME: home, USERPROFILE: home, HDTRANS_CACHE_DIR: 'off' },
      stdio: ['ignore', 'ignore', 'inherit'],
    });
  }
  const snapshot = await readFile(join(stage, 'dicts.hdsnap'));
  const manifest = {
    version: dictionaryVersion,
    snapshot: {
      file: 'dicts.hdsnap',
      sha256: createHash('sha256').update(snapshot).digest('hex'),
      builtinFingerprint: snapshot.subarray(96, 128).toString('hex'),
      dataFingerprint: snapshot.subarray(128, 160).toString('hex'),
    },
    files: [],
  };
  await writeFile(join(stage, 'dictionary.json'), `${JSON.stringify(manifest, null, 2)}\n`);

  // Opening checks the native snapshot's builtin/data fingerprints against this WASM.
  const { HdTrans } = await import(pathToFileURL(join(stage, 'index.js')));
  const translator = await HdTrans.init();
  try {
    if (translator.dictionaryVersion !== dictionaryVersion) throw new Error('packaged dictionary version does not match dicts.txt');
    if (!translator.translate('今日は本を読みます。')) throw new Error('empty translation from packaged engine');
  } finally {
    translator.close();
  }
  for (const name of ['LICENSE', 'THIRD-PARTY-NOTICES.txt']) {
    await copyFile(join(root, name), join(packageDir, name));
  }
  await rm(join(packageDir, 'dist'), { recursive: true, force: true });
  await rename(stage, join(packageDir, 'dist'));
  console.log(`npm package staged: ${names.length} source files, standalone snapshot; data ${manifest.snapshot.dataFingerprint}`);
} finally {
  await rm(stage, { recursive: true, force: true });
  await rm(home, { recursive: true, force: true });
}
