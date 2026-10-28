// The page: loads the engine and the dictionaries (from Cache Storage when
// their content hashes are cached), hands them to the worker and translates
// there as the input changes.
import { decodeText, encodeOutput, formatDocument, outputName, outputParts, zip, type OutputMode } from './documents.js';
import { count, firstName, fromFile, newId, openStore, overlay, toFile, type Dictionary, type Store } from './dictionaries.js';
import { copyText, grid } from './grid.js';
import { sha256 as sha256Bytes } from './sha256.js';

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
const out = $<HTMLElement>('out');
const example = $<HTMLSelectElement>('example');
const speech = $<HTMLSelectElement>('speech');
const outMode = $<HTMLSelectElement>('outmode');
const status = $<HTMLElement>('status');
const bar = $<HTMLProgressElement>('bar');
const info = $<HTMLElement>('info');
const warning = $<HTMLElement>('warning');
const tbar = $<HTMLProgressElement>('tbar');

const started = performance.now();
const cacheName = 'hdtrans-assets';
const mb = (n: number) => (n / 1e6).toFixed(1);
let ready = false;

const srcCount = $<HTMLElement>('count');
const clear = $<HTMLButtonElement>('clear');
/** n자 · n줄 of t: code points, and lines split at LF. */
const size = (t: string) => `${Array.from(t).length}자 · ${t === '' ? 0 : t.split('\n').length}줄`;
/** Shows the input's size and the clear button when there is input. */
function counted() {
  srcCount.textContent = size(src.value);
  clear.hidden = src.value === '';
}
src.value = localStorage.getItem('hdtrans.src') ?? '';
counted();
src.addEventListener('input', () => {
  localStorage.setItem('hdtrans.src', src.value);
  counted();
  schedule();
});
clear.addEventListener('click', () => {
  src.value = '';
  src.dispatchEvent(new Event('input'));
  src.focus();
});
const savedMode = localStorage.getItem('hdtrans.mode');
if (savedMode) (document.querySelector(`input[name=mode][value=${savedMode}]`) as HTMLInputElement).checked = true;
document.querySelectorAll('input[name=mode]').forEach((el) =>
  el.addEventListener('change', () => {
    localStorage.setItem('hdtrans.mode', mode());
    summed();
    schedule();
  }),
);
const mode = () => (document.querySelector('input[name=mode]:checked') as HTMLInputElement).value;
const savedSpeech = localStorage.getItem('hdtrans.speech');
if (savedSpeech === 'soft' || savedSpeech === 'strict') speech.value = savedSpeech;
speech.addEventListener('change', () => {
  localStorage.setItem('hdtrans.speech', speech.value);
  summed();
  schedule();
  staleDocs();
});
const outputMode = () => outMode.value as OutputMode;
const savedOutput = localStorage.getItem('hdtrans.output');
if (savedOutput === 'translation' || savedOutput === 'interleaved' || savedOutput === 'spaced') outMode.value = savedOutput;
outMode.addEventListener('change', () => {
  localStorage.setItem('hdtrans.output', outMode.value);
  summed();
  showOutput();
});

// The settings: a toolbar, or on narrow screens a row that opens it and
// sums up the choices. Each setting's own change handler updates the sum, so
// a change event that does not bubble updates it too.
const settings = $('settings');
const settingsToggle = $('settings-toggle');
const settingsSum = $('settings-sum');
settingsToggle.addEventListener('click', () => {
  const open = settingsToggle.getAttribute('aria-expanded') !== 'true';
  settingsToggle.setAttribute('aria-expanded', String(open));
  settings.classList.toggle('open', open);
});
function summed() {
  const picked = [...settings.querySelectorAll<HTMLElement>('[data-tab], .field')]
    .filter((el) => !el.hidden)
    .map((el) => {
      const select = el.querySelector('select');
      return select ? select.selectedOptions[0]?.text : el.querySelector('input:checked + span')?.textContent;
    });
  settingsSum.textContent = picked.filter(Boolean).join(', ');
}

