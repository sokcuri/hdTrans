// node --test web/test/dictionaries.test.mjs, after npm --prefix web run compile.
import assert from 'node:assert/strict';
import { test } from 'node:test';

const { count, detect, fromFile, overlay, toFile } = await import(
  new URL('../../build/web/dictionaries.js', import.meta.url).href
);

const utf8 = (s) => new TextEncoder().encode(s);
const dict = (name, format, entries, names = '', on = true) => ({ id: name, name, format, on, order: 0, entries, names });

test('detect tells an Ehnd UserDict file from an hdTrans user dictionary', () => {
  const cases = [
    ['クロムウェル\t크롬웰#N\t1\tVHn\t//인간\n一時なりとも\t한 때일지라도\t0\n', 'UserDict_a.txt', 'ehnd'],
    ['クロムウェル\t크롬웰\t1\n', 'userdict.txt', 'ehnd'],
    ['魔導具\t마도구\n蒼月\t아오츠키\tsurname\n走り回る\t뛰어다니다\tv5\n', 'userdict.txt', 'hdtrans'],
    ['手伝う\t돕다\tv:ㅂ\n', 'UserDict.txt', 'hdtrans'],
    ['魔導具\t마도구\n', 'userdict.txt', 'hdtrans'],
    ['門前\t문 앞\n', 'UserDict_@Custom.txt', 'ehnd'],
  ];
  for (const [text, file, format] of cases) assert.equal(detect(text, file), format, file + ': ' + text);
});

test('fromFile and toFile round-trip the desktop files', () => {
  const native = '# 메모\n魔導具\t마도구\n蒼月\t아오츠키\tsurname\n';
  const d = fromFile('userdict-판타지.txt', utf8('﻿#! layer=user\n' + native + '\n'));
  assert.deepEqual(d, { name: '판타지', format: 'hdtrans', on: true, entries: native.trimEnd(), names: '' });
  assert.deepEqual(toFile({ ...d, id: 'x', order: 0 }), { name: 'userdict-판타지.txt', text: native });

  const ehnd = 'クロムウェル\t크롬웰#N\t1\tVHn\n// 주석\n門前\t문 앞#M\n';
  const utf16 = new Uint8Array([0xff, 0xfe, ...new Uint8Array(new Uint16Array([...ehnd].map((c) => c.charCodeAt(0))).buffer)]);
  const e = fromFile('UserDict_@Custom.txt', utf16);
  assert.equal(e.format, 'ehnd');
  assert.equal(e.name, '@Custom');
  assert.deepEqual(toFile({ ...e, id: 'x', order: 0 }), { name: 'UserDict_@Custom.txt', text: ehnd });
  assert.equal(count({ ...e, id: 'x', order: 0 }), 2);

  assert.equal(fromFile('userdict.txt', new Uint8Array([0xfe, 0xff, 0, 0x41])), undefined);
});

test('toFile adds the names table as name rows and keeps file names safe', () => {
  const d = dict('a/b', 'hdtrans', '魔導具\t마도구', 'ユキ\t유키\n蒼月 아오츠키');
  assert.deepEqual(toFile(d), { name: 'userdict-a_b.txt', text: '魔導具\t마도구\nユキ\t유키\tname\n蒼月\t아오츠키\tname\n' });
  assert.equal(count(d), 3);
});

test('overlay merges the applied dictionaries in list order and locates rows', () => {
  const list = [
    dict('위', 'hdtrans', '猫\t냥이\n犬\t멍멍이', 'ユキ\t유키'),
    dict('꺼짐', 'hdtrans', '猫\t고양이', '', false),
    dict('Ehnd', 'ehnd', 'クロムウェル\t크롬웰\t1'),
    dict('아래', 'hdtrans', '猫\t고양이님'),
  ];
  const o = overlay(list);
  assert.equal(o.userdict, '猫\t냥이\n犬\t멍멍이\n猫\t고양이님\nユキ\t유키\tname');
  assert.equal(o.ehnd, 'クロムウェル\t크롬웰\t1');
  assert.equal(o.where(false, 2), '위 사용자 사전 2행');
  assert.equal(o.where(false, 3), '아래 사용자 사전 1행');
  assert.equal(o.where(false, 4), '위 이름 표 1행');
  assert.equal(o.where(true, 1), 'Ehnd 1행');
  const none = overlay([]);
  assert.deepEqual([none.userdict, none.ehnd], ['', '']);
});
