// node web/build.mjs
//
// Builds the site into build/web/dist from the WebAssembly engine in
// build/wasm (emcmake cmake --preset wasm && cmake --build --preset wasm)
// and the dictionary folder dicts/, which the native CLI of the same sources
// compiles into dicts.hdsnap (hdtrans --compile-snapshot):
//   index.html   its version line filled with the date and short hash of the
//                commit the tree is at (Asia/Seoul)
//   js/<hash>/   the page scripts (tsc), in a folder named by their content hash
//                so a cached page never pairs old scripts with a new engine
//   examples/    the example texts listed in examples.tsv, read from
//                web/examples/ or HDTRANS_WEB_EXAMPLES; list.json names those
//                found, in the order of examples.tsv
//   assets/<sha256 prefix>/  the engine (hdtrans_engine.js, hdtrans_engine.wasm),
//                dicts.hdsnap, listed with sizes and hashes in manifest.json
// Text and wasm files get .br and .gz copies next to them.
//
// Environment: HDTRANS_DICTS (default dicts), HDTRANS_WASM (default
// build/wasm), HDTRANS_CLI (default build/mac/hdtrans, else
// build/linux/hdtrans), HDTRANS_WEB_EXAMPLES (more example texts, none by
// default).
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { promisify } from 'node:util';
import { brotliCompress, constants, gzip } from 'node:zlib';

const here = new URL('.', import.meta.url).pathname;
const root = join(here, '..');
const dictDir = resolve(root, process.env.HDTRANS_DICTS ?? 'dicts');
const wasmDir = resolve(root, process.env.HDTRANS_WASM ?? 'build/wasm');
const cli = resolve(root, process.env.HDTRANS_CLI ?? (['build/mac/hdtrans', 'build/linux/hdtrans'].find((p) => existsSync(join(root, p))) ?? 'build/mac/hdtrans'));
const extraExamples = process.env.HDTRANS_WEB_EXAMPLES ? resolve(process.env.HDTRANS_WEB_EXAMPLES) : '';
const out = join(root, 'build/web');
const dist = join(out, 'dist');

for (const f of ['hdtrans_engine.js', 'hdtrans_engine.wasm']) {
  if (!existsSync(join(wasmDir, f))) throw new Error(`${join(wasmDir, f)} is missing: build the wasm preset first`);
}
if (!existsSync(cli)) throw new Error(`${cli} is missing: build a native preset first (HDTRANS_CLI)`);
execFileSync(join(here, 'node_modules/.bin/tsc'), ['-p', here], { cwd: here, stdio: 'inherit' });

rmSync(dist, { recursive: true, force: true });
const scripts = readdirSync(out).filter((n) => n.endsWith('.js') || n.endsWith('.d.ts')).sort();
const scriptHash = createHash('sha256');
for (const n of scripts) scriptHash.update(n).update(readFileSync(join(out, n)));
const style = readFileSync(join(here, 'style.css'));
scriptHash.update(style);
const jsDir = `js/${scriptHash.digest('hex').slice(0, 16)}`;
mkdirSync(join(dist, jsDir), { recursive: true });
mkdirSync(join(dist, 'examples'));
writeFileSync(join(dist, 'style.css'), style);
for (const n of scripts) copyFileSync(join(out, n), join(dist, jsDir, n));

const list = [];
for (const line of readFileSync(join(here, 'examples.tsv'), 'utf8').split('\n')) {
  const [file, label] = line.split('\t');
  if (!label) continue;
  const from = [join(here, 'examples', file), extraExamples && join(extraExamples, file)].find((p) => p && existsSync(p));
  if (!from) throw new Error(`${file} is missing: set HDTRANS_WEB_EXAMPLES to the example text folder`);
  copyFileSync(from, join(dist, 'examples', file));
  list.push([label, file]);
}
writeFileSync(join(dist, 'examples/list.json'), JSON.stringify(list) + '\n');

