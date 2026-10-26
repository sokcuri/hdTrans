import {
  HdTrans,
  type InitOptions,
  type UserDictEntry,
  type UserDictPos,
  type ChoiceSelection,
  type TranslationChoice,
  type TranslationResult,
  type EhndFiles,
  type HdTransInfo,
  type Speech,
  type TranslateOptions,
} from 'hdtrans';

async function check() {
  const options: InitOptions = { ehnd: { 'PreFilter_test.txt': '' } };
  const inspected: HdTransInfo = await HdTrans.getInfo(options);
  const translator: HdTrans = await HdTrans.init(options);
  const info: HdTransInfo = translator.getInfo();
  const version: string = info.version;
  const infoDictionary: string = info.dictionaryVersion;
  const infoRuntime: 'wasm' = info.runtime;
  // @ts-expect-error Metadata fields are read-only.
  info.version = 'other';
  // @ts-expect-error Metadata fields are read-only.
  info.dictionaryVersion = 'other';
  // @ts-expect-error The runtime is WASM.
  const native: 'native' = info.runtime;
  const plain: string = translator.translate('学生');
  const explained: TranslationResult = translator.translate('学生', { json: true });
  const explicitPlain: string = translator.translate('学生', { json: false });
  const dynamic: string | TranslationResult = translator.translate('学生', { json: Math.random() > 0.5 });
  const speech: Speech = 'soft';
  const callOptions: TranslateOptions = { speech, wrapped: true, context: ['明日までに行かなければ'] };
  const continued: string | TranslationResult = translator.translate('ならない。', callOptions);
  translator.setWrapped(false);
  const wrappedInit: Promise<HdTrans> = HdTrans.init({ speech, wrapped: true });
  const pos: UserDictPos = 'v:ㄷ';
  const rows: UserDictEntry[] = [{ ja: '聞く', ko: '듣다', pos }];
  translator.setUserDict(rows);
  const active: UserDictEntry[] = translator.getUserDict();
  translator.setUserDictRaw('学生\t학우\n');
  const raw: string = translator.getUserDictRaw();
  const dictionaryVersion: string = translator.dictionaryVersion;
  const runtime: 'wasm' = translator.runtime;
  const error: string = translator.userDictError;
  const errors: string[] = translator.filterErrors;
  const files: EhndFiles = { 'PostFilter_test.txt': '' };
  translator.setEhnd(files);
  translator.clearEhnd();
  translator.clearUserDict();
  translator.reset();
  translator.close();
  const choice: TranslationChoice | undefined = explained.choices[0];
  const selection: ChoiceSelection | undefined = choice?.choice;
  const logs: string[] = explained.logs;
  void [inspected, info, version, infoDictionary, infoRuntime, native,
    plain, explicitPlain, dynamic, continued, wrappedInit, active, raw, dictionaryVersion, runtime, error, errors, selection, logs];

  // @ts-expect-error Engine implementation switches are not public npm options.
  await HdTrans.init({ reference: true });
  // @ts-expect-error Engine implementation switches are not public npm options.
  await HdTrans.init({ cg: false });
  // @ts-expect-error Engine implementation switches are not public npm options.
  await HdTrans.init({ misread: true });
  // @ts-expect-error The package stages its own module and dictionaries.
  await HdTrans.init({ directories: ['/dicts'] });
  // @ts-expect-error Metadata inspection uses the package's bundled dictionaries too.
  await HdTrans.getInfo({ directories: ['/dicts'] });
  // @ts-expect-error JSON mode returns a structured result.
  const wrong: string = translator.translate('学生', { json: true });
  void wrong;
}
void check;
