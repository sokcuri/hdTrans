// node web/check.mjs URL INPUT EXPECTED [LINES]
//
// Opens the site at URL in headless Chrome (CHROME, or the default macOS
// install) twice with one profile, and reports for each visit the time
// until the engine is ready, the bytes transferred and the size of the
// engine's WebAssembly memory. On the first visit
// it translates the first LINES (default 20) lines of INPUT in document
// mode (set as the page's input) and checks the result against the file
// EXPECTED byte for byte (the CLI output of the same lines: the page gets
// them without the final newline, so one trailing newline of EXPECTED is
// dropped); then it drives the user dictionary and names table editors
// with key events and a clipboard paste, checks that the entries change the
// translation, and that POST /api/translate is 404. It checks that the
// 대사 말투 select changes quoted speech and is saved. It also checks that
// text in the input before the engine is ready gets translated once it is:
// the sample of the first visit, and text typed during the second visit's
// load, that a long translation shows its progress bar, and the version
// line (업데이트 yyyy-MM-dd HH:mm:ss · commit). With SHOT set it saves a screenshot there. Exits 1 on a failed
// check or a load failure.
import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

const [url, inputFile, expectedFile, count = '20'] = process.argv.slice(2);
if (!expectedFile) {
  console.error('usage: node check.mjs URL INPUT EXPECTED [LINES]');
  process.exit(2);
}
const input = readFileSync(inputFile, 'utf8').split('\n').slice(0, Number(count)).join('\n');
const expected = readFileSync(expectedFile, 'utf8').replace(/\n$/, '');
const chrome = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

const profile = mkdtempSync(join(tmpdir(), 'hdtrans-check-'));
const proc = spawn(chrome, ['--headless', '--disable-gpu', '--no-first-run', `--user-data-dir=${profile}`,
  '--remote-debugging-port=0', 'about:blank'], { stdio: 'ignore' });
let port;
for (let i = 0; i < 100 && !port; i++) {
  await sleep(100);
  try {
    port = readFileSync(join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0];
  } catch {}
}
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));

let id = 0;
const waiting = new Map();
let bytes = 0;
let requests = 0;
ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  if (m.id && waiting.has(m.id)) {
    waiting.get(m.id)(m);
    waiting.delete(m.id);
  } else if (m.method === 'Network.loadingFinished') {
    bytes += m.params.encodedDataLength;
    requests++;
  }
});
const send = (method, params = {}) =>
  new Promise((resolve, reject) => {
    waiting.set(++id, (m) => (m.error ? reject(new Error(m.error.message)) : resolve(m.result)));
    ws.send(JSON.stringify({ id, method, params }));
  });
const evaluate = async (expression) => {
  const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
  return r.result.value;
};
const until = async (expression, ms = 180000) => {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(50)) {
    const v = await evaluate(expression);
    if (v) return v;
  }
  throw new Error('timeout: ' + expression);
};

await send('Network.enable');
await send('Page.enable');

// visit loads the page; early, if given, runs as soon as the input exists
// and gets whether the engine was ready by then.
async function visit(n, early) {
  bytes = 0;
  requests = 0;
  const t0 = Date.now();
  await send('Page.navigate', { url });
  if (early) {
    await until(`document.readyState !== 'loading' && document.getElementById('src') !== null`);
    await early((await evaluate(`document.body.dataset.engine ?? ''`)) === 'ready');
  }
  const state = await until(`document.readyState !== 'loading' && document.body.dataset.engine`);
  const ms = Date.now() - t0;
  const status = await evaluate(`document.getElementById('status').textContent`);
  const memory = Number(await evaluate(`document.body.dataset.memory ?? 0`));
  console.log(`visit ${n}: ${state} in ${(ms / 1000).toFixed(2)} s, ${requests} requests, ${(bytes / 1e6).toFixed(2)} MB transferred, wasm memory ${(memory / 1e6).toFixed(0)} MB; ${status}`);
  if (state !== 'ready') throw new Error(status);
}

// settled waits for the page's translation after the last change and
// returns the output.
const settled = () =>
  evaluate(`(async () => {
    const out = document.getElementById('out');
    while (out.dataset.done !== '1') await new Promise((r) => setTimeout(r, 20));
    return { out: out.value, info: document.getElementById('info').textContent };
  })()`);