const git = (...args) => execFileSync('git', args, { cwd: root, encoding: 'utf8' }).trim();
const commit = git('rev-parse', '--short', 'HEAD');
const date = new Date(Number(git('log', '-1', '--format=%ct')) * 1000).toLocaleString('sv-SE', { timeZone: 'Asia/Seoul' });
const page = readFileSync(join(here, 'index.html'), 'utf8');
const stamp = `<p class="contact" id="version">업데이트 ${date}, ${commit}</p>`;
if (!page.includes('<p class="contact" id="version">dev</p>')) throw new Error('index.html has no version line');
if (!page.includes('src="js/app.js"') || !page.includes('href="style.css"')) throw new Error('index.html script or style link moved');
writeFileSync(join(dist, 'index.html'), page
  .replace('<p class="contact" id="version">dev</p>', stamp)
  .replace('src="js/app.js"', `src="${jsDir}/app.js"`)
  .replace('href="style.css"', `href="style.css?${jsDir.slice(3)}"`));

const sha256 = (b) => createHash('sha256').update(b).digest('hex');
function asset(file, name) {
  const b = readFileSync(file);
  const hash = sha256(b);
  const path = `assets/${hash.slice(0, 16)}/${name}`;
  mkdirSync(join(dist, 'assets', hash.slice(0, 16)), { recursive: true });
  copyFileSync(file, join(dist, path));
  return { name, path, size: b.length, sha256: hash };
}

const snapshot = join(out, 'dicts.hdsnap');
rmSync(snapshot, { force: true });
execFileSync(cli, ['-dicts', dictDir, '-compile-snapshot', snapshot], { stdio: ['ignore', 'ignore', 'inherit'] });

// The engine must load what the page will give it.
{
  const { default: create } = await import(pathToFileURL(join(wasmDir, 'hdtrans_engine.js')).href);
  const e = await create({ wasmBinary: readFileSync(join(wasmDir, 'hdtrans_engine.wasm')) });
  e.FS.writeFile('/dicts.hdsnap', readFileSync(snapshot));
  const t0 = performance.now();
  const { HdTrans } = await import(pathToFileURL(join(out, 'translator.js')).href);
  const translator = await HdTrans.init({ module: e, directories: ['/dicts.hdsnap'] });
  console.log(`engine: loads in ${(performance.now() - t0).toFixed(0)} ms, ${(e.HEAPU8.length / 1e6).toFixed(1)} MB of wasm memory`);
  translator.close();
}

const manifest = {
  format: 4,
  engine: asset(join(wasmDir, 'hdtrans_engine.js'), 'hdtrans_engine.js'),
  wasm: asset(join(wasmDir, 'hdtrans_engine.wasm'), 'hdtrans_engine.wasm'),
  dict: {
    snapshot: asset(snapshot, 'dicts.hdsnap'),
  },
};
writeFileSync(join(dist, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');

const br = promisify(brotliCompress);
const gz = promisify(gzip);
const compressible = /\.(html|css|js|json|wasm|hdsnap|tsv|txt)$/;
const files = [];
const walk = (dir) => {
  for (const n of readdirSync(dir)) {
    const p = join(dir, n);
    if (statSync(p).isDirectory()) walk(p);
    else if (compressible.test(n)) files.push(p);
  }
};
walk(dist);
await Promise.all(
  files.map(async (p) => {
    const b = readFileSync(p);
    const [b1, b2] = await Promise.all([
      br(b, {
        params: {
          [constants.BROTLI_PARAM_QUALITY]: 11,
          [constants.BROTLI_PARAM_LGWIN]: 24,
          [constants.BROTLI_PARAM_SIZE_HINT]: b.length,
        },
      }),
      gz(b, { level: 9 }),
    ]);
    writeFileSync(p + '.br', b1);
    writeFileSync(p + '.gz', b2);
  }),
);

let raw = 0, brotli = 0;
for (const a of [manifest.engine, manifest.wasm, manifest.dict.snapshot]) {
  const c = statSync(join(dist, a.path + '.br')).size;
  raw += a.size;
  brotli += c;
  console.log(`${a.path}\t${(a.size / 1e6).toFixed(1)} MB\tbr ${(c / 1e6).toFixed(1)} MB`);
}
console.log(`assets: ${(raw / 1e6).toFixed(1)} MB, ${(brotli / 1e6).toFixed(1)} MB brotli; examples: ${list.length}; ${stamp}`);