// 펼치기 grows a card to its text so that the page scrolls instead of the
// box; each card's choice is kept. While one is expanded, the settings, 예제
// and language rows and the card headers stay on top.
const texts = document.querySelector<HTMLElement>('.texts')!;
const cards = [$('srccard'), $('outcard')];
const expandBoth = $<HTMLButtonElement>('expand-both');
const expanded = (card: HTMLElement) => card.classList.contains('expanded');
/** Sets the input's height to its text while its card is expanded. */
function fitSource() {
  src.style.height = '';
  if (expanded(cards[0]) && src.offsetParent) src.style.height = `${src.scrollHeight}px`;
}
function expand(card: HTMLElement, on: boolean) {
  card.classList.toggle('expanded', on);
  const b = card.querySelector<HTMLButtonElement>('.expand')!;
  b.setAttribute('aria-pressed', String(on));
  b.title = b.ariaLabel = `${card.dataset.name} ${on ? '접기' : '펼치기'}`;
  localStorage.setItem(`hdtrans.expand.${card.id}`, on ? '1' : '0');
  const both = cards.every(expanded);
  expandBoth.setAttribute('aria-pressed', String(both));
  expandBoth.textContent = both ? '↕ 둘 다 접기' : '↕ 둘 다 펼치기';
  if (card === cards[0]) fitSource();
  stick();
}
for (const card of cards) {
  card.querySelector('.expand')!.addEventListener('click', () => expand(card, !expanded(card)));
}
expandBoth.addEventListener('click', () => {
  const on = !cards.every(expanded);
  for (const card of cards) expand(card, on);
});
src.addEventListener('input', fitSource);
// A new width rewraps the text: on a window resize, and when a scroll bar comes or goes.
let srcWidth = 0;
new ResizeObserver(() => {
  if (src.clientWidth === srcWidth) return;
  srcWidth = src.clientWidth;
  fitSource();
}).observe(src);

// The rows that stay on top while a card is expanded, each below the one
// before. They may take at most 45% of the window with a card header; the
// language row, then the settings, scroll away first.
const examplebar = $('examplebar');
const langbar = $('langbar');
function stick() {
  const on = !$('text').hidden && cards.some(expanded);
  document.body.classList.toggle('sticky', on);
  const rows = [settings, examplebar, langbar];
  for (const row of rows) row.style.top = '';
  if (!on) return;
  const head = cards[0].querySelector<HTMLElement>('.head')!.offsetHeight;
  const keep = new Set(rows);
  const height = () => [...keep].reduce((sum, row) => sum + row.offsetHeight, head);
  if (height() > innerHeight * 0.45) keep.delete(langbar);
  if (height() > innerHeight * 0.45) keep.delete(settings);
  let top = 0;
  for (const row of rows) {
    row.classList.toggle('stuck', keep.has(row));
    if (!keep.has(row)) continue;
    row.style.top = `${top}px`;
    top += row.offsetHeight;
  }
  texts.style.setProperty('--head-top', `${top}px`);
}
const stickRows = new ResizeObserver(() => stick());
for (const el of [settings, examplebar, langbar]) stickRows.observe(el);
addEventListener('resize', stick);

// Tabs: the dictionary editor (the selected dictionary's grids) sits below
// the text tab's panes and beside the list in the dictionary tab.
const tabs: [HTMLElement, HTMLElement][] = [
  [$('tab-text'), $('text')],
  [$('tab-docs'), $('docs')],
  [$('tab-dictionaries'), $('dictionaries')],
];
const editorBox = $('editor');
function showTab(panel: string) {
  for (const [button, p] of tabs) {
    button.setAttribute('aria-selected', String(p.id === panel));
    p.hidden = p.id !== panel;
  }
  (panel === 'dictionaries' ? $('dictedit') : $('text')).append(editorBox);
  settings.hidden = panel === 'dictionaries';
  if (panel === 'text') fitSource();
  stick();
  settings.querySelectorAll<HTMLElement>('[data-tab]').forEach((el) => (el.hidden = el.dataset.tab !== panel));
  summed();
  localStorage.setItem('hdtrans.tab', panel);
}
for (const [button, p] of tabs) button.addEventListener('click', () => showTab(p.id));
showTab(tabs.some(([, p]) => p.id === localStorage.getItem('hdtrans.tab')) ? localStorage.getItem('hdtrans.tab')! : 'text');
for (const card of cards) expand(card, localStorage.getItem(`hdtrans.expand.${card.id}`) === '1');

// The dictionaries: dicts is the list in priority order, editing the one the
// grids show. Every edit is stored at once.
let store: Store | undefined;
let dictsLoaded = false;
let dicts: Dictionary[] = [];
let editing: Dictionary | undefined;
const list = $<HTMLOListElement>('dictlist');
const editSelect = $<HTMLSelectElement>('editdict');
const namesPane = $('namespane');
const heads = {
  hdtrans: [['일본어', '한국어', '품사', '옵션'], ['手伝う', '돕다', 'v:ㅂ', '']],
  ehnd: [['일본어', '한국어', '종류', '의미 코드'], ['クロムウェル', '크롬웰', '1', 'VHn']],
};

