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
// 대사 말투 select changes quoted speech and is saved, and that each 출력 mode
// shows and copies the output in that format. It also checks that
// text in the input before the engine is ready gets translated once it is:
// the sample of the first visit, and text typed during the second visit's
// load, that a long translation shows its progress bar, and the version
// under the title (vVERSION (commit, yyyy-MM-dd)). Then it checks the
// dictionaries: that the user dictionary and names table kept in localStorage
// move into 기본 사용자 사전 on the first run with IndexedDB, and, at a desktop
// and a mobile viewport, the 사전 tab's layout (list beside or above the
// grids, grids that scroll sideways on a phone, 44 px touch targets), adding a
// dictionary and an entry, turning it off and on, priority by list order, a
// load error naming the dictionary and row, export and import of the
// desktop files (userdict*.txt and Ehnd UserDict*.txt) byte for byte, renaming,
// deleting, and that the list survives a reload. With SHOT set it saves
// screenshots there (text tab) and next to it (-dictionaries, -mobile). Exits 1
// on a failed check or a load failure.
import { spawn } from 'node:child_process';
import { mkdirSync, mkdtempSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { inflateRawSync } from 'node:zlib';
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
let promptText = ''; // the answer to the page's next prompt; confirms are accepted
let lastDialog = '';
let bytes = 0;
let requests = 0;
ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  if (m.id && waiting.has(m.id)) {
    waiting.get(m.id)(m);
    waiting.delete(m.id);
  } else if (m.method === 'Page.javascriptDialogOpening') {
    lastDialog = m.params.message;
    ws.send(JSON.stringify({ id: ++id, method: 'Page.handleJavaScriptDialog', params: { accept: true, promptText } }));
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
  const statusOk = /^준비 완료 · [\d.]+초 \(로딩 [\d.]+초, 메모리 \d+ MB\)$/.test(status);
  failed ||= !statusOk;
  if (!statusOk) console.log(`status: FAILED ${status}`);
}

// settled waits for the page's translation after the last change and
// returns the output.
const settled = () =>
  evaluate(`(async () => {
    const out = document.getElementById('out');
    while (out.dataset.done !== '1') await new Promise((r) => setTimeout(r, 20));
    return { out: out.textContent, info: document.getElementById('info').textContent, warning: document.getElementById('warning').textContent };
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
  const infoOk = /^\d+줄 · \d+ ms$/.test(r.info) &&
    (await evaluate(`document.getElementById('info').closest('.pane')?.contains(document.getElementById('out')) ?? false`));
  failed ||= !infoOk;
  console.log(`info: ${infoOk ? 'ok    ' : 'FAILED'} line count and time in the 한국어 pane title (${r.info})`);
}

// speechCheck sets 대사 말투 to 부드럽게 and back, and checks that quoted speech
// is softened only while soft and that the choice is saved.
async function speechCheck() {
  const pick = (value) => evaluate(`(() => { const s = document.getElementById('speech');
    s.value = '${value}'; s.dispatchEvent(new Event('change')); })()`);
  const strict = (await translate('「明日、海に行く」')).out;
  await pick('soft');
  const soft = (await settled()).out;
  const saved = await evaluate(`localStorage.getItem('hdtrans.speech')`);
  await pick('strict');
  const back = (await settled()).out;
  const ok = soft !== strict && back === strict && saved === 'soft';
  failed ||= !ok;
  console.log(`speech: ${ok ? 'ok    ' : 'FAILED'} soft ${soft}, strict ${strict}, saved ${saved}`);
}

// outputCheck shows a two-line translation in each 출력 mode and checks the
// shown text, the muted source lines, 복사 and that the choice is saved.
async function outputCheck() {
  const ok = say('output');
  const pick = (value) => evaluate(`(() => { const s = document.getElementById('outmode');
    s.value = '${value}'; s.dispatchEvent(new Event('change')); })()`);
  const src = '猫が来た。\n犬が来た。';
  const [a, b] = (await translate(src)).out.split('\n');
  const want = { translation: `${a}\n${b}`, interleaved: `猫が来た。\n${a}\n犬が来た。\n${b}`, spaced: `猫が来た。\n${a}\n\n犬が来た。\n${b}` };
  await send('Browser.grantPermissions', { permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });
  await send('Emulation.setFocusEmulationEnabled', { enabled: true });
  for (const mode of ['interleaved', 'spaced', 'translation']) {
    await pick(mode);
    const shown = await evaluate(`document.getElementById('out').textContent`);
    const muted = await evaluate(`[...document.querySelectorAll('#out .src')].map((e) => e.textContent).join('|')`);
    await until(`document.getElementById('copy').textContent === '복사'`);
    await click('#copy');
    const label = await until(`document.getElementById('copy').textContent !== '복사' && document.getElementById('copy').textContent`);
    const copied = await evaluate(`navigator.clipboard.readText()`);
    ok(shown === want[mode] && label === '복사됨' && copied === shown && muted === (mode === 'translation' ? '' : '猫が来た。\n|犬が来た。\n'),
      `${mode}: shown and copied ${JSON.stringify(shown)}`);
  }
  await pick('spaced');
  ok((await evaluate(`localStorage.getItem('hdtrans.output')`)) === 'spaced', 'the choice is saved');
  await pick('translation');
}

// The 사전 tab. Files go through folders of a temporary directory: in/
// for imports, dl/ for downloads.
const files = mkdtempSync(join(tmpdir(), 'hdtrans-check-files-'));
mkdirSync(join(files, 'in'));
const desktop = { width: 1200, height: 1300, deviceScaleFactor: 1, mobile: false };
const phone = { width: 390, height: 844, deviceScaleFactor: 2, mobile: true };
const say = (area) => (cond, what) => {
  console.log(`${area}: ${cond ? 'ok    ' : 'FAILED'} ${what}`);
  failed ||= !cond;
};
async function shot(suffix, metrics = desktop) {
  if (!process.env.SHOT) return;
  await send('Emulation.setDeviceMetricsOverride', metrics);
  writeFileSync(process.env.SHOT.replace(/(\.png)?$/, `${suffix}.png`), Buffer.from((await send('Page.captureScreenshot')).data, 'base64'));
}
const click = (selector) => evaluate(`document.querySelector(${JSON.stringify(selector)}).click()`);
const row = (name) => `[...document.querySelectorAll('#dictlist li')].find((li) => li.querySelector('.name').textContent === ${JSON.stringify(name)})`;
const clickRow = (name, selector) => evaluate(`${row(name)}.querySelector(${JSON.stringify(selector)}).click()`);
const dictRows = () =>
  evaluate(`[...document.querySelectorAll('#dictlist li')].map((li) => [li.querySelector('.name').textContent,
    li.querySelector('input').checked ? 'on' : 'off', li.querySelector('.count').textContent,
    li.querySelector('.fmt') ? 'Ehnd' : '', li.classList.contains('sel') ? 'selected' : ''].filter((s) => s).join(' '))`);
const dictsReady = () => until(`document.body.dataset.engine === 'ready' && document.body.dataset.dicts === 'ready'`);
const probe = async (text) => (await translate(text)).out;

/** The files of a ZIP archive by name, as text. */
function unzip(b) {
  const files = {};
  const end = b.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
  for (let p = b.readUInt32LE(end + 16), n = b.readUInt16LE(end + 10); n > 0; n--) {
    const method = b.readUInt16LE(p + 10), size = b.readUInt32LE(p + 20), nameLength = b.readUInt16LE(p + 28);
    const local = b.readUInt32LE(p + 42);
    const name = b.subarray(p + 46, p + 46 + nameLength).toString('utf8');
    const start = local + 30 + b.readUInt16LE(local + 26) + b.readUInt16LE(local + 28);
    const data = b.subarray(start, start + size);
    files[name] = (method === 8 ? inflateRawSync(data) : data).toString('utf8');
    p += 46 + nameLength + b.readUInt16LE(p + 30) + b.readUInt16LE(p + 32);
  }
  return files;
}

/** Downloads by clicking selector (in the row of name, if given) and returns the files' texts by name. */
async function download(selector, name) {
  const dir = mkdtempSync(join(files, 'dl-'));
  await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: dir });
  await (name ? clickRow(name, selector) : click(selector));
  const got = {};
  for (const end = Date.now() + 10000; Date.now() < end && Object.keys(got).length === 0; await sleep(50)) {
    for (const f of readdirSync(dir)) {
      if (f.endsWith('.crdownload')) continue;
      const b = readFileSync(join(dir, f));
      got[f] = f.endsWith('.zip') ? unzip(b) : b.toString('utf8');
    }
  }
  return got;
}

/** Imports the files (name → text) through the 가져오기 file input. */
async function upload(texts) {
  const paths = Object.entries(texts).map(([n, t]) => (writeFileSync(join(files, 'in', n), t), join(files, 'in', n)));
  const { root } = await send('DOM.getDocument');
  const { nodeId } = await send('DOM.querySelector', { nodeId: root.nodeId, selector: '#dict-file' });
  await send('DOM.setFileInputFiles', { nodeId, files: paths });
}

// migrationCheck stores a user dictionary and names table as the page kept
// them before IndexedDB, removes the database and opens the page.
async function migrationCheck() {
  const ok = say('migration');
  await send('Page.navigate', { url: new URL('manifest.json', url).href });
  await until(`document.readyState === 'complete'`);
  await evaluate(`new Promise((resolve, reject) => { const r = indexedDB.deleteDatabase('hdtrans');
    r.onsuccess = resolve; r.onerror = () => reject(r.error); r.onblocked = () => reject(new Error('blocked')); })`);
  await evaluate(`localStorage.setItem('hdtrans.userdict', '猫\\t고양이님'); localStorage.setItem('hdtrans.names', 'ユフィリア\\t유피');
    localStorage.removeItem('hdtrans.edit'); localStorage.setItem('hdtrans.tab', 'text')`);
  await send('Page.navigate', { url });
  await dictsReady();
  const rows = await dictRows();
  ok(rows.join() === '기본 사용자 사전 on 2개 selected', `one dictionary from localStorage: ${rows.join(' / ')}`);
  ok((await gridText('userdict')) === '猫\t고양이님' && (await gridText('names')) === 'ユフィリア\t유피', 'its grids hold the old entries');
  const out = await probe('猫とユフィリアが来た。');
  ok(out.includes('고양이님') && out.includes('유피'), `the entries apply: ${out}`);
}

// libraryCheck drives the 사전 tab at a desktop viewport.
async function libraryCheck() {
  const ok = say('dictionaries');
  await send('Emulation.setDeviceMetricsOverride', desktop);
  await click('#tab-dictionaries');
  const layout = await evaluate(`(() => { const l = document.getElementById('dictlist').getBoundingClientRect();
    const e = document.getElementById('dictedit').getBoundingClientRect();
    return { side: e.left >= l.right && Math.abs(e.top - l.top) < 80, inside: document.getElementById('dictedit').contains(document.getElementById('userdict')),
      hidden: document.getElementById('text').hidden }; })()`);
  ok(layout.side && layout.inside && layout.hidden, 'the tab shows the list beside the grids of the selected dictionary');

  await click('#dict-new');
  await until(`document.querySelectorAll('#dictlist li').length === 2`);
  ok((await dictRows()).join() === '기본 사용자 사전 on 2개,새 사전 on 0개 selected', '+ 새 사전 adds an empty applied dictionary and selects it');
  await focusCell('userdict', 0, 0);
  await type('猫');
  await press('Tab');
  await type('냥이');
  ok((await dictRows())[1] === '새 사전 on 1개 selected', 'an entry typed in its grid counts');
  let out = await probe('猫が来た。');
  ok(out.includes('고양이님'), `the first dictionary wins a shared key: ${out}`);
  await clickRow('새 사전', 'button[title="위로"]');
  ok((await dictRows()).join() === '새 사전 on 1개 selected,기본 사용자 사전 on 2개', '↑ moves it first');
  out = (await settled()).out;
  ok(out.includes('냥이'), `then its entry wins: ${out}`);
  await clickRow('새 사전', 'input');
  out = (await settled()).out;
  ok(out.includes('고양이님') && (await dictRows())[0] === '새 사전 off 1개 selected', `unchecked, it no longer applies: ${out}`);
  await clickRow('새 사전', 'input');
  out = (await settled()).out;
  ok(out.includes('냥이'), `checked again, it applies: ${out}`);

  await focusCell('userdict', 0, 3);
  await press('Enter');
  await type('犬');
  await press('Tab');
  await type('개');
  await press('Tab');
  await type('xx');
  await focusCell('userdict', 1, 3);
  await press('Enter');
  await type('鳥');
  await press('Tab');
  await type('수리새');
  out = await probe('鳥が来た。');
  const bad = (await settled()).warning;
  ok(bad.includes('새 사전 사용자 사전 2행'), `a load error names the dictionary and row: ${bad}`);
  ok(out.includes('수리새'), `a good new row after a bad row applies: ${out}`);
  ok(await evaluate(`document.getElementById('warning').getBoundingClientRect().height > 0`), 'warnings are visible on the dictionary tab');
  await click('#tab-docs');
  ok(await evaluate(`document.getElementById('warning').getBoundingClientRect().height > 0`), 'warnings are visible on the document tab');
  await evaluate(`(() => {
    const input = document.getElementById('files');
    const files = new DataTransfer();
    files.items.add(new File(['鳥が来た。'], 'document.txt', { type: 'text/plain' }));
    input.files = files.files;
    input.dispatchEvent(new Event('change'));
  })()`);
  await until(`!document.getElementById('run').disabled`);
  await click('#run');
  await until(`document.querySelector('#filelist tbody tr td:nth-child(3)').textContent.startsWith('완료')`);
  const documentWarning = await evaluate(`document.getElementById('warning').textContent`);
  ok(documentWarning.includes('새 사전 사용자 사전 2행'), `completed documents retain the dictionary warning: ${documentWarning}`);
  const documentFile = await download('#filelist tbody tr button');
  ok(Object.values(documentFile).some((text) => text.includes('수리새')), 'a document with warnings still saves the good-row translation');
  await click('#tab-dictionaries');
  await evaluate(`document.querySelectorAll('#userdict tbody tr')[2].querySelector('button.del').click()`);
  await evaluate(`document.querySelectorAll('#userdict tbody tr')[0].querySelector('button.del').click()`);
  out = await probe('猫が来た。ユフィリアが来た。');
  ok(out.includes('고양이님') && out.includes('유피') && !out.includes('냥이'), `an all-bad file clears its old rows without discarding another good dictionary: ${out}`);
  ok((await settled()).warning.includes('새 사전 사용자 사전 1행'), 'the all-bad dictionary still warns');
  await evaluate(`document.querySelectorAll('#userdict tbody tr')[0].querySelector('button.del').click()`);
  await focusCell('userdict', 0, 0);
  await type('猫');
  await press('Tab');
  await type('냥이');
  ok(!(await settled()).warning.includes('오류'), 'a valid replacement clears the warning');

  await evaluate(`(() => {
    const read = File.prototype.arrayBuffer;
    globalThis.restoreFileRead = () => { File.prototype.arrayBuffer = read; delete globalThis.restoreFileRead; };
    File.prototype.arrayBuffer = function () {
      if (this.name === 'userdict-unreadable.txt') return Promise.reject(new DOMException('Cannot read file', 'NotReadableError'));
      return read.call(this);
    };
  })()`);
  lastDialog = '';
  try {
    await upload({ 'userdict-unreadable.txt': '猫\t실패\n', 'userdict-readable.txt': '鳥\t수리새\n' });
    await until(`document.querySelector('#dictlist li.sel .name')?.textContent === 'readable'`);
  } finally {
    await evaluate(`globalThis.restoreFileRead()`);
  }
  ok(lastDialog === '읽지 못함: userdict-unreadable.txt', `a file read failure reports its filename, not a row warning: ${lastDialog}`);
  ok((await dictRows()).length === 3 && (await probe('鳥が来た。')).includes('수리새'), 'other readable files in the upload still apply');
  await clickRow('readable', 'button[title="삭제"]');
  await clickRow('새 사전', '.name');
  await settled();

  const exported = await download('button[title="내보내기"]', '새 사전');
  ok(exported['userdict-새 사전.txt'] === '猫\t냥이\n', `a dictionary exports as userdict-새 사전.txt: ${JSON.stringify(exported)}`);
  await upload({ 'userdict-새 사전.txt': exported['userdict-새 사전.txt'] });
  await until(`document.querySelectorAll('#dictlist li').length === 3`);
  ok((await dictRows())[2] === '새 사전 on 1개 selected', 'importing it adds an applied, selected dictionary');
  promptText = '가져온 사전';
  await evaluate(`document.querySelectorAll('#dictlist li')[2].querySelector('button[title="이름 변경"]').click()`);
  ok((await dictRows())[2] === '가져온 사전 on 1개 selected', 'renamed');
  const again = await download('button[title="내보내기"]', '가져온 사전');
  ok(again['userdict-가져온 사전.txt'] === exported['userdict-새 사전.txt'], 'and exports the same bytes');

  const ehnd = 'クロムウェル\t크롬웰경\t1\tVHn\n// 주석\n門前\t문 앞\t0\n';
  await upload({ 'UserDict_test.txt': ehnd });
  await until(`document.querySelectorAll('#dictlist li').length === 4`);
  ok((await dictRows())[3] === 'test on 2개 Ehnd selected', 'an Ehnd UserDict file imports as an Ehnd dictionary');
  ok(await evaluate(`document.getElementById('namespane').hidden && document.querySelector('#userdict th:nth-child(4)').textContent === '의미 코드'`),
    'with Ehnd columns and no names table');
  out = await probe('クロムウェルが来た。');
  ok(out.includes('크롬웰경'), `its entries apply: ${out}`);
  const back = await download('button[title="내보내기"]', 'test');
  ok(back['UserDict_test.txt'] === ehnd, `it exports as UserDict_test.txt with the same bytes`);

  const all = (await download('#dict-export'))['hdtrans-사전.zip'] ?? {};
  ok(all['userdict.txt'] === '猫\t냥이\n猫\t고양이님\n猫\t냥이\nユフィリア\t유피\tname\n' && all['UserDict.txt'] === ehnd,
    `내보내기 writes the applied dictionaries in order, in a ZIP with the Ehnd file: ${JSON.stringify(all)}`);
  await clickRow('test', 'input');
  const one = await download('#dict-export');
  ok(Object.keys(one).join() === 'userdict.txt' && one['userdict.txt'] === all['userdict.txt'], 'and only userdict.txt without an applied Ehnd dictionary');
  await clickRow('test', 'input');

  await upload({ 'UserDict_bad.txt': 'ポチ\t바둑이\t1\n犬\t' + '가'.repeat(16) + '\t1\n' });
  await until(`document.querySelectorAll('#dictlist li').length === 5`);
  out = await probe('ポチ');
  ok(out.includes('바둑이') && (await settled()).warning.includes('bad 2행'), `Ehnd good rows apply with a dictionary-and-row warning: ${out}`);
  await click('#tab-docs');
  await click('#run');
  await until(`document.querySelector('#filelist tbody tr td:nth-child(3)').textContent.startsWith('완료')`);
  ok((await evaluate(`document.getElementById('warning').textContent`)).includes('bad 2행'), 'documents also show Ehnd dictionary warnings while completing');
  await click('#tab-dictionaries');
  await clickRow('bad', 'button[title="삭제"]');
  await clickRow('test', '.name');
  await settled();

  await clickRow('가져온 사전', 'button[title="삭제"]');
  await until(`document.querySelectorAll('#dictlist li').length === 3`);
  const before = (await dictRows()).join();
  await send('Page.reload');
  await dictsReady();
  ok((await evaluate(`document.getElementById('dictionaries').hidden`)) === false && (await dictRows()).join() === before,
    `deleted, and the list and selection survive a reload: ${before}`);
  await shot('-dictionaries');
}

// mobileCheck repeats the layout checks and an edit at a phone's viewport.
async function mobileCheck() {
  const ok = say('mobile');
  await send('Emulation.setDeviceMetricsOverride', phone);
  await send('Emulation.setTouchEmulationEnabled', { enabled: true });
  await clickRow('기본 사용자 사전', '.name');
  const m = await evaluate(`(() => { const l = document.getElementById('dictlist').getBoundingClientRect();
    const e = document.getElementById('dictedit').getBoundingClientRect();
    const s = document.querySelector('#userdict .gridscroll');
    const small = [...document.querySelectorAll('#dictionaries button, .tabs button')].filter((b) => b.offsetParent && b.getBoundingClientRect().height < 44)
      .map((b) => b.textContent);
    const box = document.querySelector('#dictlist input').getBoundingClientRect();
    return { stacked: e.top >= l.bottom, page: document.documentElement.scrollWidth <= innerWidth,
      scroll: s.scrollWidth > s.clientWidth && getComputedStyle(s).overflowX === 'auto', small, box: box.width >= 24 && box.height >= 24 }; })()`);
  ok(m.stacked, 'the grids sit below the list');
  ok(m.page, 'the page does not scroll sideways');
  ok(m.scroll, 'the grid scrolls sideways in its box');
  ok(m.small.length === 0 && m.box, `buttons are at least 44 px high, checkboxes 24 px${m.small.length ? ': ' + m.small.join(' ') : ''}`);
  await click('#dict-new');
  await until(`document.querySelectorAll('#dictlist li').length === 4`);
  await focusCell('userdict', 0, 0);
  await type('魔物');
  await press('Tab');
  await type('몬스터군');
  let out = await probe('魔物が来た。');
  ok(out.includes('몬스터군'), `a new dictionary's entry applies: ${out}`);
  await clickRow('새 사전 2', 'input');
  out = (await settled()).out;
  ok(!out.includes('몬스터군'), `and stops when unchecked: ${out}`);
  await shot('-mobile', phone);
  await send('Emulation.setTouchEmulationEnabled', { enabled: false });
  await send('Emulation.clearDeviceMetricsOverride');
  await click('#tab-text');
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
  await outputCheck();
  const version = await evaluate(`document.getElementById('hver').textContent`);
  const versionOk = /^v\S+ \([0-9a-f]{7,}, \d{4}-\d\d-\d\d\)$/.test(version);
  failed ||= !versionOk;
  console.log(`version: ${versionOk ? 'ok    ' : 'FAILED'} ${version}`);
  await gridCheck();
  await shot('');
  await migrationCheck();
  await libraryCheck();
  await mobileCheck();
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
