// Text files for the document tab: decoding, output layout, names and a ZIP
// writer. The rules follow the Windows translation window.

export type OutputMode = 'translation' | 'interleaved' | 'spaced';

/**
 * The text of b with LF newlines, the name of its encoding and its first line
 * break ('\n' when it has none), or undefined.
 * BOMs take precedence, then strict UTF-8, then Shift-JIS (CP932).
 * UTF-16BE and text with NUL are refused.
 */
export function decodeText(b: Uint8Array): { text: string; encoding: string; lineBreak: string } | undefined {
  const decode = (label: string, bytes: Uint8Array) => {
    try {
      return new TextDecoder(label, { fatal: true, ignoreBOM: true }).decode(bytes);
    } catch {
      return undefined;
    }
  };
  let text: string | undefined;
  let encoding: string;
  if (b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf) {
    text = decode('utf-8', b.subarray(3));
    encoding = 'UTF-8 BOM';
  } else if (b[0] === 0xff && b[1] === 0xfe) {
    text = decode('utf-16le', b.subarray(2));
    encoding = 'UTF-16LE';
  } else if (b[0] === 0xfe && b[1] === 0xff) {
    return undefined;
  } else {
    text = decode('utf-8', b);
    encoding = 'UTF-8';
    if (text === undefined) {
      text = decode('shift_jis', b);
      encoding = 'Shift-JIS';
    }
  }
  if (text === undefined || text.includes('\0')) return undefined;
  const lineBreak = /\r\n|\r|\n/.exec(text)?.[0] ?? '\n';
  return { text: text.replace(/\r\n?/g, '\n'), encoding, lineBreak };
}

/** The lines of v; a final LF ends a line rather than starting one. */
function documentLines(v: string): string[] {
  const lines = v.split('\n');
  if (v === '' || v.endsWith('\n')) lines.pop();
  return lines;
}

/** A run of output text; source is true for the source lines of the mixed modes. */
export interface OutputPart {
  text: string;
  source: boolean;
}

/**
 * The output file text in parts. Blank lines stay single in every mode; spaced
 * puts a blank line between pairs. If the line counts differ, mixed modes show
 * the whole source and the whole translation as one pair.
 */
export function outputParts(source: string, translated: string, mode: OutputMode): OutputPart[] {
  const s = documentLines(source);
  const t = documentLines(translated);
  const pairs: [string, string][] = s.length === t.length ? s.map((line, i) => [line, t[i]]) : [[s.join('\n'), t.join('\n')]];
  const parts: OutputPart[] = [];
  const put = (text: string, isSource = false) => {
    const last = parts[parts.length - 1];
    if (last && last.source === isSource) last.text += text;
    else if (text !== '') parts.push({ text, source: isSource });
  };
  let hasLine = false;
  let hasPair = false;
  for (const [a, b] of pairs) {
    if (hasLine) put('\n');
    if (a !== '' || b !== '') {
      if (hasPair && mode === 'spaced') put('\n');
      if (mode !== 'translation') put(a + '\n', true);
      put(b);
      hasPair = true;
    }
    hasLine = true;
  }
  if (hasLine && (source.endsWith('\n') || translated.endsWith('\n'))) put('\n');
  return parts;
}

/** The output file text: the parts of outputParts joined. */
export function formatDocument(source: string, translated: string, mode: OutputMode): string {
  return outputParts(source, translated, mode)
    .map((p) => p.text)
    .join('');
}

/** <stem>_번역.txt for the file name name. */
export function outputName(name: string): string {
  const dot = name.lastIndexOf('.');
  return (dot > 0 ? name.slice(0, dot) : name) + '_번역.txt';
}

/** UTF-8 with a BOM and each LF written as lineBreak, as the Windows window writes. */
export function encodeOutput(text: string, lineBreak = '\n'): Uint8Array {
  const body = new TextEncoder().encode(lineBreak === '\n' ? text : text.replaceAll('\n', lineBreak));
  const b = new Uint8Array(body.length + 3);
  b.set([0xef, 0xbb, 0xbf]);
  b.set(body, 3);
  return b;
}

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(b: Uint8Array): number {
  let c = 0xffffffff;
  for (const x of b) c = crcTable[(c ^ x) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** Raw deflate of b, or undefined where CompressionStream lacks it. */
async function deflateRaw(b: Uint8Array): Promise<Uint8Array | undefined> {
  if (typeof CompressionStream === 'undefined') return undefined;
  try {
    const stream = new Blob([b as BlobPart]).stream().pipeThrough(new CompressionStream('deflate-raw'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  } catch {
    return undefined;
  }
}

/**
 * A ZIP archive of files with UTF-8 names. Entries are deflated when that is
 * available and smaller, otherwise stored. No ZIP64: each file and the archive
 * stay under 4 GiB.
 */
export async function zip(files: { name: string; data: Uint8Array }[], date = new Date(), compress = true): Promise<Uint8Array> {
  const time = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
  const day = ((Math.max(date.getFullYear(), 1980) - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
  const parts: Uint8Array[] = [];
  const central: Uint8Array[] = [];
  let offset = 0;
  for (const f of files) {
    const name = new TextEncoder().encode(f.name);
    const deflated = compress ? await deflateRaw(f.data) : undefined;
    const method = deflated && deflated.length < f.data.length ? 8 : 0;
    const body = method === 8 ? deflated! : f.data;
    const crc = crc32(f.data);
    const local = new Uint8Array(30 + name.length);
    const l = new DataView(local.buffer);
    l.setUint32(0, 0x04034b50, true);
    l.setUint16(4, 20, true);
    l.setUint16(6, 0x0800, true);
    l.setUint16(8, method, true);
    l.setUint16(10, time, true);
    l.setUint16(12, day, true);
    l.setUint32(14, crc, true);
    l.setUint32(18, body.length, true);
    l.setUint32(22, f.data.length, true);
    l.setUint16(26, name.length, true);
    local.set(name, 30);
    const entry = new Uint8Array(46 + name.length);
    const c = new DataView(entry.buffer);
    c.setUint32(0, 0x02014b50, true);
    c.setUint16(4, 20, true);
    c.setUint16(6, 20, true);
    c.setUint16(8, 0x0800, true);
    c.setUint16(10, method, true);
    c.setUint16(12, time, true);
    c.setUint16(14, day, true);
    c.setUint32(16, crc, true);
    c.setUint32(20, body.length, true);
    c.setUint32(24, f.data.length, true);
    c.setUint16(28, name.length, true);
    c.setUint32(42, offset, true);
    entry.set(name, 46);
    parts.push(local, body);
    central.push(entry);
    offset += local.length + body.length;
  }
  const size = central.reduce((n, e) => n + e.length, 0);
  const end = new Uint8Array(22);
  const e = new DataView(end.buffer);
  e.setUint32(0, 0x06054b50, true);
  e.setUint16(8, files.length, true);
  e.setUint16(10, files.length, true);
  e.setUint32(12, size, true);
  e.setUint32(16, offset, true);
  const all = [...parts, ...central, end];
  const out = new Uint8Array(all.reduce((n, p) => n + p.length, 0));
  let at = 0;
  for (const p of all) {
    out.set(p, at);
    at += p.length;
  }
  return out;
}