/** Translates again with the applied dictionaries, and drops document results made without them. */
function dictsChanged() {
  schedule();
  staleDocs();
}

function edited(d: Dictionary) {
  store?.put(d).catch(() => {});
  const item = list.querySelector(`li[data-id="${d.id}"] .count`);
  if (item) item.textContent = `${count(d)}개`;
  if (d.on) dictsChanged();
}
const userdict = grid($('userdict'), heads.hdtrans[0], heads.hdtrans[1], 'userdict.txt', (text) => {
  if (!editing) return;
  editing.entries = text;
  edited(editing);
});
const names = grid($('names'), ['일본어', '한국어', '품사', '옵션'], ['春香', '하루카', 'name', ''], 'names.tsv', (text) => {
  if (!editing) return;
  editing.names = text;
  edited(editing);
});

function select(d: Dictionary) {
  editing = d;
  localStorage.setItem('hdtrans.edit', d.id);
  const [columns, placeholders] = heads[d.format];
  userdict.heads(columns, placeholders, toFile(d).name);
  userdict.text = d.entries;
  names.text = d.format === 'hdtrans' ? d.names : '';
  namesPane.hidden = d.format !== 'hdtrans';
  render();
}

/** Stores the list order after a move. */
function reorder(next: Dictionary[]) {
  dicts = next;
  dicts.forEach((d, i) => {
    if (d.order === i) return;
    d.order = i;
    store?.put(d).catch(() => {});
  });
  render();
  dictsChanged();
}

function move(d: Dictionary, to: number) {
  const next = dicts.filter((x) => x !== d);
  next.splice(Math.max(0, Math.min(to, next.length)), 0, d);
  reorder(next);
}

const utf8 = (text: string) => new TextEncoder().encode(text);
const textType = 'text/plain;charset=utf-8';
let dragged: Dictionary | undefined;

function render() {
  list.replaceChildren();
  editSelect.replaceChildren();
  dicts.forEach((d, i) => {
    editSelect.add(new Option(d.name, d.id, false, d === editing));
    const li = list.appendChild(document.createElement('li'));
    li.dataset.id = d.id;
    li.draggable = true;
    li.classList.toggle('sel', d === editing);
    const on = li.appendChild(document.createElement('input'));
    on.type = 'checkbox';
    on.checked = d.on;
    on.title = '적용';
    on.addEventListener('change', () => {
      d.on = on.checked;
      store?.put(d).catch(() => {});
      dictsChanged();
    });
    const name = li.appendChild(document.createElement('button'));
    name.type = 'button';
    name.className = 'name';
    name.textContent = d.name;
    name.addEventListener('click', () => {
      select(d);
      if (matchMedia('(max-width: 48rem)').matches) editorBox.scrollIntoView({ block: 'start' });
    });
    const info = li.appendChild(document.createElement('span'));
    info.className = 'count';
    info.textContent = `${count(d)}개`;
    if (d.format === 'ehnd') info.insertAdjacentHTML('afterend', '<span class="fmt">Ehnd</span>');
    const acts = li.appendChild(document.createElement('span'));
    acts.className = 'acts';
    const act = (label: string, title: string, fn: () => void, disabled = false) => {
      const b = acts.appendChild(document.createElement('button'));
      b.type = 'button';
      b.textContent = label;
      b.title = title;
      b.disabled = disabled;
      b.addEventListener('click', fn);
    };
    act('↑', '위로', () => move(d, i - 1), i === 0);
    act('↓', '아래로', () => move(d, i + 1), i === dicts.length - 1);
    act('이름 변경', '이름 변경', () => {
      const n = prompt('사전 이름', d.name)?.trim();
      if (!n) return;
      d.name = n;
      store?.put(d).catch(() => {});
      if (d === editing) select(d);
      else render();
    });
    act('내보내기', '내보내기', () => {
      const f = toFile(d);
      save(f.name, utf8(f.text), textType);
    });
    act('삭제', '삭제', () => remove(d), dicts.length === 1);
    li.addEventListener('dragstart', (ev) => {
      dragged = d;
      ev.dataTransfer?.setData('text/plain', d.name);
    });
    li.addEventListener('dragover', (ev) => {
      if (!dragged || dragged === d) return;
      ev.preventDefault();
    });
    li.addEventListener('drop', (ev) => {
      ev.preventDefault();
      if (!dragged || dragged === d) return;
      const box = li.getBoundingClientRect();
      const after = ev.clientY > box.top + box.height / 2;
      const next = dicts.filter((x) => x !== dragged);
      next.splice(next.indexOf(d) + (after ? 1 : 0), 0, dragged);
      dragged = undefined;
      reorder(next);
    });
    li.addEventListener('dragend', () => (dragged = undefined));
  });
}

