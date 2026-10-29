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
// deleting, and that the list survives a reload. Last it checks the text tab's
// layout at a desktop, a phone and a 360 px viewport and the × button.
// Served over plain http from a LAN address (not a secure context), the same
// checks run, with clipboard paste and copy seen through events. With SHOT set it saves
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

// laidOut waits until the page has sized the text cards for the window (after a resize).
const laidOut = () =>
  until(`(() => { const t = document.getElementById('text'); if (t.hidden) return true;
    const c = [...document.querySelectorAll('#text .card')].filter((e) => e.offsetParent).pop();
    return Math.abs(c.getBoundingClientRect().bottom + scrollY - (document.querySelector('html.locked') ? scrollY : 0) - (innerHeight - 12)) < 2
; })()`, 3000).catch(() => {});

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
  Escape: { code: 'Escape', windowsVirtualKeyCode: 27 },
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
  await click('#tab-dictionaries');
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
  if (secure) {
    await evaluate(`navigator.clipboard.writeText(${JSON.stringify(tsv)})`);
    await press('v', process.platform === 'darwin' ? 4 : 2, ['paste']);
  } else {
    await evaluate(`(() => { const dt = new DataTransfer(); dt.setData('text/plain', ${JSON.stringify(tsv)});
      document.activeElement.dispatchEvent(new ClipboardEvent('paste', { clipboardData: dt, bubbles: true, cancelable: true })); })()`);
  }
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

  await click('#tab-text');
  ok(await evaluate(`!document.getElementById('text').contains(document.getElementById('userdict')) && document.getElementById('applied').textContent === '기본 사용자 사전'`),
    'the text tab has no dictionary grids and names the applied dictionary in the settings');
  await click('#dict-edit');
  ok(await evaluate(`!document.getElementById('dictionaries').hidden && document.querySelector('#dictlist li.sel .name').textContent === '기본 사용자 사전'`),
    '사전 편집 opens the 사전 tab on it');
  await click('#tab-text');

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
  const infoOk = /^\d+자 · \d+줄 · \d+ ms$/.test(r.info) &&
    (await evaluate(`document.getElementById('info').closest('.pane')?.contains(document.getElementById('out')) ?? false`));
  failed ||= !infoOk;
  console.log(`info: ${infoOk ? 'ok    ' : 'FAILED'} character and line count and time in the 한국어 card header (${r.info})`);
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
  // Without a secure context there is no navigator.clipboard; the page copies
  // through a hidden textarea, seen here by its copy event.
  await evaluate(`document.addEventListener('copy', (e) => {
    const t = e.target;
    window.copied = t instanceof HTMLTextAreaElement ? t.value.slice(t.selectionStart, t.selectionEnd) : String(getSelection());
  }, true)`);
  for (const mode of ['interleaved', 'spaced', 'translation']) {
    await pick(mode);
    const shown = await evaluate(`document.getElementById('out').textContent`);
    const muted = await evaluate(`[...document.querySelectorAll('#out .src')].map((e) => e.textContent).join('|')`);
    await until(`document.getElementById('copy').textContent === '복사'`);
    await send('Runtime.evaluate', { expression: `document.getElementById('copy').click()`, userGesture: true });
    const label = await until(`document.getElementById('copy').textContent !== '복사' && document.getElementById('copy').textContent`);
    const copied = await evaluate(secure ? `navigator.clipboard.readText()` : `window.copied`);
    const info = await evaluate(`document.getElementById('info').textContent`);
    ok(info.startsWith(`${[...shown].length}자 · ${shown.split('\n').length}줄 · `), `${mode}: the header counts the copied text: ${info}`);
    ok(shown === want[mode] && label === '복사됨' && copied === shown && muted === (mode === 'translation' ? '' : '猫が来た。\n|犬が来た。\n'),
      `${mode}: shown and copied ${JSON.stringify(shown)}`);
  }
  // The settings row's summary, the select and the shown output agree after a
  // change, on the document tab, and after a reload.
  const agree = async (mode, where) => {
    const r = await evaluate(`(() => {
      const s = document.getElementById('outmode');
      return { value: s.value, label: s.selectedOptions[0].text, sum: document.getElementById('settings-sum').textContent,
        muted: document.querySelectorAll('#out .src').length, shown: document.getElementById('out').textContent };
    })()`);
    const good = r.value === mode && r.sum.split(', ').includes(r.label) && r.shown === want[mode] && (r.muted > 0) === (mode !== 'translation');
    ok(good, `${where}: summary, select and output agree on ${mode}${good ? '' : ' ' + JSON.stringify(r)}`);
  };
  await pick('spaced');
  await agree('spaced', 'after a change');
  ok((await evaluate(`localStorage.getItem('hdtrans.output')`)) === 'spaced', 'the choice is saved');
  await click('#tab-docs');
  await agree('spaced', 'on the document tab');
  await pick('interleaved');
  await click('#tab-text');
  await agree('interleaved', 'back on the text tab after a change on the document tab');
  await send('Page.reload');
  await dictsReady();
  await settled();
  await agree('interleaved', 'after a reload');
  await pick('translation');
  await agree('translation', 'after changing back');
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

