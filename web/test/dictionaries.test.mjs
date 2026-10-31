// node --test web/test/dictionaries.test.mjs, after npm --prefix web run compile.
import assert from 'node:assert/strict';
import { test } from 'node:test';

const { count, detect, fromFile, overlay, toFile } = await import(
  new URL('../../build/web/dictionaries.js', import.meta.url).href
);
const { namesList } = await import(new URL('../../build/web/names.js', import.meta.url).href);

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
  assert.equal(o.userdict, '猫\t냥이\n犬\t멍멍이\n猫\t고양이님');
  assert.equal(o.userdictFile, '猫\t냥이\n犬\t멍멍이\n猫\t고양이님\nユキ\t유키\tname');
  assert.equal(o.names, 'ユキ\t유키\tfull');
  assert.equal(o.ehnd, 'クロムウェル\t크롬웰\t1');
  assert.equal(o.where(false, 2), '위 사용자 사전 2행');
  assert.equal(o.where(false, 3), '아래 사용자 사전 1행');
  assert.equal(o.namesWhere(1), '위 이름 표 1행');
  assert.equal(o.where(true, 1), 'Ehnd 1행');
  const none = overlay([]);
  assert.deepEqual([none.userdict, none.ehnd, none.names, none.userdictFile], ['', '', '', '']);
});

test('overlay keeps malformed rows and line offsets across files and names tables', () => {
  const first = '# comment\n猫\t냥이\n\n犬\t개\txx\n';
  const second = '鳥\t수리새\n東京\t도시\tn\tbogus\n';
  const o = overlay([
    dict('첫째', 'hdtrans', first, 'ユキ\t유키\n蒼月\t아오츠키'),
    dict('둘째', 'hdtrans', second),
    dict('필터 첫째', 'ehnd', '// comment\nポチ\t바둑이\t1\n'),
    dict('필터 둘째', 'ehnd', 'bad\nタマ\t나비\t1'),
    dict('모두 오류', 'hdtrans', '犬\t개\txx\nmalformed'),
  ]);
  assert.equal(o.userdict, first + '\n' + second + '\n犬\t개\txx\nmalformed');
  assert.equal(o.userdictFile, o.userdict + '\nユキ\t유키\tname\n蒼月\t아오츠키\tname');
  assert.equal(o.names, 'ユキ\t유키\tfull\n蒼月\t아오츠키\tfull');
  assert.equal(o.ehnd, '// comment\nポチ\t바둑이\t1\n\nbad\nタマ\t나비\t1');
  assert.equal(o.where(false, 4), '첫째 사용자 사전 4행');
  assert.equal(o.where(false, 6), '둘째 사용자 사전 1행');
  assert.equal(o.where(false, 7), '둘째 사용자 사전 2행');
  assert.equal(o.where(false, 9), '모두 오류 사용자 사전 1행');
  assert.equal(o.namesWhere(2), '첫째 이름 표 2행');
  assert.equal(o.where(true, 4), '필터 둘째 1행');
  assert.equal(o.where(true, 5), '필터 둘째 2행');
});

test('namesList reads a names table as a name list, line for line', () => {
  const table = '// 주인공\nユキ\t유키\n蒼月 아오츠키\n黒沢\t쿠로사와\tsurname\n若葉\t와카바\tname\tprio=2 force\n'
    + 'みみ\t미미\tnick\thon\n星見町\t호시미초\tplace\n星見座\t호시미자\tn\n\n# 끝';
  assert.equal(namesList(table), '# 주인공\nユキ\t유키\tfull\n蒼月\t아오츠키\tfull\n黒沢\t쿠로사와\tfamily\n若葉\t와카바\tfull\n'
    + 'みみ\t미미\tnick\thon\n星見町\t호시미초\tplace\n星見座\t호시미자\torg\n\n# 끝');
  assert.equal(namesList('猫'), '猫');
  assert.equal(namesList('猫\t냥\tverb'), '猫\t냥\tverb');
});

test('overlay puts the names tables of higher dictionaries last, where a name list line wins', () => {
  const o = overlay([
    dict('위', 'hdtrans', '', 'ユキ\t유키'),
    dict('Ehnd', 'ehnd', 'クロムウェル\t크롬웰\t1', ''),
    dict('아래', 'hdtrans', '', 'ユキ\t유끼\n蒼月\t아오츠키'),
  ]);
  assert.equal(o.names, 'ユキ\t유끼\tfull\n蒼月\t아오츠키\tfull\nユキ\t유키\tfull');
  assert.equal(o.namesWhere(2), '아래 이름 표 2행');
  assert.equal(o.namesWhere(3), '위 이름 표 1행');
  assert.equal(o.userdict, '');
});

test('exported names table rows read as user dictionary rows', () => {
  const o = overlay([dict('작품', 'hdtrans', '', '黒沢\t쿠로사와\tfamily\nみみ\t미미\tnick\thon\n星見町\t호시미초\tplace\n若葉\t와카바\tname\tprio=2\n春香')]);
  assert.equal(o.userdictFile, '黒沢\t쿠로사와\tsurname\nみみ\t미미\tname\n星見町\t호시미초\tplace\n若葉\t와카바\tname\tprio=2\n春香');
});