async function add(d: Omit<Dictionary, 'id' | 'order'>): Promise<Dictionary> {
  const full: Dictionary = { ...d, id: newId(), order: dicts.length };
  dicts.push(full);
  await store?.put(full).catch(() => {});
  return full;
}

async function remove(d: Dictionary) {
  if (dicts.length === 1 || !confirm(`'${d.name}' 사전을 지울까요?`)) return;
  dicts = dicts.filter((x) => x !== d);
  await store?.remove(d.id).catch(() => {});
  reorder(dicts);
  if (d === editing) select(dicts[0]);
}

$('dict-new').addEventListener('click', async () => {
  const taken = new Set(dicts.map((d) => d.name));
  let name = '새 사전';
  for (let k = 2; taken.has(name); k++) name = `새 사전 ${k}`;
  select(await add({ name, format: 'hdtrans', on: true, entries: '', names: '' }));
  dictsChanged();
});
const fileInput = $<HTMLInputElement>('dict-file');
$('dict-import').addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', async () => {
  let last: Dictionary | undefined;
  const failed: string[] = [];
  for (const f of Array.from(fileInput.files ?? [])) {
    try {
      const d = fromFile(f.name, new Uint8Array(await f.arrayBuffer()));
      if (d) last = await add(d);
      else failed.push(f.name);
    } catch {
      failed.push(f.name);
    }
  }
  fileInput.value = '';
  if (failed.length > 0) alert('읽지 못함: ' + failed.join(', '));
  if (last) select(last);
  dictsChanged();
});
// The applied dictionaries as the desktop reads them: userdict.txt,
// UserDict.txt for Ehnd ones, or both in a ZIP.
$('dict-export').addEventListener('click', async () => {
  const d = overlay(dicts);
  const out = [
    { name: 'userdict.txt', text: d.userdict },
    { name: 'UserDict.txt', text: d.ehnd },
  ].filter((f) => f.text !== '');
  if (out.length === 1) save(out[0].name, utf8(out[0].text + '\n'), textType);
  else if (out.length === 2) {
    save('hdtrans-사전.zip', await zip(out.map((f) => ({ name: f.name, data: utf8(f.text + '\n') }))), 'application/zip');
  }
});
editSelect.addEventListener('change', () => {
  const d = dicts.find((x) => x.id === editSelect.value);
  if (d) select(d);
});

openStore()
  .then(async (s) => {
    dicts = await s.all();
    store = s;
    navigator.storage?.persist?.().catch(() => {});
  })
  .catch((err) => (warning.textContent = '사전을 열지 못했습니다: ' + String(err)))
  .finally(async () => {
    if (dicts.length === 0) await add({ name: firstName, format: 'hdtrans', on: true, entries: '', names: '' });
    select(dicts.find((d) => d.id === localStorage.getItem('hdtrans.edit')) ?? dicts[0]);
    dictsLoaded = true;
    document.body.dataset.dicts = 'ready';
    dictsChanged();
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
      counted();
      fitSource();
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
    warning.textContent = '예제를 불러오지 못했습니다: ' + String(err);
  }
});

const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
let seq = 0;
type Reply = { lines?: string[]; message?: string; warning?: string; ehndWarning?: string[]; ms: number; memory: number };
const pending = new Map<number, (m: Reply) => void>();