// layoutCheck checks the text tab at a desktop and a phone viewport: cards
// side by side or stacked with both in the first screen, the 설정 button with
// its layer (a popover under it, a bottom sheet on a phone) that closes on
// Esc, a backdrop tap and its × and keeps the focus inside while open, no
// sideways page scroll down to 360 px, 40 px touch targets, and the clear
// button and input count.
async function layoutCheck() {
  const ok = say('layout');
  const measure = () => evaluate(`(() => {
    const [a, b] = [...document.querySelectorAll('#text .card')].map((c) => c.getBoundingClientRect());
    const small = [...document.querySelectorAll('main button, main select, .seg label')]
      .filter((e) => e.offsetParent && !e.closest('table.grid') && e.getBoundingClientRect().height < 40).map((e) => e.id || e.textContent);
    const ex = document.getElementById('example').getBoundingClientRect();
    return { side: Math.abs(a.top - b.top) < 1 && b.left > a.right, stacked: b.top >= a.bottom, inView: b.top + 80 < innerHeight,
      example: ex.height >= 40 && ex.bottom <= a.top + 60 && ex.right <= innerWidth && !document.getElementById('example').closest('#settings'),
      sheet: document.getElementById('settings-sheet').open, page: document.documentElement.scrollWidth <= innerWidth, small };
  })()`);
  const layer = () => evaluate(`(() => {
    const d = document.getElementById('settings-sheet'), r = d.getBoundingClientRect(), t = document.getElementById('settings-toggle').getBoundingClientRect();
    return { open: d.open, modal: d.matches(':modal'), focus: d.contains(document.activeElement), bottom: Math.abs(r.bottom - innerHeight) < 1 && Math.abs(r.width - innerWidth) < 1,
      anchored: Math.abs(r.top - t.bottom - 6) < 2 && Math.abs(r.left - t.left) < 2, r: [r.top, r.bottom, r.width, innerHeight, innerWidth], expanded: document.getElementById('settings-toggle').getAttribute('aria-expanded') };
  })()`);
  const settingsLayer = async (name) => {
    await click('#settings-toggle');
    let l = await layer();
    ok(l.open && l.modal && l.focus && l.expanded === 'true' && (name === 'desktop' ? l.anchored : l.bottom),
      `${name}: 설정 opens ${name === 'desktop' ? 'a popover under the button' : 'a bottom sheet'} with the focus inside${l.open && l.focus && (name === 'desktop' ? l.anchored : l.bottom) ? '' : ' ' + JSON.stringify(l)}`);
    let inside = true;
    for (let i = 0; i < 12; i++) {
      await press('Tab');
      inside &&= await evaluate(`document.getElementById('settings-sheet').contains(document.activeElement)`);
    }
    ok(inside, `${name}: Tab stays inside the open layer`);
    await evaluate(`(() => { const s = document.getElementById('outmode'); s.value = 'spaced'; s.dispatchEvent(new Event('change')); })()`);
    ok((await evaluate(`document.getElementById('settings-sheet').open && document.getElementById('settings-sum').textContent.includes('원문 병기 + 빈 줄') && document.querySelectorAll('#out .src').length > 0`)),
      `${name}: a choice applies at once, with the layer still open`);
    await evaluate(`(() => { const s = document.getElementById('outmode'); s.value = 'translation'; s.dispatchEvent(new Event('change')); })()`);
    const focused = await evaluate(`document.activeElement.id || document.activeElement.tagName`);
    await press('Escape');
    await until(`document.getElementById('settings-toggle').getAttribute('aria-expanded') === 'false'`, 2000).catch(() => {});
    l = await layer();
    ok(!l.open && l.expanded === 'false', `${name}: Esc closes it${l.open ? ' (focus on ' + focused + ')' : ''}`);
    await click('#settings-toggle');
    for (const type of ['mousePressed', 'mouseReleased']) await send('Input.dispatchMouseEvent', { type, x: 5, y: 5, button: 'left', clickCount: 1 });
    ok(!(await layer()).open, `${name}: a tap on the backdrop closes it`);
    await click('#settings-toggle');
    await click('#settings-close');
    ok(!(await layer()).open, `${name}: × closes it`);
  };
  // How far the header text of each card starts from its first text line.
  const aligned = async (name) => {
    const d = await evaluate(`(() => {
      const src = document.getElementById('src'), out = document.getElementById('out');
      const srcText = src.getBoundingClientRect().left + src.clientLeft + parseFloat(getComputedStyle(src).paddingLeft);
      const node = document.createTreeWalker(out, NodeFilter.SHOW_TEXT).nextNode();
      const range = document.createRange();
      range.setStart(node, 0);
      range.setEnd(node, 1);
      const left = (id) => document.getElementById(id).getBoundingClientRect().left;
      const badges = [...document.querySelectorAll('#text .card .badge')];
      return [badges[0].getBoundingClientRect().left - srcText, badges[1].getBoundingClientRect().left - range.getBoundingClientRect().left,
        badges.map((e) => e.textContent).join() === '원문,번역문' && badges.every((e) => getComputedStyle(e).borderTopStyle === 'solid') ? 0 : 99];
    })()`);
    ok(d.every((x) => Math.abs(x) <= 1), `${name}: the boxed labels 원문 and 번역문 start the headers at the text's left edge (${d.slice(0, 2).map((x) => x.toFixed(1)).join(', ')} px)`);
  };
  await click('#tab-text');
  await translate('猫が来た。\n犬が来た。');
  await send('Emulation.setFocusEmulationEnabled', { enabled: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1366, height: 800, deviceScaleFactor: 1, mobile: false });
  await laidOut();
  await aligned('desktop');
  let m = await measure();
  ok(m.side && !m.sheet && m.page, `desktop: cards side by side, settings closed${m.side && !m.sheet && m.page ? '' : ' ' + JSON.stringify(m)}`);
  await settingsLayer('desktop');
  await send('Emulation.setDeviceMetricsOverride', phone);
  await send('Emulation.setTouchEmulationEnabled', { enabled: true });
  await laidOut();
  await aligned('phone');
  m = await measure();
  const phoneOk = m.stacked && m.inView && !m.sheet && m.page && m.small.length === 0 && m.example;
  ok(phoneOk, `phone: stacked cards in the first screen, settings closed, the 예제 row above the cards${phoneOk ? '' : ' ' + JSON.stringify(m)}`);
  await click('#settings-toggle');
  m = await measure();
  ok(m.small.length === 0, `phone: the settings sheet has 40 px targets${m.small.length ? ': ' + m.small.join(' ') : ''}`);
  await click('#settings-close');
  await settingsLayer('phone');
  await send('Emulation.setDeviceMetricsOverride', { ...phone, width: 360, height: 780 });
  await laidOut();
  m = await measure();
  ok(m.inView && m.example, `360 px: 예제 and both cards in the first screen${m.inView && m.example ? '' : ' ' + JSON.stringify(m)}`);
  for (const tab of ['text', 'docs', 'dictionaries']) {
    await click(`#tab-${tab}`);
    m = await measure();
    ok(m.page && m.small.length === 0, `360 px ${tab} tab: no sideways scroll, 40 px targets${m.small.length ? ': ' + m.small.join(' ') : ''}`);
  }
  await click('#tab-text');
  await send('Emulation.setTouchEmulationEnabled', { enabled: false });
  await send('Emulation.clearDeviceMetricsOverride');
  const before = await evaluate(`document.getElementById('src').value`);
  const counted = await evaluate(`document.getElementById('count').textContent`);
  await click('#clear');
  ok((await evaluate(`document.getElementById('src').value === '' && document.getElementById('count').textContent === '0자 · 0줄' && document.getElementById('clear').hidden`)) &&
    counted === `${[...before].length}자 · ${before.split('\n').length}줄` && (await settled()).out === '', `× clears the input (count was ${counted})`);
}