async function translate(text) {
  await evaluate(`(() => {
    document.querySelector('input[name=mode][value=document]').checked = true;
    const src = document.getElementById('src');
    src.value = ${JSON.stringify(text)};
    src.dispatchEvent(new Event('input'));
  })()`);
  return settled();
}

const keys = {
  Tab: { code: 'Tab', windowsVirtualKeyCode: 9 },
  Enter: { code: 'Enter', windowsVirtualKeyCode: 13, text: '\r' },
  ArrowUp: { code: 'ArrowUp', windowsVirtualKeyCode: 38 },
  v: { code: 'KeyV', windowsVirtualKeyCode: 86 },
};
async function press(key, modifiers = 0, commands) {
  const k = { key, modifiers, ...keys[key], ...(commands ? { commands } : {}) };
  await send('Input.dispatchKeyEvent', { type: 'keyDown', ...k });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', ...k, text: undefined });
}
const type = (text) => send('Input.insertText', { text });
const gridText = (id) =>
  evaluate(`Array.from(document.querySelectorAll('#${id} tbody tr'), (tr) =>
    Array.from(tr.querySelectorAll('input'), (i) => i.value).join('\\t').replace(/\\t+$/, '')).filter((s) => s.trim()).join('\\n')`);
const focusCell = (id, r, c) =>
  evaluate(`document.querySelectorAll('#${id} tbody tr')[${r}].querySelectorAll('input')[${c}].focus()`);
const activeCell = () =>
  evaluate(`(() => { const td = document.activeElement.closest('td');
    return td ? td.parentElement.sectionRowIndex + ',' + td.cellIndex : null; })()`);

// Types a user dictionary entry with Tab between the cells, moves with
// Shift+Tab, Enter and the arrow keys, pastes a three-row names table from
// the clipboard, copies a row back out, and checks that the entries change
// the translation of a probe text.
async function gridCheck() {
  const probe = 'パレッティア王国の魔物が現れた。ユフィリアとアルガルドは学院に戻った。';
  const ok = (cond, what) => {
    console.log(`grid: ${cond ? 'ok    ' : 'FAILED'} ${what}`);
    failed ||= !cond;
  };
  const base = (await translate(probe)).out;
  console.log(`grid: baseline   ${base}`);

  await focusCell('userdict', 0, 0);
  await type('魔物');
  await press('Tab');
  await type('몬스터');
  ok((await activeCell()) === '0,1', 'Tab moves to the next cell');
  await press('Tab', 8);
  ok((await activeCell()) === '0,0', 'Shift+Tab moves back');
  await press('Tab');
  await press('Enter');
  ok((await activeCell()) === '1,0', 'Enter adds a row and moves to it');
  await press('ArrowUp');
  ok((await activeCell()) === '0,0', 'ArrowUp moves up');
  ok((await gridText('userdict')) === '魔物\t몬스터', 'user dictionary holds 魔物⇥몬스터');
  const one = (await settled()).out;
  console.log(`grid: user dict  ${one}`);
  ok(one.includes('몬스터') && !base.includes('몬스터'), 'user entry 魔物→몬스터 changes the translation');

  const tsv = 'パレッティア\t팔레티아\nユフィリア\t유피\nアルガルド\t알군\tname\n';
  await send('Browser.grantPermissions', { permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });
  await focusCell('names', 0, 0);
  await evaluate(`navigator.clipboard.writeText(${JSON.stringify(tsv)})`);
  await press('v', process.platform === 'darwin' ? 4 : 2, ['paste']);
  ok((await gridText('names')) === tsv.trimEnd(), 'Ctrl+V of a 3-row TSV fills 3 rows');
  const two = (await settled()).out;
  console.log(`grid: + names    ${two}`);
  ok(['팔레티아', '유피', '알군'].every((s) => two.includes(s)), 'pasted names change the translation');

  await focusCell('names', 1, 1);
  const copied = await evaluate(`(() => { const dt = new DataTransfer();
    document.activeElement.setSelectionRange(0, 0);
    document.activeElement.dispatchEvent(new ClipboardEvent('copy', { clipboardData: dt, bubbles: true }));
    return dt.getData('text/plain'); })()`);
  ok(copied === 'ユフィリア\t유피', 'copy without a selection gives the row as TSV');

  const api = await fetch(new URL('api/translate', url), { method: 'POST', body: '{}' });
  ok(api.status === 404, `POST /api/translate answers ${api.status}`);
}

