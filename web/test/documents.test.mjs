// node --test web/test/documents.test.mjs, after npm --prefix web run compile.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { crc32 as zlibCrc32, inflateRawSync } from 'node:zlib';

const { decodeText, formatDocument, outputName, encodeOutput, crc32, zip } = await import(
  new URL('../../build/web/documents.js', import.meta.url).href
);

const bytes = (...b) => new Uint8Array(b);
const utf8 = (s) => new TextEncoder().encode(s);

test('decodeText detects encodings like the Windows window', () => {
  const cases = [
    [utf8('日本語\r\n二行目\r三行目\n'), '日本語\n二行目\n三行目\n', 'UTF-8', '\r\n'],
    [utf8('日本語\n二行目\r\n'), '日本語\n二行目\n', 'UTF-8', '\n'],
    [utf8('日本語\r二行目\r\n'), '日本語\n二行目\n', 'UTF-8', '\r'],
    [bytes(0xef, 0xbb, 0xbf, ...utf8('日本語')), '日本語', 'UTF-8 BOM', '\n'],
    [bytes(0xff, 0xfe, 0xe5, 0x65, 0x2c, 0x67, 0x9e, 0x8a, 0x0d, 0x00, 0x0a, 0x00), '日本語\n', 'UTF-16LE', '\r\n'],
    [bytes(0x93, 0xfa, 0x96, 0x7b, 0x8c, 0xea, 0x0d, 0x0a, 0xb1), '日本語\nｱ', 'Shift-JIS', '\r\n'],
    [bytes(), '', 'UTF-8', '\n'],
  ];
  for (const [input, text, encoding, lineBreak] of cases) assert.deepEqual(decodeText(input), { text, encoding, lineBreak });
});

test('decodeText refuses what the Windows window refuses', () => {
  for (const input of [
    bytes(0xfe, 0xff, 0x65, 0xe5),
    bytes(0xff, 0xfe, 0xe5),
    bytes(0xef, 0xbb, 0xbf, 0x93, 0xfa),
    utf8('a\0b'),
    bytes(0x93),
  ]) {
    assert.equal(decodeText(input), undefined);
  }
});

test('formatDocument matches the Windows window with blank lines kept single', () => {
  // [source, translation, translation only, source/translation, spaced]
  const cases = [
    ['', '', '', '', ''],
    ['\n\n', '\n\n', '\n\n', '\n\n', '\n\n'],
    ['学生\n', '학생님\n', '학생님\n', '学生\n학생님\n', '学生\n학생님\n'],
    ['学生\n\n', '학생님\n\n', '학생님\n\n', '学生\n학생님\n\n', '学生\n학생님\n\n'],
    ['学生\nASCII', '학생님\nASCII', '학생님\nASCII', '学生\n학생님\nASCII\nASCII', '学生\n학생님\n\nASCII\nASCII'],
    ['学生\n\nASCII', '학생님\n\nASCII', '학생님\n\nASCII', '学生\n학생님\n\nASCII\nASCII', '学生\n학생님\n\n\nASCII\nASCII'],
    ['学生\n\n学生\n', '학생님\n\n학생님\n', '학생님\n\n학생님\n', '学生\n학생님\n\n学生\n학생님\n', '学生\n학생님\n\n\n学生\n학생님\n'],
    ['\n学生\n\nASCII\n\n', '\n학생님\n\nASCII\n\n', '\n학생님\n\nASCII\n\n', '\n学生\n학생님\n\nASCII\nASCII\n\n',
      '\n学生\n학생님\n\n\nASCII\nASCII\n\n'],
    ['a\nb', '가나', '가나', 'a\nb\n가나', 'a\nb\n가나'],
  ];
  for (const [source, translated, ...want] of cases) {
    assert.deepEqual(['translation', 'interleaved', 'spaced'].map((m) => formatDocument(source, translated, m)), want, source);
  }
});

test('outputName adds _번역.txt to the stem', () => {
  assert.equal(outputName('原文.txt'), '原文_번역.txt');
  assert.equal(outputName('a.b.TXT'), 'a.b_번역.txt');
  assert.equal(outputName('notes'), 'notes_번역.txt');
  assert.equal(outputName('.hidden'), '.hidden_번역.txt');
});

test('encodeOutput writes UTF-8 with a BOM', () => {
  assert.deepEqual(Array.from(encodeOutput('가')), [0xef, 0xbb, 0xbf, 0xea, 0xb0, 0x80]);
  assert.deepEqual(Array.from(encodeOutput('a\nb\n')), [0xef, 0xbb, 0xbf, 0x61, 0x0a, 0x62, 0x0a]);
  assert.deepEqual(Array.from(encodeOutput('a\nb\n', '\r\n')), [0xef, 0xbb, 0xbf, 0x61, 0x0d, 0x0a, 0x62, 0x0d, 0x0a]);
});

/** The entries of a ZIP archive, read through its central directory. */
function unzip(b) {
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const end = b.length - 22;
  assert.equal(v.getUint32(end, true), 0x06054b50);
  const count = v.getUint16(end + 10, true);
  let at = v.getUint32(end + 16, true);
  assert.equal(at + v.getUint32(end + 12, true), end);
  const entries = [];
  for (let i = 0; i < count; i++) {
    assert.equal(v.getUint32(at, true), 0x02014b50);
    const method = v.getUint16(at + 10, true);
    const crc = v.getUint32(at + 16, true);
    const size = v.getUint32(at + 20, true);
    const nameLength = v.getUint16(at + 28, true);
    const offset = v.getUint32(at + 42, true);
    const name = new TextDecoder().decode(b.subarray(at + 46, at + 46 + nameLength));
    assert.equal(v.getUint32(offset, true), 0x04034b50);
    assert.equal(v.getUint16(offset + 6, true) & 0x0800, 0x0800);
    const start = offset + 30 + v.getUint16(offset + 26, true) + v.getUint16(offset + 28, true);
    const stored = b.subarray(start, start + size);
    const data = method === 8 ? new Uint8Array(inflateRawSync(stored)) : stored;
    assert.equal(crc, zlibCrc32(data));
    entries.push({ name, method, data: new TextDecoder().decode(data) });
    at += 46 + nameLength;
  }
  return entries;
}

test('zip stores or deflates UTF-8 named entries', async () => {
  const long = '일본어\n'.repeat(200);
  const files = [
    { name: '原文_번역.txt', data: utf8(long) },
    { name: 'b_번역.txt', data: utf8('가') },
  ];
  const date = new Date(2026, 9, 26, 12, 34, 56);
  assert.deepEqual(unzip(await zip(files, date)), [
    { name: '原文_번역.txt', method: 8, data: long },
    { name: 'b_번역.txt', method: 0, data: '가' },
  ]);
  assert.deepEqual(
    unzip(await zip(files, date, false)).map((e) => e.method),
    [0, 0],
  );
  assert.equal(crc32(utf8('123456789')), 0xcbf43926);
  assert.equal((await zip([])).length, 22);
});
