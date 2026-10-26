// A small table editor for tab-separated dictionary text: one input per
// cell, Tab/Shift+Tab and the arrow keys move between cells, Enter adds a
// row, Ctrl+Shift+Backspace deletes one, a paste holding tabs or newlines
// (from a spreadsheet) fills the cells from the focused one on, and Ctrl+C
// without a selection copies the row. The text is the rows that have
// content, cells joined by tabs (empty trailing cells dropped), rows by
// newlines; fields past the last column stay in the last cell, joined by
// tabs.

export interface Grid {
  text: string;
  /** Sets the column headers, the placeholders of the first row and the download name. */
  heads(columns: string[], placeholders: string[], name: string): void;
}

/** Splits TSV text into rows of fields; quoted fields (Excel, Sheets) are unquoted. */
export function parseTSV(text: string): string[][] {
  const lines = text.replace(/\r\n?/g, '\n').replace(/\n$/, '').split('\n');
  return lines.map((l) => l.split('\t').map((f) => (/^".*"$/s.test(f) ? f.slice(1, -1).replace(/""/g, '"') : f)));
}

/**
 * Builds the editor in box with the given column headers and cell
 * placeholders. onChange gets the text after every edit; name is the file
 * name of a download.
 */
export function grid(
  box: HTMLElement,
  columns: string[],
  placeholders: string[],
  name: string,
  onChange: (text: string) => void,
): Grid {
  const n = columns.length;
  const scroll = document.createElement('div');
  scroll.className = 'gridscroll';
  const table = document.createElement('table');
  table.className = 'grid';
  const head = table.createTHead().insertRow();
  for (let c = 0; c <= n; c++) head.appendChild(document.createElement('th'));
  const body = table.createTBody();

  const bar = document.createElement('div');
  bar.className = 'gridbar';
  const button = (label: string, title: string, fn: () => void) => {
    const b = bar.appendChild(document.createElement('button'));
    b.type = 'button';
    b.textContent = label;
    b.title = title;
    b.addEventListener('click', fn);
  };
  const file = document.createElement('input');
  file.type = 'file';
  file.accept = '.tsv,.txt,text/plain,text/tab-separated-values';
  file.hidden = true;
  file.addEventListener('change', async () => {
    const f = file.files?.[0];
    if (!f) return;
    setText(await f.text());
    file.value = '';
    changed();
  });
  button('+ 행', '행 추가', () => focus(addRow(body.rows.length), 0));
  button('파일 열기', 'TSV 파일 불러오기', () => file.click());
  button('파일 저장', 'TSV 파일로 내려받기', () => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text() + '\n'], { type: 'text/tab-separated-values' }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  });
  button('TSV 복사', '표 전체를 탭 구분 텍스트로 복사', () => navigator.clipboard.writeText(text()));
  button('모두 지우기', '모든 행 삭제', () => {
    if (text() !== '' && !confirm('모든 행을 지울까요?')) return;
    setText('');
    changed();
  });
  bar.appendChild(file);
  scroll.append(table);
  box.append(scroll, bar);

  const changed = () => onChange(text());
  const cell = (r: number, c: number) => body.rows[r]?.cells[c]?.firstElementChild as HTMLInputElement | undefined;
  const focus = (r: number, c: number) => {
    const el = cell(r, c);
    el?.focus();
    el?.select();
  };
  const where = (el: EventTarget | null): [number, number] => {
    const td = (el as HTMLElement).closest('td')!;
    return [(td.parentElement as HTMLTableRowElement).sectionRowIndex, td.cellIndex];
  };

  function addRow(at: number, values: string[] = []): number {
    const tr = body.insertRow(at);
    for (let c = 0; c < n; c++) {
      const input = tr.insertCell().appendChild(document.createElement('input'));
      input.value = values[c] ?? '';
      input.placeholder = at === 0 ? (placeholders[c] ?? '') : '';
      input.spellcheck = false;
    }
    const del = tr.insertCell().appendChild(document.createElement('button'));
    del.type = 'button';
    del.className = 'del';
    del.textContent = '×';
    del.title = '행 삭제';
    del.tabIndex = -1;
    return tr.sectionRowIndex;
  }

  function deleteRow(r: number) {
    body.deleteRow(r);
    if (body.rows.length === 0) addRow(0);
    changed();
  }

  function row(r: number): string {
    const f = Array.from({ length: n }, (_, c) => cell(r, c)!.value);
    while (f.length > 0 && f[f.length - 1] === '') f.pop();
    return f.join('\t');
  }

  function text(): string {
    const rows: string[] = [];
    for (let r = 0; r < body.rows.length; r++) {
      const s = row(r);
      if (s.trim() !== '') rows.push(s);
    }
    return rows.join('\n');
  }

  function setText(t: string) {
    body.replaceChildren();
    if (t !== '') for (const f of parseTSV(t)) addRow(body.rows.length, [...f.slice(0, n - 1), f.slice(n - 1).join('\t')]);
    if (body.rows.length === 0) addRow(0);
  }

  body.addEventListener('input', changed);
  body.addEventListener('click', (ev) => {
    const b = (ev.target as HTMLElement).closest('button.del');
    if (!b) return;
    const [r] = where(b);
    deleteRow(r);
    focus(Math.min(r, body.rows.length - 1), 0);
  });

  body.addEventListener('keydown', (ev) => {
    const el = ev.target as HTMLInputElement;
    if (el.tagName !== 'INPUT' || ev.isComposing || ev.altKey) return;
    const [r, c] = where(el);
    const last = body.rows.length - 1;
    const mod = ev.ctrlKey || ev.metaKey;
    const atStart = el.selectionStart === 0 && el.selectionEnd === 0;
    const atEnd = el.selectionStart === el.value.length && el.selectionEnd === el.value.length;
    let to: [number, number] | undefined;
    if (ev.key === 'Tab' && !mod) {
      if (ev.shiftKey) {
        if (c > 0) to = [r, c - 1];
        else if (r > 0) to = [r - 1, n - 1];
        else return;
      } else if (c < n - 1) to = [r, c + 1];
      else {
        if (r === last) addRow(r + 1);
        to = [r + 1, 0];
      }
    } else if (ev.key === 'Enter' && !mod && !ev.shiftKey) to = [addRow(r + 1), 0];
    else if (ev.key === 'ArrowUp' && r > 0) to = [r - 1, c];
    else if (ev.key === 'ArrowDown' && r < last) to = [r + 1, c];
    else if (ev.key === 'ArrowLeft' && atStart && c > 0) to = [r, c - 1];
    else if (ev.key === 'ArrowRight' && atEnd && c < n - 1) to = [r, c + 1];
    else if (ev.key === 'Backspace' && mod && ev.shiftKey) {
      deleteRow(r);
      to = [Math.min(r, body.rows.length - 1), c];
    }
    if (!to) return;
    ev.preventDefault();
    focus(...to);
  });

  body.addEventListener('paste', (ev) => {
    const t = ev.clipboardData?.getData('text/plain') ?? '';
    if (!/[\t\n]/.test(t.replace(/\r?\n$/, ''))) return;
    ev.preventDefault();
    const [r0, c0] = where(ev.target);
    parseTSV(t).forEach((f, i) => {
      if (r0 + i >= body.rows.length) addRow(r0 + i);
      f.forEach((v, j) => {
        const el = cell(r0 + i, Math.min(c0 + j, n - 1))!;
        el.value = c0 + j < n ? v : el.value + '\t' + v;
      });
    });
    changed();
  });

  body.addEventListener('copy', (ev) => {
    const el = ev.target as HTMLInputElement;
    if (el.tagName !== 'INPUT' || el.selectionStart !== el.selectionEnd) return;
    ev.preventDefault();
    ev.clipboardData?.setData('text/plain', row(where(el)[0]));
  });

  function heads(cols: string[], holders: string[], file: string) {
    cols.forEach((c, i) => (head.cells[i].textContent = c));
    placeholders = holders;
    name = file;
    for (let c = 0; c < n; c++) cell(0, c)!.placeholder = placeholders[c] ?? '';
  }

  setText('');
  heads(columns, placeholders, name);
  return {
    heads,
    get text() {
      return text();
    },
    set text(t: string) {
      setText(t);
    },
  };
}