// earlyCheck reports whether the output, once settled, is the translation
// of the input: the same as after the input is set again.
async function earlyCheck(what, wasReady) {
  const first = await settled();
  const src = await evaluate(`document.getElementById('src').value`);
  const again = await translate(src);
  const ok = !wasReady && src !== '' && first.out !== '' && first.out === again.out;
  failed ||= !ok;
  console.log(`early: ${ok ? 'ok    ' : 'FAILED'} ${what} (${src.split('\n').length} lines, ${first.info}${wasReady ? ', engine was already ready' : ''})`);
}

// progressCheck translates INPUT 30 times, all lines made distinct so that
// no earlier translation is reused, and checks that the
// translation's progress bar was shown and is hidden again.
async function progressCheck() {
  const lines = readFileSync(inputFile, 'utf8').split('\n');
  const all = Array.from({ length: 30 }, (_, k) => lines.map((l) => `${k}${l}`)).flat().join('\n');
  await evaluate(`window.tbarSeen = false; new MutationObserver(() => {
    if (!document.getElementById('tbar').hidden) window.tbarSeen = true;
  }).observe(document.getElementById('tbar'), { attributes: true })`);
  const r = await translate(all);
  const seen = await evaluate('window.tbarSeen');
  const hidden = await evaluate(`document.getElementById('tbar').hidden`);
  const ok = seen && hidden;
  failed ||= !ok;
  console.log(`progress: ${ok ? 'ok    ' : 'FAILED'} bar shown during a ${all.split('\n').length}-line translation and hidden after (${r.info})`);
}

// speechCheck sets 대사 말투 to 엄격하게 and back, and checks that quoted speech
// keeps its plain -다 only while strict and that the choice is saved.
async function speechCheck() {
  const pick = (value) => evaluate(`(() => { const s = document.getElementById('speech');
    s.value = '${value}'; s.dispatchEvent(new Event('change')); })()`);
  const soft = (await translate('「明日、海に行く」')).out;
  await pick('strict');
  const strict = (await settled()).out;
  const saved = await evaluate(`localStorage.getItem('hdtrans.speech')`);
  await pick('soft');
  const back = (await settled()).out;
  const ok = soft !== strict && back === soft && saved === 'strict';
  failed ||= !ok;
  console.log(`speech: ${ok ? 'ok    ' : 'FAILED'} soft ${soft}, strict ${strict}, saved ${saved}`);
}

let failed = false;
try {
  let wasReady;
  await visit(1, async (ready) => (wasReady = ready));
  await earlyCheck('first-visit sample translated when the engine became ready', wasReady);
  const r = await translate(input);
  const same = r.out === expected;
  failed ||= !same;
  console.log(`parity: ${same ? 'identical' : 'DIFFERENT'} (${r.info})`);
  if (!same) {
    const a = r.out.split('\n'), b = expected.split('\n');
    const i = a.findIndex((l, k) => l !== b[k]);
    console.log(`  first difference at line ${i + 1}:\n  got:  ${a[i]}\n  want: ${b[i]}`);
  }
  await progressCheck();
  await speechCheck();
  const version = await evaluate(`document.getElementById('version').textContent`);
  const versionOk = /^업데이트 \d{4}-\d\d-\d\d \d\d:\d\d:\d\d · [0-9a-f]{7,}$/.test(version);
  failed ||= !versionOk;
  console.log(`version: ${versionOk ? 'ok    ' : 'FAILED'} ${version}`);
  await gridCheck();
  if (process.env.SHOT) {
    await send('Emulation.setDeviceMetricsOverride', { width: 1200, height: 1300, deviceScaleFactor: 1, mobile: false });
    writeFileSync(process.env.SHOT, Buffer.from((await send('Page.captureScreenshot')).data, 'base64'));
  }
  const typed = '猫が好きです。\n「おはよう、レナ」';
  await visit(2, async (ready) => {
    wasReady = ready;
    await evaluate(`(() => { const src = document.getElementById('src'); src.focus(); src.select(); })()`);
    await type(typed);
  });
  await earlyCheck('text typed during the load translated when the engine became ready', wasReady);
} catch (err) {
  console.log('error:', err.message);
  failed = true;
}
ws.close();
proc.kill();
process.exit(failed ? 1 : 0);
