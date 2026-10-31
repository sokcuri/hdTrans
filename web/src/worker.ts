// The engine runs here, off the page's thread.
// in: init with engine URL, compiled module and snapshot;
//     translate with id, mode, speech, lines, userdict, ehnd (the text of an
//     Ehnd UserDict file) and names (a name list).
// out: ready/fatal, result/error, with skipped dictionary and name list lines
//      reported as warnings.
import { HdTrans, type WasmModule } from './translator.js';

interface Module extends WasmModule {
  FS: { mkdir(path: string): void; writeFile(path: string, data: Uint8Array | string): void };
  HEAPU8: Uint8Array;
}

let m: Module | undefined;
let translator: HdTrans | undefined;
let loaded = '';
let loadedEhnd = '';
let loadedNames = '';
let namesWarning: string[] = []; // "LINE: message" of the loaded name list

self.onmessage = async (ev: MessageEvent) => {
  const msg = ev.data;
  if (msg.type === 'init') {
    try {
      const t0 = performance.now();
      const { default: create } = await import(msg.engine);
      const e: Module = await create({
        instantiateWasm(imports: WebAssembly.Imports, done: (i: WebAssembly.Instance) => void) {
          WebAssembly.instantiate(msg.module as WebAssembly.Module, imports).then(done);
          return {};
        },
      });
      e.FS.writeFile('/dicts.hdsnap', new Uint8Array(msg.snapshot));
      translator?.close();
      translator = await HdTrans.init({ module: e, directories: ['/dicts.hdsnap'], strictDict: false });
      loaded = '';
      loadedEhnd = '';
      loadedNames = '';
      namesWarning = [];
      m = e;
      postMessage({ type: 'ready', ms: performance.now() - t0, memory: e.HEAPU8.length });
    } catch (err) {
      postMessage({ type: 'fatal', message: String(err) });
    }
    return;
  }
  if (msg.type !== 'translate' || !m || !translator) return;
  const e = m;
  const t = translator;
  try {
    const t0 = performance.now();
    if (msg.userdict !== loaded) {
      t.setUserDictRaw(msg.userdict);
      loaded = msg.userdict;
    }
    const ehnd: string = msg.ehnd ?? '';
    if (ehnd !== loadedEhnd) {
      t.setEhnd(ehnd === '' ? {} : { 'UserDict.txt': ehnd + '\n' });
      loadedEhnd = ehnd;
    }
    const names: string = msg.names ?? '';
    if (names !== loadedNames) {
      loadedNames = names;
      namesWarning = [];
      try {
        t.setNames(names);
      } catch (err) {
        namesWarning = String(err instanceof Error ? err.message : err).split('\n').slice(1);
      }
    }
    t.setSpeech(msg.speech);
    let lines: string[];
    if (msg.mode === 'document') {
      t.reset();
      lines = t.translate(msg.lines.join('\n')).split('\n');
    } else {
      lines = msg.lines.map((line: string) => {
        t.reset();
        return t.translate(line);
      });
    }
    const warning = t.userDictError.replace(/^userdict\.tsv:/gm, '');
    const ehndWarning = ehnd === '' ? [] : t.filterErrors;
    postMessage({ type: 'result', id: msg.id, lines, ms: performance.now() - t0, warning, ehndWarning, namesWarning, memory: e.HEAPU8.length });
  } catch (err) {
    postMessage({ type: 'error', id: msg.id, message: String(err) });
  }
};