// exampleCheck picks an example and checks that the input becomes its text
// and gets translated.
async function exampleCheck(filledBeforeReady) {
  const ok = say('examples');
  const options = await evaluate(`[...document.getElementById('example').options].slice(1).map((o) => o.value)`);
  ok(options.length > 0, `the list has ${options.length} examples${filledBeforeReady ? ', filled before the engine was ready' : ''}`);
  const file = options[options.length - 1];
  await evaluate(`(() => { const s = document.getElementById('example'); s.value = ${JSON.stringify(file)}; s.dispatchEvent(new Event('change')); })()`);
  const want = await evaluate(`fetch('examples/${file}').then((r) => r.text()).then((t) => t.replace(/\\r/g, '').replace(/\\n+$/, ''))`);
  await until(`document.getElementById('src').value === ${JSON.stringify(want)}`, 10000).catch(() => {});
  const r = await settled();
  ok((await evaluate(`document.getElementById('src').value`)) === want && r.out !== '', `choosing ${file} fills the input and translates it (${r.info})`);
}

// expandCheck fills the input with a few hundred lines and checks, at a
// desktop, a phone and a 360 px viewport: the two cards fill the window under
// the rows above them with no page scroll, each scrolling inside; 펼치기 shows
// one card alone, filling the window exactly under the settings, 예제 and
// language rows, with the page held still and the other card hidden but still
// translated; ↕ and Esc bring both back. On a phone the expanded source still
// fits when the window shrinks as for an on-screen keyboard. The expanded card
// survives a reload.
async function expandCheck() {
  const ok = say('expand');
  const lines = readFileSync(inputFile, 'utf8').split('\n').filter(Boolean);
  const long = Array.from({ length: 300 }, (_, k) => lines[k % lines.length]).join('\n');
  await click('#tab-text');
  await translate(long);
  await evaluate(`document.getElementById('outmode').value = 'interleaved'; document.getElementById('outmode').dispatchEvent(new Event('change'))`);
  const view = () => evaluate(`(() => {
    const box = (id) => { const b = document.getElementById(id).getBoundingClientRect(); return { top: b.top, bottom: b.bottom }; };
    const shown = (id) => document.getElementById(id).offsetParent !== null;
    const src = document.getElementById('src'), out = document.getElementById('out');
    return { y: scrollY, h: innerHeight, src: shown('srccard') ? box('srccard') : null, out: shown('outcard') ? box('outcard') : null,
      langbar: box('langbar').bottom, settings: box('settings').top, lang: document.getElementById('langbar').innerText.replace(/\\s+/g, ' ').trim(),
      srcScrolls: src.scrollHeight > src.clientHeight, outScrolls: out.scrollHeight > out.clientHeight, outText: out.textContent.length };
  })()`);
  // Whether a wheel turn over the rows above the cards scrolls the page.
  const wheelMoves = async () => {
    const [x, y, before] = await evaluate(`(() => { const b = document.getElementById('langbar').getBoundingClientRect(); return [b.left + 4, b.top + b.height / 2, scrollY]; })()`);
    await send('Input.dispatchMouseEvent', { type: 'mouseWheel', x, y, deltaX: 0, deltaY: 400 });
    await sleep(300);
    const after = await evaluate('scrollY');
    await evaluate(`scrollTo(0, ${before})`);
    return after !== before;
  };
  const fits = (v) => [v.src, v.out].every((b) => !b || (b.top >= 0 && b.bottom <= v.h + 0.5));
  for (const [name, metrics] of [['desktop', { width: 1366, height: 800, deviceScaleFactor: 1, mobile: false }], ['phone', phone], ['360 px', { ...phone, width: 360, height: 780 }]]) {
    await send('Emulation.setDeviceMetricsOverride', metrics);
    await evaluate('scrollTo(0, 0)');
    await laidOut();
    let v = await view();
    ok(fits(v) && v.src && v.out && v.srcScrolls && v.outScrolls && Math.abs(v.out.bottom - (v.h - 12)) < 2,
      `${name}: both cards fill the window under the rows, each scrolling inside${fits(v) && v.out && Math.abs(v.out.bottom - (v.h - 12)) < 2 ? '' : ' ' + JSON.stringify(v)}`);
    await click('#srccard .expand');
    v = await view();
    v.moved = await wheelMoves();
    const alone = (b) => b && Math.abs(v.settings) < 1 && b.top >= v.langbar && Math.abs(b.bottom - (v.h - 12)) < 2 && !v.moved;
    ok(v.lang === '일본어', `${name}: with 원문 alone the language row reads ${v.lang}`);
    ok(alone(v.src) && !v.out && v.srcScrolls, `${name}: 원문 펼치기 fills the window exactly under the rows, the page holds still and 번역 is hidden${alone(v.src) && !v.out ? '' : ' ' + JSON.stringify(v)}`);
    const before = v.outText;
    await evaluate(`(() => { const s = document.getElementById('src'); s.value = s.value + '\\n猫が来た。'; s.dispatchEvent(new Event('input')); })()`);
    const r = await settled();
    ok(r.out.length > before && r.out.endsWith('고양이가 왔다.'), `${name}: the hidden translation keeps up with the input`);
    await click('#settings-toggle');
    await press('Escape');
    await until(`!document.getElementById('settings-sheet').open`, 2000).catch(() => {});
    ok(await evaluate(`!document.getElementById('settings-sheet').open && document.getElementById('srccard').classList.contains('expanded')`),
      `${name}: Esc on the settings layer closes only the layer, the card stays expanded`);
    // A key pressed right after a close request can be swallowed by the browser.
    await sleep(500);
    if (name === 'phone') {
      await evaluate(`document.getElementById('src').focus()`);
      await send('Emulation.setDeviceMetricsOverride', { ...phone, height: 500 });
      await until(`Math.abs(document.getElementById('srccard').getBoundingClientRect().bottom - (innerHeight - 12)) < 2`, 3000).catch(() => {});
      v = await view();
      ok(alone(v.src), `phone: with the window cut to 500 px as by a keyboard, the expanded source still fits${alone(v.src) ? '' : ' ' + JSON.stringify(v)}`);
      await send('Emulation.setDeviceMetricsOverride', phone);
      await laidOut();
      const unzoomed = await evaluate(`document.getElementById('srccard').style.height`);
      await send('Emulation.setPageScaleFactor', { pageScaleFactor: 2 });
      await sleep(400);
      const zoomed = await evaluate(`document.getElementById('srccard').style.height`);
      await send('Emulation.setPageScaleFactor', { pageScaleFactor: 1 });
      ok(zoomed === unzoomed, `phone: a pinch zoom leaves the expanded card at ${unzoomed} (${zoomed} zoomed)`);
      await press('Escape');
      await evaluate(`document.getElementById('src').focus()`);
      await send('Emulation.setDeviceMetricsOverride', { ...phone, width: 844, height: 390 });
      await laidOut();
      v = await view();
      const turned = v.src && v.out && Math.abs((v.src.bottom - v.src.top) - (v.out.bottom - v.out.top)) < 1 && Math.abs(v.src.top - v.out.top) < 1;
      ok(turned, `phone: turned sideways with the input focused, the cards are laid out again side by side${turned ? '' : ' ' + JSON.stringify(v)}`);
      await send('Emulation.setDeviceMetricsOverride', phone);
      await evaluate(`document.activeElement.blur()`);
      await laidOut();
      await click('#srccard .expand');
    }
    await press('Escape');
    v = await view();
    ok(fits(v) && v.src && v.out && v.y === 0, `${name}: Esc brings both cards back${v.src && v.out ? '' : ' ' + JSON.stringify(v)}`);
    await click('#outcard .expand');
    v = await view();
    v.moved = await wheelMoves();
    ok(v.lang === '한국어', `${name}: with 번역문 alone the language row reads ${v.lang}`);
    ok(alone(v.out) && !v.src && v.outScrolls, `${name}: 번역 펼치기 fills the window and hides 원문${alone(v.out) && !v.src ? '' : ' ' + JSON.stringify(v)}`);
    if (name === 'desktop') {
      await send('Page.reload');
      await dictsReady();
      await settled();
      v = await view();
      ok(alone(v.out) && !v.src, 'the expanded card survives a reload');
    }
    await click('#outcard .expand');
    v = await view();
    ok(v.src && v.out && fits(v) && v.lang === '일본어 → 한국어', `${name}: ↕ brings both cards back${v.lang === '일본어 → 한국어' ? '' : ' ' + v.lang}`);
  }
  await evaluate(`document.getElementById('outmode').value = 'translation'; document.getElementById('outmode').dispatchEvent(new Event('change'))`);
  await send('Emulation.clearDeviceMetricsOverride');
}

