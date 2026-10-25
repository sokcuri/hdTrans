export type UserDictPos = 'n' | 'phrase' | 'name' | 'surname' | 'intj' | 'lead' | 'hon'
  | 'v' | 'v1' | 'v5' | 'adj' | 'na' | 'adv' | 'ono' | 'end' | 'pat'
  | `${'v' | 'v1' | 'v5' | 'adj' | 'na'}:${string}`;

export interface UserDictEntry {
  ja: string;
  ko: string;
  pos?: UserDictPos;
  /** Native TSV options, for example "prio=2 force". */
  options?: string;
}

export interface ChoiceSelection {
  index: number;
  /** Selected engine target, before output composition and marker expansion. */
  text: string;
  provenance: string;
  layer: string;
  id: number;
  /** Local rule number, global rule file:line, sense table:row, or empty when no
   *  source is recorded. */
  source: string;
}

export interface TranslationChoice {
  /** Zero-based pipeline indices within this call, after preprocessing and line splitting. */
  line: number;
  chunk: number;
  sentence: number;
  token: number;
  /** UTF-8 byte offset within the normalized token, not the original document. */
  start: number;
  surface: string;
  choice: ChoiceSelection;
  ending: (ChoiceSelection & { surface: string }) | null;
}

export interface TranslationResult {
  text: string;
  choices: TranslationChoice[];
  /** Ehnd-style stage and fired-rule messages, including trailing newlines. Not load diagnostics. */
  logs: string[];
}

/** The speech level: 'strict' (the default) keeps plain sentence ends (간다) but where the
 *  Japanese marks casual speech; 'soft' writes them in 해체 (갈 거야) inside 「」 and on lines
 *  without 「」 that read as speech. */
export type Speech = 'soft' | 'strict';

/** UTF-8 contents of PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt. */
export type EhndFiles = Readonly<Record<string, string>>;

interface Binding {
  open(directories: readonly string[]): boolean;
  translate(text: string): string;
  translateJson(text: string): TranslationResult;
  reset(): boolean;
  setSpeech(speech: Speech): boolean;
  setUserDictRaw(text: string): boolean;
  getUserDict(): UserDictEntry[];
  setEhnd(files: EhndFiles): boolean;
  close(): void;
  delete(): void;
  readonly error: string;
  readonly dictionaryVersion: string;
  readonly userDictError: string;
  readonly filterErrors: string[];
}

/** Bootstrap module returned by the Emscripten factory. */
export interface WasmModule {
  Translator: new () => Binding;
  version(): string;
}

export interface HdTransInfo {
  readonly version: string;
  readonly dictionaryVersion: string;
  readonly runtime: 'wasm';
}

export interface InitOptions {
  /** Supply an instantiated module with its dictionary files already staged. */
  module?: WasmModule | Promise<WasmModule>;
  /** Virtual filesystem paths, optionally starting with a compiled .hdsnap. */
  directories?: readonly string[];
  ehnd?: EhndFiles;
  speech?: Speech;
}

export class HdTrans {
  readonly version: string;
  readonly runtime = 'wasm' as const;
  private binding_: Binding | undefined;
  private dictionaryVersion_ = '';
  private userDictRaw_ = '';
  private speech_: Speech = 'strict';

  private constructor(binding: Binding, version: string) {
    this.binding_ = binding;
    this.version = version;
  }

  static async init(options: InitOptions = {}): Promise<HdTrans> {
    const directories = options.directories ?? ['/dicts'];
    if (!Array.isArray(directories) || directories.some((path) => typeof path !== 'string' || path.includes('\0'))) {
      throw new TypeError('hdtrans: dictionary paths must be strings without NUL');
    }
    let module = await options.module;
    if (!module) {
      const engine = './hdtrans_engine.js';
      const { default: create } = await import(engine);
      module = await create() as WasmModule;
    }
    const version = module.version();
    const binding = new module.Translator();
    const translator = new HdTrans(binding, version);
    try {
      if (!binding.open(directories)) throw new Error(binding.error);
      translator.dictionaryVersion_ = binding.dictionaryVersion;
      if (options.ehnd) translator.setEhnd(options.ehnd);
      if (options.speech) translator.setSpeech(options.speech);
      return translator;
    } catch (error) {
      translator.close();
      throw error;
    }
  }

