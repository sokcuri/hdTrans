// The page: loads the engine and the dictionaries (from Cache Storage when
// their content hashes are cached), hands them to the worker and translates
// there as the input changes.
import { decodeText, encodeOutput, formatDocument, outputName, zip, type OutputMode } from './documents.js';
import { grid } from './grid.js';
import { namesDict } from './names.js';

interface Asset {
  name: string;
  path: string; // assets/<hash>/<name>, relative to the page
  size: number;
  sha256: string;
}

interface Manifest {
  format: number;
  engine: Asset;
  wasm: Asset;
  dict: { snapshot: Asset };
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const src = $<HTMLTextAreaElement>('src');
const out = $<HTMLTextAreaElement>('out');
const example = $<HTMLSelectElement>('example');
const speech = $<HTMLSelectElement>('speech');
const status = $<HTMLElement>('status');
const bar = $<HTMLProgressElement>('bar');
const info = $<HTMLElement>('info');
const tbar = $<HTMLProgressElement>('tbar');

const started = performance.now();
const cacheName = 'hdtrans-assets';
const mb = (n: number) => (n / 1e6).toFixed(1);
let ready = false;

src.value = localStorage.getItem('hdtrans.src') ?? '';
src.addEventListener('input', () => {
  localStorage.setItem('hdtrans.src', src.value);
  schedule();
});
const editor = (key: string, placeholders: string[]) => {
  const g = grid($(key), ['일본어', '한국어', '품사', '옵션'], placeholders, key + '.tsv', (text) => {
    localStorage.setItem('hdtrans.' + key, text);
    schedule();
    staleDocs();
  });
  g.text = localStorage.getItem('hdtrans.' + key) ?? '';
  return g;
};
const userdict = editor('userdict', ['手伝う', '돕다', 'v:ㅂ', '']);
const names = editor('names', ['春香', '하루카', 'name', '']);
const savedMode = localStorage.getItem('hdtrans.mode');
if (savedMode) (document.querySelector(`input[name=mode][value=${savedMode}]`) as HTMLInputElement).checked = true;
document.querySelectorAll('input[name=mode]').forEach((el) =>
  el.addEventListener('change', () => {
    localStorage.setItem('hdtrans.mode', mode());
    schedule();
  }),
);
const mode = () => (document.querySelector('input[name=mode]:checked') as HTMLInputElement).value;
const savedSpeech = localStorage.getItem('hdtrans.speech');
if (savedSpeech === 'soft' || savedSpeech === 'strict') speech.value = savedSpeech;
speech.addEventListener('change', () => {
  localStorage.setItem('hdtrans.speech', speech.value);
  schedule();
  staleDocs();
});

/** Returns the text of the example file. */
async function exampleText(file: string): Promise<string> {
  const res = await fetch('examples/' + file);
  if (!res.ok) throw new Error(`${file}: HTTP ${res.status}`);
  return (await res.text()).replace(/\r/g, '').replace(/\n+$/, '');
}

fetch('examples/list.json')
  .then((res) => res.json())
  .then(async (list: [string, string][]) => {
    list.forEach(([label, file]) => example.add(new Option(label, file)));
    if (src.value === '' && list.length > 0) {
      const text = await exampleText(list[0][1]);
      if (src.value !== '') return;
      src.value = text;
      schedule();
    }
  })
  .catch(() => {});
example.addEventListener('change', async () => {
  if (example.value === '') return;
  const file = example.value;
  example.value = '';
  try {
    src.value = await exampleText(file);
    src.dispatchEvent(new Event('input'));
  } catch (err) {
    info.textContent = '예제를 불러오지 못했습니다: ' + String(err);
  }
});

const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
let seq = 0;
type Reply = { lines?: string[]; message?: string; warning?: string; ms: number; memory: number };
const pending = new Map<number, (m: Reply) => void>();

function request(mode: string, lines: string[], userdict: string): Promise<Reply> {
  const id = ++seq;
  return new Promise<Reply>((resolve) => {
    pending.set(id, resolve);
    worker.postMessage({ type: 'translate', id, mode, speech: speech.value, lines, userdict });
  });
}
worker.onmessage = (ev) => {
  const m = ev.data;
  if (m.type === 'ready') {
    ready = true;
    document.body.dataset.engine = 'ready';
    document.body.dataset.memory = String(m.memory);
    const total = performance.now() - started;
    status.textContent = `준비 완료 · ${(total / 1000).toFixed(1)}초 (엔진 사전 적재 ${(m.ms / 1000).toFixed(1)}초, 메모리 ${Math.round(m.memory / 1e6)} MB)`;
    bar.hidden = true;
    schedule();
    showDocs();
  } else if (m.type === 'fatal') {
    document.body.dataset.engine = 'error';
    status.textContent = '엔진을 불러오지 못했습니다: ' + m.message;
  } else {
    pending.get(m.id)?.(m);
    pending.delete(m.id);
  }
};

/**
 * The user dictionary of the engine: the user dictionary grid, then the
 * names table as name entries. userLines is the number of lines of the first.
 */
function dictText(): { text: string; userLines: number } {
  const u = userdict.text.trim() === '' ? '' : userdict.text;
  const n = names.text.trim() === '' ? '' : namesDict(names.text);
  return { text: [u, n].filter((t) => t !== '').join('\n'), userLines: u === '' ? 0 : u.split('\n').length };
}

/** The engine's load errors ("LINE: message") with the table and row of each line. */
function warningText(warning: string, userLines: number): string {
  return warning
    .split('\n')
    .map((w) =>
      w.replace(/^(\d+): /, (_, n) =>
        Number(n) <= userLines ? `사용자 사전 ${n}행: ` : `이름 표 ${Number(n) - userLines}행: `,
      ),
    )
    .join(' / ');
}

// The input is translated a moment after it last changed. Every change
// (text, mode, speech, dictionaries) starts a new generation; a result is shown
// only if no change came while it was made, otherwise the translation runs
// again at once.
let timer: ReturnType<typeof setTimeout> | undefined;
let generation = 0;
let shown = -1; // the generation the output shows
let running = false;

function schedule() {
  generation++;
  out.dataset.done = '0';
  clearTimeout(timer);
  timer = setTimeout(translate, 150);
}

async function translate() {
  if (!ready || running || shown === generation) return;
  running = true;
  const g = generation;
  const lines = src.value.replace(/\r/g, '').split('\n');
  const d = dictText();
  const t0 = performance.now();
  const slow = setTimeout(() => (tbar.hidden = false), 300);
  let text: string;
  try {
    const m = await request(mode(), lines, d.text);
    if (m.lines === undefined) throw new Error(m.message);
    document.body.dataset.memory = String(m.memory);
    text = `${lines.length}줄 · ${Math.round(performance.now() - t0)} ms`;
    if (m.warning) text += ' / 사전 오류: ' + warningText(m.warning, d.userLines);
    if (g === generation) out.value = m.lines.join('\n');
  } catch (err) {
    text = '번역 실패: ' + (err instanceof Error ? err.message : String(err));
  }
  clearTimeout(slow);
  running = false;
  tbar.hidden = true;
  if (g !== generation) {
    translate();
    return;
  }
  shown = g;
  info.textContent = text;
  out.dataset.done = '1';
}

// The document tab: files are read and translated here, one after another,
// each as one document. Results are kept as text; the output mode applies
// when a file is saved.
const tabs: [HTMLElement, HTMLElement][] = [
  [$('tab-text'), $('text')],
  [$('tab-docs'), $('docs')],
];
function showTab(index: number) {
  tabs.forEach(([button, panel], i) => {
    button.setAttribute('aria-selected', String(i === index));
    panel.hidden = i !== index;
  });
  localStorage.setItem('hdtrans.tab', index === 1 ? 'docs' : 'text');
}
tabs.forEach(([button], i) => button.addEventListener('click', () => showTab(i)));
if (localStorage.getItem('hdtrans.tab') === 'docs') showTab(1);

interface Doc {
  out: string; // output file name
  source?: string; // undefined if the file could not be read
  translated?: string;
  row: HTMLTableRowElement;
  save: HTMLButtonElement;
}
const drop = $<HTMLLabelElement>('drop');
const files = $<HTMLInputElement>('files');
const fileList = $<HTMLTableElement>('filelist');
const run = $<HTMLButtonElement>('run');
const zipButton = $<HTMLButtonElement>('zip');
let docs: Doc[] = [];
let docsRunning = false;
let docsCancel = false;
let docsAt = 0;

const outputMode = () => (document.querySelector('input[name=output]:checked') as HTMLInputElement).value as OutputMode;
const outputBytes = (d: Doc) => encodeOutput(formatDocument(d.source!, d.translated!, outputMode()));
const setState = (d: Doc, text: string) => (d.row.cells[2].textContent = text);

function showDocs() {
  const todo = docs.filter((d) => d.source !== undefined).length;
  run.disabled = !ready || docsCancel || (!docsRunning && todo === 0);
  run.textContent = docsCancel ? '중단 중…' : docsRunning ? `중단 (${docsAt}/${todo})` : '모두 번역';
  zipButton.disabled = docsRunning || !docs.some((d) => d.translated !== undefined);
  for (const d of docs) d.save.disabled = d.translated === undefined;
  fileList.hidden = docs.length === 0;
}

/** Drops the results made with other settings. */
function staleDocs() {
  if (docsRunning) return;
  for (const d of docs) {
    if (d.translated === undefined) continue;
    d.translated = undefined;
    setState(d, '대기');
  }
  showDocs();
}

function save(name: string, bytes: Uint8Array, type: string) {
  const url = URL.createObjectURL(new Blob([bytes as BlobPart], { type }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60_000);
}

async function addFiles(list: FileList | null) {
  if (docsRunning || !list) return;
  const body = fileList.tBodies[0];
  body.replaceChildren();
  docs = [];
  const used = new Set<string>();
  for (const f of Array.from(list)) {
    let out = outputName(f.name);
    for (let k = 2; used.has(out); k++) out = outputName(f.name).replace(/\.txt$/, ` (${k}).txt`);
    used.add(out);
    const decoded = decodeText(new Uint8Array(await f.arrayBuffer()));
    const row = body.insertRow();
    for (const text of [f.name, decoded?.encoding ?? '-', decoded ? '대기' : '읽지 못함 (Shift-JIS, UTF-8, UTF-16LE만)']) {
      row.insertCell().textContent = text;
    }
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = '받기';
    button.title = out;
    row.insertCell().append(button);
    const d: Doc = { out, source: decoded?.text, row, save: button };
    button.addEventListener('click', () => save(d.out, outputBytes(d), 'text/plain;charset=utf-8'));
    docs.push(d);
  }
  showDocs();
}

async function runDocs() {
  docsRunning = true;
  docsCancel = false;
  docsAt = 0;
  const todo = docs.filter((d) => d.source !== undefined);
  for (const d of todo) {
    d.translated = undefined;
    setState(d, '대기');
  }
  for (const d of todo) {
    if (docsCancel) break;
    docsAt++;
    setState(d, '번역 중');
    showDocs();
    const t0 = performance.now();
    try {
      const m = await request('document', d.source!.split('\n'), dictText().text);
      if (m.lines === undefined) throw new Error(m.message);
      if (docsCancel) break;
      d.translated = m.lines.join('\n');
      setState(d, `완료 · ${Math.round(performance.now() - t0)} ms`);
    } catch (err) {
      setState(d, '번역 실패: ' + (err instanceof Error ? err.message : String(err)));
    }
  }
  for (const d of todo) {
    const state = d.row.cells[2].textContent;
    if (docsCancel && d.translated === undefined && (state === '대기' || state === '번역 중')) setState(d, '중단');
  }
  docsRunning = false;
  docsCancel = false;
  showDocs();
}

files.addEventListener('change', () => {
  addFiles(files.files).finally(() => (files.value = ''));
});
drop.addEventListener('dragover', (ev) => {
  ev.preventDefault();
  drop.classList.add('over');
});
drop.addEventListener('dragleave', () => drop.classList.remove('over'));
drop.addEventListener('drop', (ev) => {
  ev.preventDefault();
  drop.classList.remove('over');
  addFiles(ev.dataTransfer?.files ?? null);
});
run.addEventListener('click', () => {
  if (!docsRunning) runDocs();
  else {
    docsCancel = true;
    showDocs();
  }
});
zipButton.addEventListener('click', async () => {
  const done = docs.filter((d) => d.translated !== undefined);
  save('hdtrans-번역.zip', await zip(done.map((d) => ({ name: d.out, data: outputBytes(d) }))), 'application/zip');
});
showDocs();

/** Hex SHA-256 of b. */
async function sha256(b: Uint8Array): Promise<string> {
  const d = new Uint8Array(await crypto.subtle.digest('SHA-256', b as BufferSource));
  return Array.from(d, (x) => x.toString(16).padStart(2, '0')).join('');
}

/**
 * Returns the bytes of a, from the cache or downloaded (and then checked
 * against its hash and cached). progress gets the bytes read so far.
 */
async function asset(cache: Cache | undefined, a: Asset, progress: (n: number) => void): Promise<Uint8Array> {
  const url = new URL(a.path, location.href);
  const hit = await cache?.match(url);
  if (hit) {
    const b = new Uint8Array(await hit.arrayBuffer());
    progress(b.length);
    return b;
  }
  const res = await fetch(url);
  if (!res.ok || !res.body) throw new Error(`${a.name}: HTTP ${res.status}`);
  const b = new Uint8Array(a.size);
  let n = 0;
  const reader = res.body.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (n + value.length > b.length) throw new Error(`${a.name}: larger than ${a.size} bytes`);
    b.set(value, n);
    n += value.length;
    progress(n);
  }
  if (n !== a.size || (await sha256(b)) !== a.sha256) throw new Error(`${a.name}: content does not match the manifest`);
  await cache
    ?.put(url, new Response(b as BufferSource, { headers: { 'content-type': res.headers.get('content-type') ?? '' } }))
    .catch(() => {});
  return b;
}

async function load() {
  const res = await fetch('manifest.json', { cache: 'no-cache' });
  const m: Manifest = await res.json();
  if (m.format !== 4) throw new Error('지원하지 않는 manifest 형식입니다. 페이지를 새로고침해 주세요.');
  const all = [m.wasm, m.dict.snapshot];
  const total = all.reduce((s, a) => s + a.size, 0);
  const done = all.map(() => 0);
  const show = () => {
    const n = done.reduce((s, x) => s + x, 0);
    bar.max = total;
    bar.value = n;
    status.textContent = `엔진과 사전 불러오는 중… ${mb(n)} / ${mb(total)} MB`;
  };
  show();
  const cache = await globalThis.caches?.open(cacheName).catch(() => undefined);
  const bytes = await Promise.all(all.map((a, i) => asset(cache, a, (n) => ((done[i] = n), show()))));
  if (cache) {
    const keep = new Set(all.map((a) => new URL(a.path, location.href).href));
    for (const req of await cache.keys()) if (!keep.has(req.url)) await cache.delete(req);
  }
  status.textContent = '엔진 준비 중…';
  const module = await WebAssembly.compile(bytes[0] as BufferSource);
  const snapshot = bytes[1].buffer;
  const engine = new URL(m.engine.path, location.href).href;
  worker.postMessage({ type: 'init', engine, module, snapshot }, [snapshot]);
}

load().catch((err) => {
  document.body.dataset.engine = 'error';
  status.textContent = '불러오기 실패: ' + (err instanceof Error ? err.message : String(err));
});
