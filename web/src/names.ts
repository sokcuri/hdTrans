// The part of speech of a user dictionary row for each kind of a name list.
const kindPos: Record<string, string> = {
  given: 'name', full: 'name', nick: 'name', family: 'surname', place: 'n', org: 'n', title: 'n',
};

/**
 * Returns a names table as a plain dictionary: a line without a part of
 * speech gets name, a kind becomes its part of speech, the option hon is
 * dropped, and a line without a tab is split at its first run of spaces.
 */
export function namesDict(text: string): string {
  return text
    .split('\n')
    .map((line) => {
      line = line.replace(/\r$/, '');
      const t = line.trim();
      if (t === '' || t.startsWith('//')) return line;
      let f = line.split('\t');
      if (f.length === 1) {
        const i = t.indexOf(' ');
        f = i < 0 ? [t] : [t.slice(0, i), t.slice(i + 1)];
      }
      if (f.length === 2) f.push('name');
      else if (f.length > 2 && f[2].trim() === '') f[2] = 'name';
      else f[2] = kindPos[f[2].trim()] ?? f[2];
      if (f.length > 3) {
        f[3] = f[3].split(' ').filter((o) => o !== 'hon').join(' ');
        while (f.length > 3 && f[f.length - 1].trim() === '') f.pop();
      }
      return f.join('\t');
    })
    .join('\n');
}

const kinds = new Set(['given', 'family', 'full', 'nick', 'place', 'org', 'title']);
// The parts of speech a names table took when its rows were user dictionary rows.
const legacyKinds: Record<string, string> = { name: 'full', surname: 'family', n: 'org' };

/**
 * Returns a names table as a name list (docs/dictionary-files.md), one line
 * for each line: the kind defaults to full, the part of speech of an older
 * table becomes its kind (name full, surname family, n org), only the option
 * hon is kept, and // comments become # comments. A line without a tab is
 * split at its first run of spaces.
 */
export function namesList(text: string): string {
  return text
    .split('\n')
    .map((line) => {
      line = line.replace(/\r$/, '');
      const t = line.trim();
      if (t === '') return '';
      if (t.startsWith('//')) return '#' + t.slice(2);
      if (t.startsWith('#')) return line;
      let f = line.split('\t');
      if (f.length === 1) {
        const i = t.indexOf(' ');
        f = i < 0 ? [t] : [t.slice(0, i), t.slice(i + 1)];
      }
      if (f.length < 2) return line;
      const kind = (f[2] ?? '').trim();
      f[2] = kind === '' ? 'full' : kinds.has(kind) ? kind : legacyKinds[kind] ?? kind;
      const hon = (f[3] ?? '').split(/\s+/).includes('hon');
      return [f[0], f[1], f[2], ...(hon ? ['hon'] : []), ...f.slice(4)].join('\t');
    })
    .join('\n');
}