// resetCheck checks that a new example and × start both cards at their first
// line: the page at the top, and no scroll left inside either box, also with
// a card expanded.
async function resetCheck() {
  const ok = say('reset');
  const lines = readFileSync(inputFile, 'utf8').split('\n').filter(Boolean);
  const long = Array.from({ length: 300 }, (_, k) => lines[k % lines.length]).join('\n');
  const file = await evaluate(`document.getElementById('example').options[1].value`);
  const pickExample = async () => {
    await evaluate(`(() => { const s = document.getElementById('example'); s.value = ${JSON.stringify(file)}; s.dispatchEvent(new Event('change')); })()`);
    await until(`document.getElementById('src').value.length > 0 && document.getElementById('src').value.length < ${long.length - 10}`, 10000);
  };
  const scrolledDeep = async () => {
    await translate(long);
    await evaluate(`scrollTo(0, document.documentElement.scrollHeight); document.getElementById('src').scrollTop = 1e6; document.getElementById('out').scrollTop = 1e6`);
  };
  const where = () => evaluate(`(() => ({ y: scrollY, settings: document.getElementById('settings').getBoundingClientRect().top,
    src: document.getElementById('src').scrollTop, out: document.getElementById('out').scrollTop }))()`);
  await click('#tab-text');
  await send('Emulation.setDeviceMetricsOverride', phone);
  for (const [what, act] of [['a new example', pickExample], ['×', () => click('#clear')]]) {
    await scrolledDeep();
    await act();
    await settled();
    let w = await where();
    ok(w.y === 0 && w.src === 0 && w.out === 0, `${what} scrolls the page to the top and both boxes to their first line${w.y || w.src || w.out ? ' ' + JSON.stringify(w) : ''}`);
    for (const card of ['srccard', 'outcard']) {
      await click(`#${card} .expand`);
      await scrolledDeep();
      await act();
      await settled();
      w = await where();
      const good = Math.abs(w.settings) < 1 && w.src === 0 && w.out === 0;
      ok(good, `${card} expanded: ${what} starts its text at the first line${good ? '' : ' ' + JSON.stringify(w)}`);
      await click(`#${card} .expand`);
    }
  }
  await send('Emulation.clearDeviceMetricsOverride');
}

let secure = true;
let examplesEarly = false;
let failed = false;
try {
  let wasReady;
  await visit(1, async (ready) => {
    wasReady = ready;
    await until(`document.getElementById('example').options.length > 1`, 30000);
    examplesEarly = (await evaluate(`document.body.dataset.engine ?? ''`)) !== 'ready';
  });
  secure = await evaluate('isSecureContext');
  console.log(`context: ${secure ? 'secure' : 'not secure (no crypto.subtle, Cache API or navigator.clipboard)'}`);
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
  await exampleCheck(examplesEarly);
  await expandCheck();
  await resetCheck();
  const version = await evaluate(`document.getElementById('hver').textContent`);
  const versionOk = /^v\S+ \([0-9a-f]{7,}, \d{4}-\d\d-\d\d\)$/.test(version);
  failed ||= !versionOk;
  console.log(`version: ${versionOk ? 'ok    ' : 'FAILED'} ${version}`);
  await gridCheck();
  await shot('');
  await migrationCheck();
  await libraryCheck();
  await mobileCheck();
  await layoutCheck();
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
