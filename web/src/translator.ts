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

/** The speech levels of dialogue lines: 'auto' (the default) writes a line that 「」 opens again at the
 *  level its cues give after the translation; 'keep' keeps the translation's own levels. */
export type SpeechLevels = 'auto' | 'keep';

/** Options of one translate() call; speech and wrapped apply to this call only. */
export interface TranslateOptions {
  json?: boolean;
  speech?: Speech;
  speechLevels?: SpeechLevels;
  wrapped?: boolean;
  /** Lines that come before text in the same document, read only for their cues and, when
   *  wrapped, for the start of a sentence text goes on with; they are not returned. */
  context?: readonly string[];
}

/** UTF-8 contents of PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt and UserDict*.txt. */
export type EhndFiles = Readonly<Record<string, string>>;

type DictionaryCallOptions = { strict?: boolean };

function dictionaryStrict(value: boolean | undefined): boolean | undefined {
  if (value !== undefined && typeof value !== 'boolean') {
    throw new TypeError('hdtrans: strict dictionary option must be a boolean');
  }
  return value;
}

interface Binding {
  open(directories: readonly string[], strictDict: boolean | undefined): boolean;
  translate(text: string, context: string): string;
  translateJson(text: string, context: string): TranslationResult;
  reset(): boolean;
  setSpeech(speech: Speech): boolean;
  setSpeechLevels(levels: SpeechLevels): boolean;
  setWrapped(wrapped: boolean): void;
  setUserDict(entries: readonly { ja: string; ko: string; pos: string; options: string }[], strict: boolean | undefined): boolean;
  setUserDictRaw(text: string, strict: boolean | undefined): boolean;
  getUserDict(): UserDictEntry[];
  setEhnd(files: EhndFiles, strict: boolean | undefined): boolean;
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
  speechLevels?: SpeechLevels;
  wrapped?: boolean;
  strictDict?: boolean;
}

export class HdTrans {
  readonly version: string;
  readonly runtime = 'wasm' as const;
  private binding_: Binding | undefined;
  private dictionaryVersion_ = '';
  private userDictRaw_ = '';
  private speech_: Speech = 'strict';
  private speechLevels_: SpeechLevels = 'auto';
  private wrapped_ = false;

  private constructor(binding: Binding, version: string) {
    this.binding_ = binding;
    this.version = version;
  }

  static async init(options: InitOptions = {}): Promise<HdTrans> {
    const strictDict = dictionaryStrict(options.strictDict);
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
      if (!binding.open(directories, strictDict)) throw new Error(binding.error);
      translator.dictionaryVersion_ = binding.dictionaryVersion;
      if (options.ehnd) translator.setEhnd(options.ehnd);
      if (options.speech) translator.setSpeech(options.speech);
      if (options.speechLevels) translator.setSpeechLevels(options.speechLevels);
      if (options.wrapped !== undefined) translator.setWrapped(options.wrapped);
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

  /** options.speech, options.speechLevels and options.wrapped apply to this call only; setSpeech(),
   *  setSpeechLevels() and setWrapped() set them for later calls. Nothing is kept from one call to the next. */
  translate(text: string): string;
  translate(text: string, options: TranslateOptions & { json: true }): TranslationResult;
  translate(text: string, options: TranslateOptions & { json?: false }): string;
  translate(text: string, options: TranslateOptions): string | TranslationResult;
  translate(text: string, options: TranslateOptions = {}): string | TranslationResult {
    const binding = this.binding();
    if (typeof text !== 'string') throw new TypeError('hdtrans: text must be a string');
    const lines = options.context ?? [];
    if (!Array.isArray(lines) || lines.some((line) => typeof line !== 'string')) {
      throw new TypeError('hdtrans: context must be an array of strings');
    }
    const context = lines.join('\n');
    const speech = this.speech_;
    const speechLevels = this.speechLevels_;
    const wrapped = this.wrapped_;
    try {
      if (options.speech !== undefined) this.setSpeech(options.speech);
      if (options.speechLevels !== undefined) this.setSpeechLevels(options.speechLevels);
      if (options.wrapped !== undefined) this.setWrapped(options.wrapped);
      return options.json ? binding.translateJson(text, context) : binding.translate(text, context);
    } finally {
      if (options.speech !== undefined) this.setSpeech(speech);
      if (options.speechLevels !== undefined) this.setSpeechLevels(speechLevels);
      if (options.wrapped !== undefined) this.setWrapped(wrapped);
    }
  }

  /** The speech level of later translations; 'strict' until set. */
  setSpeech(speech: Speech): void {
    const binding = this.binding();
    if (!binding.setSpeech(speech)) throw new Error(binding.error);
    this.speech_ = speech;
  }

  /** The speech levels of the dialogue lines of later translations; 'auto' until set. */
  setSpeechLevels(levels: SpeechLevels): void {
    const binding = this.binding();
    if (!binding.setSpeechLevels(levels)) throw new Error(binding.error);
    this.speechLevels_ = levels;
  }

  /** Whether the lines of later translations may break mid-sentence (captions, hooked text);
   *  the line count is kept. false until set. */
  setWrapped(wrapped: boolean): void {
    const binding = this.binding();
    if (typeof wrapped !== 'boolean') throw new TypeError('hdtrans: wrapped must be a boolean');
    binding.setWrapped(wrapped);
    this.wrapped_ = wrapped;
  }

  reset(): void {
    const binding = this.binding();
    if (!binding.reset()) throw new Error(binding.error);
  }

  setUserDict(entries: readonly UserDictEntry[], options: DictionaryCallOptions = {}): void {
    const rows = entries.map((entry) => {
      const fields = [entry.ja, entry.ko, entry.pos ?? '', entry.options ?? ''];
      if (fields.some((field) => typeof field !== 'string' || /[\t\r\n\0]/.test(field))) {
        throw new TypeError('hdtrans: user dictionary fields must be strings without tabs, newlines or NUL');
      }
      return fields;
    });
    const binding = this.binding();
    const strict = dictionaryStrict(options.strict);
    this.userDictRaw_ = rows.length ? rows.map((fields) => fields.join('\t').replace(/\t+$/, '')).join('\n') + '\n' : '';
    const normalized = rows.map(([ja, ko, pos, entryOptions]) => ({ ja, ko, pos, options: entryOptions }));
    if (!binding.setUserDict(normalized, strict)) throw new Error(binding.error);
  }

  /** Replaces the overlay and resets context. A rejected update keeps the prior overlay. */
  setUserDictRaw(text: string, options: DictionaryCallOptions = {}): void {
    const binding = this.binding();
    if (typeof text !== 'string') throw new TypeError('hdtrans: user dictionary must be a string');
    const strict = dictionaryStrict(options.strict);
    this.userDictRaw_ = text;
    if (!binding.setUserDictRaw(text, strict)) throw new Error(binding.error);
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
  setEhnd(files: EhndFiles, options: DictionaryCallOptions = {}): void {
    const binding = this.binding();
    for (const [name, text] of Object.entries(files)) {
      if (!/^(PreFilter|PostFilter|SkipLayer|UserDict)[^/\\\0]*\.txt$/i.test(name) || typeof text !== 'string') {
        throw new TypeError('hdtrans: expected named Ehnd .txt files with string contents');
      }
    }
    if (!binding.setEhnd(files, dictionaryStrict(options.strict))) throw new Error(binding.error);
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
