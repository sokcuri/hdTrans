import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, fixturePath, snapshotPath] = process.argv.slice(2);
assert(modulePath && fixturePath, 'usage: wasm_smoke.mjs MODULE.js DICTIONARY.tsv [SNAPSHOT.hdsnap]');
const wrapper = process.env.HDTRANS_WASM_WRAPPER
    ? pathToFileURL(resolve(process.env.HDTRANS_WASM_WRAPPER))
    : new URL('../web/src/translator.ts', import.meta.url);
const { HdTrans } = await import(wrapper);
const { default: createEngine } = await import(pathToFileURL(resolve(modulePath)));
const engine = await createEngine({
    wasmBinary: await readFile(join(dirname(resolve(modulePath)), 'hdtrans_engine.wasm')),
});
const fixtureBytes = await readFile(fixturePath);
engine.FS.mkdir('/dicts');
engine.FS.writeFile('/dicts/base.tsv', fixtureBytes);
const init = (options = {}) => HdTrans.init({ module: engine, directories: ['/dicts'], ...options });
await assert.rejects(init({ directories: ['/missing'] }), /dictionaries do not load/);
await assert.rejects(init({ directories: [42] }), TypeError);
await assert.rejects(init({ directories: ['/dicts\0hidden'] }), TypeError);
const inspect = (options = {}) => HdTrans.getInfo({ module: engine, directories: ['/dicts'], ...options });
await assert.rejects(inspect({ directories: ['/missing'] }), /dictionaries do not load/);
await assert.rejects(inspect({ directories: [42] }), TypeError);
assert.deepEqual(await inspect(), {
    version: engine.version(), dictionaryVersion: 'development (unversioned)', runtime: 'wasm',
});
assert.deepEqual(engine.FS.readdir('/').filter((name) => name.startsWith('hdtrans-userdict-')), []);
const unversioned = await init();
assert.equal(unversioned.dictionaryVersion, 'development (unversioned)');
unversioned.close();
engine.FS.writeFile('/dicts.txt', 'fixture-v1\r\nignored\n');
const info = await inspect();
assert.deepEqual(info, { version: engine.version(), dictionaryVersion: 'fixture-v1', runtime: 'wasm' });
assert.deepEqual(await inspect({ module: Promise.resolve(engine) }), info);
assert.deepEqual(await inspect({ ehnd: { 'PostFilter_info.txt': '^.+$\t정보조회\t1\t1\n' } }), info);
await assert.rejects(inspect({ ehnd: { 'bad/name.txt': '' } }), TypeError);
assert.deepEqual(engine.FS.readdir('/').filter((name) => name.startsWith('hdtrans-userdict-')), []);
const translator = await init();
const independent = await init();
const text = '学生';
try {
    const base = translator.translate(text);
    assert(base.length > 0);
    assert.equal(typeof translator.version, 'string');
    assert(translator.version.length > 0);
    assert.equal(translator.dictionaryVersion, 'fixture-v1');
    assert.equal(translator.runtime, 'wasm');
    assert.deepEqual(translator.getInfo(), info);
    translator.getInfo().dictionaryVersion = 'changed copy';
    assert.deepEqual(translator.getInfo(), info);
    engine.FS.writeFile('/dicts.txt', 'fixture-v2\n');
    assert.equal((await inspect()).dictionaryVersion, 'fixture-v2');
    assert.deepEqual(translator.getInfo(), info, 'metadata belongs to the dictionary loaded by this instance');
    assert.deepEqual(independent.getInfo(), info);
    engine.FS.writeFile('/dicts.txt', 'fixture-v1\n');
    assert.deepEqual(translator.getUserDict(), []);
    assert.deepEqual(translator.filterErrors, []);
    assert.equal(translator.translate(''), '');
    translator.reset();
    const emoji = translator.translate(`${text}😀`);
    assert(emoji.startsWith(base) && emoji.endsWith('😀'), emoji);
    translator.reset();
    assert.equal(translator.translate(`${text}😀\n\n${text}\r\n`), `${emoji}\n\n${base}\r\n`);
    assert(translator.translate('A\0B').includes('B'), 'embedded NUL must not truncate input');
    for (let i = 0; i < 30; ++i) {
        translator.reset();
        assert.equal(translator.translate(text), base);
    }

    const typed = [{ ja: text, ko: '학우', pos: 'n', options: 'prio=2 force' }];
    translator.setUserDict(typed);
    assert.equal(translator.translate(text), '학우');
    assert.deepEqual(translator.getUserDict(), typed);
    assert.equal(translator.getUserDictRaw(), '学生\t학우\tn\tprio=2 force\n');
    assert.equal(independent.translate(text), base);
    typed[0].ko = '외부수정';
    const copy = translator.getUserDict();
    copy[0].ko = '복사수정';
    copy.push({ ja: '東京', ko: '추가수정' });
    assert.equal(translator.translate(text), '학우');
    assert.equal(translator.getUserDict()[0].ko, '학우');
    assert.equal(translator.getUserDict().length, 1);
    assert.throws(() => translator.setUserDict([{ ja: '学生\n東京', ko: '금지' }]), TypeError);
    assert.equal(translator.translate(text), '학우');

    const raw = '﻿# comment\n  // comment\n学生\t동료\tn\tprio=3 force\t// trailing\n'
        + '学生\t학우\tn\tprio=1\n蒼月\t아오츠키\tsurname\tprio=2\r\n'
        + '書く\t쓰다\tv\nたん\t\thon\n';
    translator.setUserDictRaw(raw);
    const registered = [
        { ja: text, ko: '동료', pos: 'n', options: 'prio=3 force' },
        { ja: text, ko: '학우', pos: 'n', options: 'prio=1' },
        { ja: '蒼月', ko: '아오츠키', pos: 'surname', options: 'prio=2' },
        { ja: '書く', ko: '쓰다', pos: 'v' },
        { ja: 'たん', ko: '', pos: 'hon' },
    ];
    assert.deepEqual(translator.getUserDict(), registered, 'native source rows must not expand conjugation variants');
    assert.equal(translator.getUserDictRaw(), raw);
    assert.equal(translator.userDictError, '');
    assert.equal(translator.translate(text), '동료');
    const malformed = '学生\t실패\tn\tbogus\n東京\t도시\n';
    translator.setUserDictRaw(malformed);
    assert.match(translator.userDictError, /^userdict\.tsv:1: .*bogus/m);
    assert.equal(translator.getUserDictRaw(), malformed);
    assert.deepEqual(translator.getUserDict(), registered, 'failed whole-file update retains the active rows');
    translator.reset();
    assert.equal(translator.translate(text), '동료');
    translator.clearUserDict();
    assert.equal(translator.getUserDictRaw(), '');
    assert.deepEqual(translator.getUserDict(), []);
    assert.equal(translator.userDictError, '');
    assert.equal(translator.translate(text), base);
    translator.setUserDict([{ ja: text, ko: '학우' }]);
    translator.setUserDict([]);
    assert.equal(translator.translate(text), base);

    const probe = '魔導具';
    const unfiltered = translator.translate(probe);
    translator.setEhnd({
        'PreFilter_A.txt': '魔導具\t学生\t1\t0\n',
        'PostFilter_A.txt': `${base}\t마도구\t1\t0\n`,
        'UserDict_A.txt': 'ポチ\t바둑이\t1\n',
    });
    assert.equal(translator.translate(probe), '마도구');
    const filterTrace = translator.translate(probe, { json: true });
    assert.equal(filterTrace.text, '마도구');
    assert(filterTrace.logs.includes(`[REQUEST] ${probe}\n\n`));
    assert(filterTrace.logs.includes('[PRE] 学生\n\n'));
    assert(filterTrace.logs.includes(`[TRANS] ${base}\n\n`));
    assert(filterTrace.logs.includes('[POST] 마도구\n\n'));
    assert(filterTrace.logs.some((line) => line.startsWith('PreFilter : [PreFilter_A.txt:0]')));
    assert(filterTrace.logs.some((line) => line.startsWith('PostFIlter : [PostFilter_A.txt:0]')));
    assert.equal(translator.translate('ポチ'), '바둑이');
    assert.equal(independent.translate(probe), unfiltered);
    assert.throws(() => translator.setEhnd({ 'PreFilter/invalid.txt': '' }), TypeError);
    assert.equal(translator.translate(probe), '마도구', 'invalid filenames must not mutate active filters');
    translator.setEhnd({ 'PreFilter_B.txt': '魔導具\t東京\t1\t0\n' });
    assert.equal(translator.translate(probe), independent.translate('東京'), 'replacement removes the old postfilter');
    const replacedTrace = translator.translate(probe, { json: true });
    assert(replacedTrace.logs.some((line) => line.includes('PreFilter_B.txt:0')));
    assert(!replacedTrace.logs.some((line) => line.includes('_A.txt')));
    assert.equal(translator.translate('ポチ'), independent.translate('ポチ'), 'replacement removes the old Ehnd dictionary');
    translator.clearEhnd();
    assert.equal(translator.translate(probe), unfiltered);
    assert(!translator.translate(probe, { json: true }).logs.some((line) => /^(PreFilter|PostFIlter) :/.test(line)));
    translator.setUserDict([{ ja: text, ko: '학우' }]);
    translator.setEhnd({
        'PreFilter_live.txt': '魔導具\t学生\t1\t0\n(x\tz\t1\t1\n',
        'PostFilter_live.txt': '학우\t필터값\t1\t0\n',
    });
    assert.equal(translator.translate(probe), '필터값');
    assert(translator.filterErrors.some((error) => error.startsWith('PreFilter_live.txt:2: regex rejected')));
    const diagnosticsTrace = translator.translate(probe, { json: true });
    assert(diagnosticsTrace.logs.some((line) => line.includes('PreFilter_live.txt:0')));
    assert(!diagnosticsTrace.logs.some((line) => line.includes('regex rejected')));
    assert(!translator.translate('無関係', { json: true }).logs.some((line) => /^(PreFilter|PostFIlter) :/.test(line)));
    assert.equal(translator.getUserDict()[0].ko, '학우');
    translator.setUserDictRaw('学生\t동료\n');
    assert.equal(translator.translate(probe), '동료', 'normal dictionary changes preserve Ehnd prefilters');
    translator.clearEhnd();
    assert.equal(translator.translate(text), '동료', 'clearing Ehnd preserves the normal overlay');
    assert.deepEqual(translator.filterErrors, []);
    translator.clearUserDict();
    assert.equal(translator.translate(text), base);
    const configured = await init({ ehnd: { 'PostFilter_init.txt': `${base}\t설정값\t1\t0\n` } });
    try {
        assert.equal(configured.translate(text), '설정값');
        assert.equal(independent.translate(text), base);
        configured.clearEhnd();
        assert.equal(configured.translate(text), base);
    } finally {
        configured.close();
    }

    assert.equal(translator.dictionaryVersion, 'fixture-v1', 'overlays and filters do not change the base version');
    const document = '学生の学生\n行く';
    translator.reset();
    const ordinary = translator.translate(document);
    translator.reset();
    const explained = translator.translate(document, { json: true });
    assert.equal(explained.text, ordinary);
    assert(Array.isArray(explained.choices) && explained.choices.length > 0);
    const student = explained.choices[0];
    assert.equal(student.surface, '学生');
    assert.equal(student.choice.text, '학생');
    assert.equal(student.choice.provenance, 'Digit');
    assert.equal(student.choice.layer, 'defaults');
    assert.equal(student.choice.index, 0);
    assert.equal(student.line, 0);
    assert.equal(student.start, 0);
    assert.equal(explained.choices[1].start, 6, 'offsets count UTF-8 bytes, not UTF-16 code units');
    assert(explained.choices.some((choice) => choice.line === 1));
    for (const choice of explained.choices) {
        assert(Number.isInteger(choice.chunk) && Number.isInteger(choice.token) && Number.isInteger(choice.start));
        for (const selection of [choice.choice, choice.ending].filter(Boolean)) {
            assert.equal(typeof selection.provenance, 'string');
            if (selection.provenance === 'LocalRule' || selection.provenance === 'GlobalRule') {
                assert.match(selection.source, /\.txt:\d+$/);
            }
        }
    }
    const retained = structuredClone(explained);
    translator.reset();
    assert.equal(translator.translate(document, { json: false }), ordinary);
    assert.deepEqual(explained, retained, 'later calls must not mutate returned trace objects');
    assert.equal(independent.translate(text), base);

    engine.FS.mkdir('/trace');
    engine.FS.writeFile('/trace/base.tsv', 'key\tko\tpos\tsem\tgram\tdict\trule\tctx\tid\n'
        + '東京\t동경\tN130\tQPNXXX\t\tGN\t0\t\t1\n'
        + '東京\t도쿄\tN130\tQCT\t\tGN\t9\t||1 S+1== "、"\t2\n'
        + '犬\t개\tN110\tXXX\t\tGN\t0\t\t3\n書\t쓰\tM421\tXXX\t\tGN\t0\t\t4\n'
        + 'V\t4다\tS500\tXXX\t\tGN\t0\t\t5\nV\t4는다\tS500\t\t\tGN\t2\t||1 G+0== "Ow"\t6\n');
    engine.FS.writeFile('/trace/pre.tsv', '#! layer=prefilter\n道具\t東京\t1\t0\n');
    engine.FS.writeFile('/trace/post.tsv', '#! layer=postfilter\n도쿄\t도시\t1\t0\n');
    const traced = await init({ directories: ['/trace'] });
    try {
        const selected = traced.translate('東京、', { json: true });
        assert.equal(selected.text, '도시,');
        const tokyo = selected.choices[0].choice;
        assert.equal(tokyo.index, 1);
        assert.equal(tokyo.text, '도쿄');
        assert.equal(tokyo.provenance, 'LocalRule');
        assert.equal(tokyo.id, 9);
        assert.equal(tokyo.layer, 'local');
        // A local rule lives in the row that names it, so its number is its source.
        assert.equal(tokyo.source, 'L9');
        const ending = traced.translate('書く。', { json: true }).choices.find((choice) => choice.ending)?.ending;
        assert(ending);
        assert.equal(ending.index, 1);
        assert.equal(ending.text, '4는다');
        assert.equal(ending.provenance, 'LocalRule');
        assert.equal(ending.id, 2);
        assert.equal(ending.layer, 'local-ending');
        assert.equal(ending.source, 'L2');
        assert.equal(traced.translate('道具、'), '도시,');
        traced.setEhnd({ 'PostFilter_extra.txt': '도시\t확장\t1\t0\n' });
        assert.equal(traced.translate('道具、'), '확장,');
        traced.clearEhnd();
        assert.equal(traced.translate('道具、'), '도시,', 'clearing Ehnd preserves shipped pre/post filters');
        traced.setEhnd({
            'PreFilter_toggle.txt': '_TGL\t_DECR:T_\t_OFF\n_TGL\t_ON:T_\t猫\t_MATCH_CHECK\t1\t0\n猫\t犬\t2\t0\n',
            'PostFilter_toggle.txt': '_TGL\t_CONN:T_\t개\t멍멍이\t1\t0\n',
        });
        const toggled = traced.translate('猫', { json: true });
        assert.equal(toggled.text, '멍멍이');
        assert(toggled.logs.includes('토글 변수 T_의 상태가 ON으로 변경.\n'));
        assert(toggled.logs.some((line) => line.includes('[PreFilter_toggle.txt:1] | 猫 | _MATCH_CHECK | 1 | 0')));
        assert(toggled.logs.some((line) => line.includes('[PostFilter_toggle.txt:0]')));
    } finally {
        traced.close();
    }
    console.log(`PASS wasm smoke: strings, unicode, multiline, lifecycle, userdict, live Ehnd logs, structured choices; ${text} -> ${base}`);

    if (snapshotPath) {
        engine.FS.writeFile('/dicts.hdsnap', await readFile(snapshotPath));
        engine.FS.unlink('/dicts/base.tsv');
        engine.FS.rmdir('/dicts');
        assert.deepEqual(await inspect({ directories: ['/dicts.hdsnap'] }), info);
        const compiled = await init({ directories: ['/dicts.hdsnap'] });
        try {
            assert.deepEqual(compiled.getInfo(), info);
            assert.equal(compiled.translate(text), base);
            assert.equal(compiled.dictionaryVersion, 'fixture-v1');
            compiled.setUserDict([{ ja: text, ko: '학우' }]);
            assert.equal(compiled.translate(text), '학우');
            compiled.clearUserDict();
            assert.equal(compiled.translate(text), base);
        } finally {
            compiled.close();
            engine.FS.mkdir('/dicts');
            engine.FS.writeFile('/dicts/base.tsv', fixtureBytes);
        }
        console.log(`PASS wasm smoke: standalone snapshot without source TSVs, ${text} -> ${base}`);
    }
} finally {
    translator.close();
    independent.close();
}
{
    // 高い as 높다: quoted speech is 높아 unless speech is strict.
    engine.FS.mkdir('/speech');
    engine.FS.writeFile('/speech/base.tsv', 'key\tko\tpos\tsem\tgram\tdict\trule\tid\n'
        + '高\t$D3@\tH7N0\t\t\tGN\t0\t1\nい\t1다\tT500\t\t\tGN\t0\t2\n');
    const soft = await HdTrans.init({ module: engine, directories: ['/speech'], speech: 'soft' });
    const strict = await HdTrans.init({ module: engine, directories: ['/speech'] });
    try {
        assert.equal(soft.translate('「高い」'), '「높아」');
        assert.equal(soft.translate('「高い」', { speech: 'strict' }), '「높다」');
        assert.equal(soft.translate('「高い」'), '「높아」', 'a per-call level applies to that call only');
        assert.equal(soft.translate('高い。'), '높다.', 'narration keeps its plain end');
        assert.equal(soft.translate('「高い。'), '「높아.');
        assert.equal(soft.translate('高い」'), '높아」', 'a quote carries to the next call');
        soft.reset();
        assert.equal(soft.translate('高い」'), '높다」', 'reset forgets the open quote');
        soft.setSpeech('strict');
        assert.equal(soft.translate('「高い」'), '「높다」');
        assert.throws(() => soft.setSpeech('always'), /speech takes soft or strict/);
        assert.equal(soft.translate('「高い」'), '「높다」', 'a rejected level keeps the current one');
        assert.equal(strict.translate('「高い」'), '「높다」');
    } finally {
        soft.close();
        strict.close();
    }
    console.log('PASS wasm smoke: speech levels, per-call option, quote carry');
}
translator.close();
assert.deepEqual(translator.getInfo(), info);
assert.equal(translator.version, info.version);
assert.equal(translator.dictionaryVersion, info.dictionaryVersion);
for (const call of [() => translator.translate(text), () => translator.translate(text, { json: true }),
    () => translator.reset(), () => translator.setUserDict([]), () => translator.getUserDict(),
    () => translator.getUserDictRaw(), () => translator.clearUserDict(), () => translator.setEhnd({}),
    () => translator.clearEhnd(), () => translator.userDictError, () => translator.filterErrors]) {
    assert.throws(call, /translator is closed/);
}
for (let i = 0; i < 5; ++i) {
    const reopened = await init();
    assert(reopened.translate(text));
    reopened.close();
}
assert.deepEqual(engine.FS.readdir('/').filter((name) => name.startsWith('hdtrans-userdict-')), [], 'close must remove private files');
console.log('PASS wasm smoke: close, use-after-close, repeated initialization, private file cleanup');
