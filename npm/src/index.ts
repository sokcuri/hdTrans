import { readFile } from 'node:fs/promises';
import createEngine, { type EngineModule } from './hdtrans_engine.js';
import { HdTrans as RuntimeHdTrans, type HdTransInfo, type InitOptions as RuntimeInitOptions } from './runtime.js';

export type {
  UserDictPos,
  UserDictEntry,
  ChoiceSelection,
  TranslationChoice,
  TranslationResult,
  EhndFiles,
  HdTransInfo,
} from './runtime.js';

export type InitOptions = Pick<RuntimeInitOptions, 'ehnd'>;
export type HdTrans = RuntimeHdTrans;

let modulePromise: Promise<EngineModule> | undefined;

async function loadModule(): Promise<EngineModule> {
  const wasmBinary = await readFile(new URL('./hdtrans_engine.wasm', import.meta.url));
  const module = await createEngine({ wasmBinary });
  for (const file of ['dicts.hdsnap', 'dicts.txt']) {
    module.FS.writeFile(`/${file}`, await readFile(new URL(file, import.meta.url)));
  }
  return module;
}

/** Uses the bundled engine and dictionary. Each call returns an independent translator. */
export const HdTrans = Object.freeze({
  /** Loads the bundled engine and dictionary, reads their metadata, then closes the temporary translator. */
  async getInfo(options: InitOptions = {}): Promise<HdTransInfo> {
    const translator = await HdTrans.init(options);
    try {
      return translator.getInfo();
    } finally {
      translator.close();
    }
  },

  async init(options: InitOptions = {}): Promise<HdTrans> {
    modulePromise ??= loadModule().catch((error) => {
      modulePromise = undefined;
      throw error;
    });
    return RuntimeHdTrans.init({
      module: await modulePromise,
      directories: ['/dicts.hdsnap'],
      ehnd: options.ehnd,
    });
  },
});