function request(mode: string, lines: string[], d: { userdict: string; ehnd: string }): Promise<Reply> {
  const id = ++seq;
  return new Promise<Reply>((resolve) => {
    pending.set(id, resolve);
    worker.postMessage({ type: 'translate', id, mode, speech: speech.value, lines, userdict: d.userdict, ehnd: d.ehnd });
  });
}
worker.onmessage = (ev) => {
  const m = ev.data;
  if (m.type === 'ready') {
    ready = true;
    document.body.dataset.engine = 'ready';
    document.body.dataset.memory = String(m.memory);
    const total = performance.now() - started;
    status.textContent = `준비 완료 · ${(total / 1000).toFixed(1)}초 (로딩 ${(m.ms / 1000).toFixed(1)}초, 메모리 ${Math.round(m.memory / 1e6)} MB)`;
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

/** The engine's load errors ("LINE: message", Ehnd "UserDict.txt:LINE: message") with the dictionary and row of each line. */
function warningText(warning: string, ehnd: string[], where: (ehnd: boolean, line: number) => string): string {
  const user = warning === '' ? [] : warning.split('\n').map((w) => w.replace(/^(\d+): /, (_, n) => where(false, Number(n)) + ': '));
  const e = ehnd.map((w) => w.replace(/^(?:.*\/)?UserDict\.txt:(\d+): /, (_, n) => where(true, Number(n)) + ': '));
  return [...user, ...e].join(' / ');
}

// The output shows the last translation in the output mode, source lines muted;
// its text is what 복사 copies and what a document saves.
let shownSource = '';
let shownTranslation = '';
let shownTime = ''; // n ms of the shown translation, '' after a failure
function showOutput() {
  out.replaceChildren(
    ...outputParts(shownSource, shownTranslation, outputMode()).map((p) => {
      if (!p.source) return p.text;
      const span = document.createElement('span');
      span.className = 'src';
      span.textContent = p.text;
      return span;
    }),
  );
  showInfo();
}
/** The size of what 복사 copies, and the time of the translation. */
function showInfo() {
  info.textContent = shownTime && `${size(out.textContent ?? '')} · ${shownTime}`;
}
out.addEventListener('keydown', (ev) => {
  if (ev.key !== 'a' || !(ev.ctrlKey || ev.metaKey)) return;
  ev.preventDefault();
  getSelection()?.selectAllChildren(out);
});
const copy = $<HTMLButtonElement>('copy');
copy.addEventListener('click', async () => {
  if (await copyText(out.textContent ?? '')) copy.textContent = '복사됨';
  else {
    getSelection()?.selectAllChildren(out);
    copy.textContent = '선택됨';
  }
  setTimeout(() => (copy.textContent = '복사'), 1500);
});

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
  if (!ready || !dictsLoaded || running || shown === generation) return;
  running = true;
  const g = generation;
  const lines = src.value.replace(/\r/g, '').split('\n');
  const d = overlay(dicts);
  const t0 = performance.now();
  const slow = setTimeout(() => (tbar.hidden = false), 300);
  let text = '';
  let problem = '';
  try {
    const m = await request(mode(), lines, d);
    if (m.lines === undefined) throw new Error(m.message);
    document.body.dataset.memory = String(m.memory);
    text = `${Math.round(performance.now() - t0)} ms`;
    const w = warningText(m.warning ?? '', m.ehndWarning ?? [], d.where);
    if (w) problem = '사전 오류: ' + w;
    if (g === generation) {
      shownSource = lines.join('\n');
      shownTranslation = m.lines.join('\n');
      showOutput();
    }
  } catch (err) {
    problem = '번역 실패: ' + (err instanceof Error ? err.message : String(err));
  }
  clearTimeout(slow);
  running = false;
  tbar.hidden = true;
  if (g !== generation) {
    translate();
    return;
  }
  shown = g;
  shownTime = text;
  showInfo();
  warning.textContent = problem;
  out.dataset.done = '1';
}

// The document tab: files are read and translated here, one after another,
// each as one document. Results are kept as text; the output mode applies
// when a file is saved.

interface Doc {
  out: string; // output file name
  source?: string; // undefined if the file could not be read
  lineBreak: string; // the source's first line break, used in the output
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

const outputBytes = (d: Doc) => encodeOutput(formatDocument(d.source!, d.translated!, outputMode()), d.lineBreak);
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
    const d: Doc = { out, source: decoded?.text, lineBreak: decoded?.lineBreak ?? '\n', row, save: button };
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
      const dictionaries = overlay(dicts);
      const m = await request('document', d.source!.split('\n'), dictionaries);
      if (m.lines === undefined) throw new Error(m.message);
      if (docsCancel) break;
      d.translated = m.lines.join('\n');
      const w = warningText(m.warning ?? '', m.ehndWarning ?? [], dictionaries.where);
      warning.textContent = w ? '사전 오류: ' + w : '';
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

/**
 * Hex SHA-256 of b. Outside a secure context (plain http on a LAN address)
 * crypto.subtle is missing and the page hashes in JavaScript, which takes a
 * fraction of a second for the dictionary; the check still catches truncated
 * or stale downloads, which plain http does nothing against.
 */
async function sha256(b: Uint8Array): Promise<string> {
  const d = globalThis.crypto?.subtle ? new Uint8Array(await crypto.subtle.digest('SHA-256', b as BufferSource)) : sha256Bytes(b);
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
    if (document.body.dataset.engine === 'error') return;
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
