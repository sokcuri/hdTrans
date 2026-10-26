// The user's dictionaries, kept in IndexedDB. Each has a name, a file format
// (an hdTrans user dictionary or an Ehnd UserDict file), its entries as
// tab-separated text, a names table (hdTrans only), whether it is applied and
// its place in the list; the first is the highest priority.
import { decodeText } from './documents.js';
import { namesDict } from './names.js';

export type Format = 'hdtrans' | 'ehnd';

export interface Dictionary {
  id: string;
  name: string;
  format: Format;
  on: boolean;
  order: number;
  entries: string;
  names: string;
}

export interface Store {
  /** The dictionaries in list order. */
  all(): Promise<Dictionary[]>;
  put(d: Dictionary): Promise<void>;
  remove(id: string): Promise<void>;
}

export const firstName = '기본 사용자 사전';
const dbName = 'hdtrans';
const storeName = 'dictionaries';

export const newId = () => Date.now().toString(36) + Math.random().toString(36).slice(2, 10);

/** The dictionary made on first run from the user dictionary and names table kept in localStorage. */
function migrated(): Dictionary {
  return {
    id: newId(),
    name: firstName,
    format: 'hdtrans',
    on: true,
    order: 0,
    entries: localStorage.getItem('hdtrans.userdict') ?? '',
    names: localStorage.getItem('hdtrans.names') ?? '',
  };
}

const done = <T>(r: IDBRequest<T>) =>
  new Promise<T>((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });

/**
 * Opens the store. The first open creates it holding the migrated
 * dictionary; without IndexedDB the dictionaries live in memory.
 */
export async function openStore(): Promise<Store> {
  let db: IDBDatabase;
  try {
    const req = indexedDB.open(dbName, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(storeName, { keyPath: 'id' }).add(migrated());
    db = await done(req);
  } catch {
    const mem = new Map<string, Dictionary>();
    const d = migrated();
    mem.set(d.id, d);
    return {
      all: async () => [...mem.values()].map((d) => ({ ...d })).sort((a, b) => a.order - b.order),
      put: async (d) => void mem.set(d.id, { ...d }),
      remove: async (id) => void mem.delete(id),
    };
  }
  db.onversionchange = () => db.close();
  const tx = (mode: IDBTransactionMode) => db.transaction(storeName, mode).objectStore(storeName);
  return {
    all: async () => ((await done(tx('readonly').getAll())) as Dictionary[]).sort((a, b) => a.order - b.order),
    put: async (d) => void (await done(tx('readwrite').put(d))),
    remove: async (id) => void (await done(tx('readwrite').delete(id))),
  };
}

const isComment = (line: string, format: Format) => {
  const t = line.trim();
  return t === '' || t.startsWith('//') || (format === 'hdtrans' && t.startsWith('#'));
};

/** The number of entry rows of d, names included. */
export function count(d: Dictionary): number {
  const rows = (t: string) => t.split('\n').filter((l) => !isComment(l, d.format)).length;
  return rows(d.entries) + (d.format === 'hdtrans' ? rows(d.names) : 0);
}

const hdtransPos = /^(n|phrase|name|surname|place|intj|lead|open|hon|v|v1|v5|adj|na|adv|ono|end|pat)(:.*)?$/;

/**
 * The format of a dictionary file: by its third column (a number in Ehnd, a
 * part of speech in hdTrans), else by its name (UserDict… is Ehnd).
 */
export function detect(text: string, file: string): Format {
  let ehnd = 0;
  let hdtrans = 0;
  for (const line of text.split('\n')) {
    if (isComment(line, 'hdtrans')) continue;
    const third = (line.split('\t')[2] ?? '').trim();
    if (/^\d/.test(third)) ehnd++;
    else if (hdtransPos.test(third)) hdtrans++;
  }
  if (ehnd !== hdtrans) return ehnd > hdtrans ? 'ehnd' : 'hdtrans';
  return /^UserDict/.test(file) ? 'ehnd' : 'hdtrans';
}

/** A dictionary read from a desktop file, not yet stored; undefined if the file is not text (decodeText). */
export function fromFile(file: string, b: Uint8Array): Omit<Dictionary, 'id' | 'order'> | undefined {
  let text = decodeText(b)?.text;
  if (text === undefined) return undefined;
  const format = detect(text, file);
  if (format === 'hdtrans') text = text.replace(/^#!.*\n?/, '');
  const name = file.replace(/\.(txt|tsv)$/i, '').replace(/^userdict[-_ ]?/i, '') || file;
  return { name, format, on: true, entries: text.split('\n').filter((l) => l.trim() !== '').join('\n'), names: '' };
}

/** The file of d as the desktop reads it: name and text. */
export function toFile(d: Dictionary): { name: string; text: string } {
  const safe = d.name.replace(/[\\/:*?"<>|\x00-\x1f]/g, '_');
  const parts = [d.entries, d.format === 'hdtrans' ? namesDict(d.names) : ''].filter((t) => t.trim() !== '');
  return {
    name: d.format === 'ehnd' ? `UserDict_${safe}.txt` : `userdict-${safe}.txt`,
    text: parts.map((t) => t + '\n').join(''),
  };
}

interface Part {
  label: string;
  lines: number;
}

/**
 * The engine's user dictionary from the applied dictionaries of list (in
 * list order): userdict holds the entries of each hdTrans dictionary, then
 * their names tables; ehnd the entries of each Ehnd dictionary. In a text an
 * earlier line ranks first among equal keys.
 */
export function overlay(list: Dictionary[]): { userdict: string; ehnd: string; where: (ehnd: boolean, line: number) => string } {
  const on = list.filter((d) => d.on);
  const join = (texts: [string, string][]) => {
    const parts: Part[] = [];
    const kept: string[] = [];
    for (const [label, t] of texts) {
      if (t.trim() === '') continue;
      kept.push(t);
      parts.push({ label, lines: t.split('\n').length });
    }
    return { text: kept.join('\n'), parts };
  };
  const user = join([
    ...on.filter((d) => d.format === 'hdtrans').map((d): [string, string] => [`${d.name} 사용자 사전`, d.entries]),
    ...on.filter((d) => d.format === 'hdtrans').map((d): [string, string] => [`${d.name} 이름 표`, namesDict(d.names)]),
  ]);
  const ehnd = join(on.filter((d) => d.format === 'ehnd').map((d): [string, string] => [d.name, d.entries]));
  return {
    userdict: user.text,
    ehnd: ehnd.text,
    where(isEhnd, line) {
      for (const p of (isEhnd ? ehnd : user).parts) {
        if (line <= p.lines) return `${p.label} ${line}행`;
        line -= p.lines;
      }
      return `${line}행`;
    },
  };
}