  /** Loads the engine and dictionaries, reads their metadata, then closes the temporary translator. */
  static async getInfo(options: InitOptions = {}): Promise<HdTransInfo> {
    const translator = await HdTrans.init(options);
    try {
      return translator.getInfo();
    } finally {
      translator.close();
    }
  }

  /** Metadata captured at initialization, also available after close(). */
  getInfo(): HdTransInfo {
    return {
      version: this.version,
      dictionaryVersion: this.dictionaryVersion,
      runtime: this.runtime,
    };
  }

  /** options.speech applies to this call only; setSpeech() sets it for later calls. */
  translate(text: string): string;
  translate(text: string, options: { json: true; speech?: Speech }): TranslationResult;
  translate(text: string, options: { json?: false; speech?: Speech }): string;
  translate(text: string, options: { json?: boolean; speech?: Speech }): string | TranslationResult;
  translate(text: string, options: { json?: boolean; speech?: Speech } = {}): string | TranslationResult {
    const binding = this.binding();
    if (typeof text !== 'string') throw new TypeError('hdtrans: text must be a string');
    if (options.speech === undefined) return options.json ? binding.translateJson(text) : binding.translate(text);
    const previous = this.speech_;
    this.setSpeech(options.speech);
    try {
      return options.json ? binding.translateJson(text) : binding.translate(text);
    } finally {
      this.setSpeech(previous);
    }
  }

  /** The speech level of later translations; 'strict' until set. */
  setSpeech(speech: Speech): void {
    const binding = this.binding();
    if (!binding.setSpeech(speech)) throw new Error(binding.error);
    this.speech_ = speech;
  }

  reset(): void {
    const binding = this.binding();
    if (!binding.reset()) throw new Error(binding.error);
  }

  setUserDict(entries: readonly UserDictEntry[]): void {
    const rows = entries.map((entry) => {
      const fields = [entry.ja, entry.ko, entry.pos ?? '', entry.options ?? ''];
      if (fields.some((field) => typeof field !== 'string' || /[\t\r\n\0]/.test(field))) {
        throw new TypeError('hdtrans: user dictionary fields must be strings without tabs, newlines or NUL');
      }
      return fields.join('\t').replace(/\t+$/, '');
    });
    this.setUserDictRaw(rows.length ? rows.join('\n') + '\n' : '');
  }

  /** Replaces the overlay and resets context. A malformed update keeps the prior overlay and sets userDictError. */
  setUserDictRaw(text: string): void {
    const binding = this.binding();
    if (typeof text !== 'string') throw new TypeError('hdtrans: user dictionary must be a string');
    if (!binding.setUserDictRaw(text)) throw new Error(binding.error);
    this.userDictRaw_ = text;
  }

  /** Active registered source rows, not expanded or merged winners. Returns an independent copy. */
  getUserDict(): UserDictEntry[] {
    return this.binding().getUserDict();
  }

  /** Last submitted text, including comments and malformed rows. */
  getUserDictRaw(): string {
    this.binding();
    return this.userDictRaw_;
  }

  clearUserDict(): void {
    this.setUserDictRaw('');
  }

  get dictionaryVersion(): string {
    return this.dictionaryVersion_;
  }

  get userDictError(): string {
    return this.binding().userDictError;
  }

  get filterErrors(): string[] {
    return this.binding().filterErrors;
  }

  /** Replaces only this instance's Ehnd files and resets context. No Ehnd files load by default. */
  setEhnd(files: EhndFiles): void {
    const binding = this.binding();
    for (const [name, text] of Object.entries(files)) {
      if (!/^(PreFilter|PostFilter|SkipLayer|UserDict)[^/\\\0]*\.txt$/i.test(name) || typeof text !== 'string') {
        throw new TypeError('hdtrans: expected named Ehnd .txt files with string contents');
      }
    }
    if (!binding.setEhnd(files)) throw new Error(binding.error);
  }

  clearEhnd(): void {
    this.setEhnd({});
  }

  close(): void {
    const binding = this.binding_;
    if (!binding) return;
    this.binding_ = undefined;
    try {
      binding.close();
    } finally {
      binding.delete();
    }
  }

  private binding(): Binding {
    if (!this.binding_) throw new Error('hdtrans: the translator is closed');
    return this.binding_;
  }
}
