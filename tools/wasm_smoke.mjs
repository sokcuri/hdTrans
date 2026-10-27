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
await assert.rejects(init({ strictDict: 'false' }), TypeError);
for (const strictDict of [true, false]) {
    await assert.rejects(init({ directories: ['/missing'], strictDict }), /dictionaries do not load/);
}
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
    const malformed = '学生\t실패\tn\tbogus\n東京\t도시\n犬\t개\txx\n';
    assert.throws(() => translator.setUserDictRaw(malformed), (error) => {
        assert.match(error.message, /^userdict\.tsv:1: .*bogus/m);
        assert.match(error.message, /^userdict\.tsv:3: /m);
        return true;
    });
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
    }, { strict: false });
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

    const mixedRows = [
        { ja: '学生', ko: '부분학우' },
        { ja: '東京', ko: '실패', pos: 'n', options: 'bogus' },
        { ja: '犬', ko: '개', pos: 'xx' },
        { ja: '鳥', ko: '수리새' },
    ];
    const rowsText = (rows) => rows.map((row) => [row.ja, row.ko, row.pos ?? '', row.options ?? '']
        .join('\t').replace(/\t+$/, '')).join('\n') + '\n';
    const goodRows = [mixedRows[0], mixedRows[3]];
    const badRows = mixedRows.slice(1, 3);
    const mixedFilters = {
        'PostFilter_first.txt': '^.+$\t부분필터\t1\t1\n',
        'PreFilter_second.txt': '魔導具\t学生\t1\t0\n(x\tz\t1\t1\n(y\tz\t1\t1\n',
    };
    const badFilters = { 'PreFilter_bad.txt': '(x\tz\t1\t1\n(y\tz\t1\t1\n' };
    for (const strictDict of [undefined, true, false]) {
        const local = await init({ strictDict });
        const strictDefault = strictDict !== false;
        try {
            for (const strict of [true, false]) {
                assert.throws(() => local.setUserDict([{ ja: '学生\n東京', ko: '금지' }], { strict }), TypeError);
                assert.throws(() => local.setUserDict([{ ja: '学生', ko: '금지\0' }], { strict }), TypeError);
                assert.throws(() => local.setUserDictRaw(42, { strict }), TypeError);
                assert.throws(() => local.setEhnd({ 'PreFilter/bad.txt': '' }, { strict }), TypeError);
            }
            for (const typed of [false, true]) {
                const load = (rows, options) => typed
                    ? local.setUserDict(rows, options)
                    : local.setUserDictRaw(rowsText(rows), options);
                load(mixedRows, { strict: false });
                assert.deepEqual(local.getUserDict(), goodRows);
                assert.match(local.userDictError, /^userdict\.tsv:2: .*bogus/m);
                assert.match(local.userDictError, /^userdict\.tsv:3: /m);
                assert.equal(local.translate('学生'), '부분학우');
                local.reset();
                assert.equal(local.translate('鳥'), '수리새');
                if (strictDefault) {
                    assert.throws(() => load(badRows), /bogus/);
                    assert.deepEqual(local.getUserDict(), goodRows, 'false override does not change strict instance policy');
                } else {
                    load(badRows);
                    assert.deepEqual(local.getUserDict(), [], 'all-bad input replaces the active overlay with empty rows');
                    assert.match(local.userDictError, /bogus/);
                }
                load([{ ja: '学生', ko: '기존학우' }]);
                assert.equal(local.userDictError, '');
                assert.throws(() => load(mixedRows, { strict: true }), (error) => {
                    assert.match(error.message, /^userdict\.tsv:2: .*bogus/m);
                    assert.match(error.message, /^userdict\.tsv:3: /m);
                    return true;
                });
                assert.equal(local.getUserDictRaw(), rowsText(mixedRows), 'failed typed/raw submissions retain their submitted text');
                assert.deepEqual(local.getUserDict(), [{ ja: '学生', ko: '기존학우' }]);
                assert.equal(local.translate('学生'), '기존학우');
                if (strictDefault) assert.throws(() => load(mixedRows), /bogus/);
                else {
                    load(mixedRows);
                    assert.deepEqual(local.getUserDict(), goodRows, 'true override does not change lenient instance policy');
                }
                for (const value of ['false', null, 0]) assert.throws(() => load(goodRows, { strict: value }), TypeError);
                local.clearUserDict();
                assert.deepEqual(local.getUserDict(), []);
                assert.equal(local.userDictError, '');
            }
            local.setEhnd(mixedFilters, { strict: false });
            assert.equal(local.translate(probe), '부분필터', 'a bad second file does not discard the good first file');
            assert(local.translate(probe, { json: true }).logs.includes('[PRE] 学生\n\n'), 'good rows of the second file apply');
            assert(local.filterErrors.some((error) => error.startsWith('PreFilter_second.txt:2:')));
            assert(local.filterErrors.some((error) => error.startsWith('PreFilter_second.txt:3:')));
            if (strictDefault) {
                assert.throws(() => local.setEhnd(badFilters), /PreFilter_bad\.txt:1:/);
                assert.equal(local.translate(probe), '부분필터', 'false Ehnd override does not change instance policy');
            } else {
                local.setEhnd(badFilters);
                assert.equal(local.translate(probe), unfiltered, 'all-bad Ehnd replacement clears old filters');
                assert.equal(local.filterErrors.length, 2);
            }
            local.setEhnd({ 'PostFilter_old.txt': '^.+$\t기존필터\t1\t1\n' });
            assert.deepEqual(local.filterErrors, []);
            assert.throws(() => local.setEhnd(mixedFilters, { strict: true }), (error) => {
                assert.match(error.message, /PreFilter_second\.txt:2:/);
                assert.match(error.message, /PreFilter_second\.txt:3:/);
                return true;
            });
            assert.equal(local.translate(probe), '기존필터');
            if (strictDefault) assert.throws(() => local.setEhnd(mixedFilters), /PreFilter_second\.txt:2:/);
            else {
                local.setEhnd(mixedFilters);
                assert.equal(local.translate(probe), '부분필터', 'true Ehnd override does not change instance policy');
            }
            assert.throws(() => local.setEhnd({}, { strict: 'false' }), TypeError);
            local.clearEhnd();
            assert.deepEqual(local.filterErrors, []);
            assert.equal(local.translate(probe), unfiltered);
            const badUserText = `犬\t${'가'.repeat(16)}\t1\n\t실패\t1\n`;
            const userFiles = {
                'UserDict_first.txt': 'ポチ\t바둑이\t1\n',
                'UserDict_second.txt': 'タマ\t나비\t1\n' + badUserText,
            };
            local.setEhnd(userFiles, { strict: false });
            assert.equal(local.translate('ポチ'), '바둑이');
            assert.equal(local.translate('タマ'), '나비', 'good rows in a malformed second Ehnd dictionary still apply');
            assert.equal(local.filterErrors.length, 2);
            assert(local.filterErrors.some((error) => error.startsWith('UserDict_second.txt:2:')));
            assert(local.filterErrors.some((error) => error.startsWith('UserDict_second.txt:3:')));
            const badUserFile = { 'UserDict_bad.txt': badUserText };
            assert.throws(() => local.setEhnd(badUserFile, { strict: true }), (error) => {
                assert.match(error.message, /UserDict_bad\.txt:1:/);
                assert.match(error.message, /UserDict_bad\.txt:2:/);
                return true;
            });
            assert.equal(local.translate('ポチ'), '바둑이', 'strict Ehnd dictionary failure preserves the prior overlay');
            local.setEhnd(badUserFile, { strict: false });
            assert.equal(local.translate('ポチ'), independent.translate('ポチ'));
            assert.equal(local.filterErrors.length, 2);
            local.clearEhnd();
            assert.deepEqual(local.filterErrors, []);
        } finally {
            local.close();
        }
    }
    await assert.rejects(init({ ehnd: mixedFilters }), /PreFilter_second\.txt:2:/);
    const lenientInit = await init({ ehnd: mixedFilters, strictDict: false });
    try {
        assert.equal(lenientInit.translate(probe), '부분필터');
        assert.equal(lenientInit.filterErrors.length, 2);
    } finally {
        lenientInit.close();
    }
    assert.deepEqual(engine.FS.readdir('/').filter((name) => name.startsWith('hdtrans-userdict-')), [], 'memory loads do not stage temporary files');

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
        assert.equal(soft.translate('高い。」'), '높다.」', 'a quote does not carry to the next call');
        soft.reset();
        assert.equal(soft.translate('高い。」'), '높다.」', 'reset keeps calls independent');
        soft.setSpeech('strict');
        assert.equal(soft.translate('「高い」'), '「높다」');
        assert.throws(() => soft.setSpeech('always'), /speech takes soft or strict/);
        assert.equal(soft.translate('「高い」'), '「높다」', 'a rejected level keeps the current one');
        assert.equal(strict.translate('「高い」'), '「높다」');
        soft.setSpeech('soft');
        assert.equal(soft.translate('高い。」', { context: ['「高い。'] }), '높아.」', 'context lines give their quote');
        assert.equal(soft.translate('高い。」'), '높다.」', 'context is not kept after the call');
        assert.equal(soft.translate('高い。」', { json: true, context: ['「高い。'] }).text, '높아.」');
        assert.equal(soft.translate('高い\n高い', { context: ['高い', '高い', ''] }).split('\n').length, 2);
        assert.throws(() => soft.translate('高い', { context: '高い' }), /context must be an array of strings/);
        soft.setWrapped(true);
        assert.equal(soft.translate('高い\n高い', { wrapped: false }).split('\n').length, 2);
        assert.equal(soft.translate('高い\n高い').split('\n').length, 2, 'wrapped keeps the line count');
        assert.throws(() => soft.setWrapped('yes'), /wrapped must be a boolean/);
        const wrapped = await HdTrans.init({ module: engine, directories: ['/speech'], wrapped: true });
        try {
            assert.equal(wrapped.translate('高い\n高い').split('\n').length, 2);
        } finally {
            wrapped.close();
        }
    } finally {
        soft.close();
        strict.close();
    }
    console.log('PASS wasm smoke: speech levels, per-call options, quote carry, context, wrapped');
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
assert.deepEqual(engine.FS.readdir('/').filter((name) => name.startsWith('hdtrans-userdict-')), [], 'memory overlays leave no private files');
console.log('PASS wasm smoke: close, use-after-close, repeated initialization, memory overlays');
