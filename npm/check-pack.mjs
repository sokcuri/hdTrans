import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { access, readFile, readdir } from 'node:fs/promises';

const dist = new URL('./dist/', import.meta.url);
const expected = [
  'index.js', 'index.d.ts', 'cli.js', 'cli.d.ts',
  'runtime.js', 'runtime.d.ts', 'dictionary.json',
  'hdtrans_engine.js', 'hdtrans_engine.wasm', 'dicts.hdsnap', 'dicts.txt',
].sort();
const entries = await readdir(dist, { withFileTypes: true });
assert(entries.every((entry) => entry.isFile()), 'dist must not contain directories or links');
assert.deepEqual(entries.map((entry) => entry.name).sort(), expected, 'unexpected package files');
for (const name of ['LICENSE', 'NOTICE', 'THIRD-PARTY-NOTICES.txt', 'README.md']) {
  await access(new URL(name, import.meta.url));
}
const manifest = JSON.parse(await readFile(new URL('dictionary.json', dist), 'utf8'));
assert.deepEqual(manifest.files, [], 'the bundled snapshot must not require TSV files');
assert.equal(manifest.snapshot.file, 'dicts.hdsnap');
const snapshot = await readFile(new URL('dicts.hdsnap', dist));
assert(snapshot.length >= 160, 'invalid snapshot header');
assert.equal(createHash('sha256').update(snapshot).digest('hex'), manifest.snapshot.sha256);
assert.equal(snapshot.subarray(96, 128).toString('hex'), manifest.snapshot.builtinFingerprint);
assert.equal(snapshot.subarray(128, 160).toString('hex'), manifest.snapshot.dataFingerprint);
assert.equal(await readFile(new URL('dicts.txt', dist), 'utf8'), `${manifest.version}\n`);
const { version } = JSON.parse(await readFile(new URL('./package.json', import.meta.url), 'utf8'));
assert(manifest.version.startsWith(`${version}+`));
assert.match(manifest.version.slice(version.length + 1), /^[a-f0-9]{12}$/);
