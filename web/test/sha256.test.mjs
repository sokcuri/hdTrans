// node --test web/test/sha256.test.mjs, after npm --prefix web run compile.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'node:test';

const { sha256 } = await import(new URL('../../build/web/sha256.js', import.meta.url).href);
const hex = (b) => Buffer.from(b).toString('hex');

test('sha256 matches node:crypto around the padding boundaries', () => {
  for (const n of [0, 1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 1000, 100003]) {
    const b = Uint8Array.from({ length: n }, (_, i) => (i * 131 + 7) & 0xff);
    assert.equal(hex(sha256(b)), createHash('sha256').update(b).digest('hex'), `length ${n}`);
  }
  assert.equal(hex(sha256(new TextEncoder().encode('abc'))), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
});

test('sha256 reads a view at an offset', () => {
  const all = Uint8Array.from({ length: 300 }, (_, i) => i & 0xff);
  const part = all.subarray(17, 250);
  assert.equal(hex(sha256(part)), createHash('sha256').update(part).digest('hex'));
});
