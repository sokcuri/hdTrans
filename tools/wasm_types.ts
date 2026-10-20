import { HdTrans, type EhndFiles, type HdTransInfo, type InitOptions, type TranslationResult, type UserDictEntry } from '../web/src/translator.js';

declare const translator: HdTrans;
declare const enabled: boolean;

const ordinary: string = translator.translate('学生');
const disabled: string = translator.translate('学生', { json: false });
const omitted: string = translator.translate('学生', {});
const structured: TranslationResult = translator.translate('学生', { json: true });
const conditional: string | TranslationResult = translator.translate('学生', { json: enabled });
// @ts-expect-error A true JSON option returns an object.
const notString: string = translator.translate('学生', { json: true });
// @ts-expect-error A false JSON option returns a string.
const notStructured: TranslationResult = translator.translate('学生', { json: false });
// @ts-expect-error A boolean variable cannot promise only an object.
const notAlwaysStructured: TranslationResult = translator.translate('学生', { json: enabled });

const entries: readonly UserDictEntry[] = [
  { ja: '蒼月', ko: '아오츠키', pos: 'surname' },
  { ja: '手伝う', ko: '돕다', pos: 'v:ㅂ', options: 'prio=2 force' },
];
translator.setUserDict(entries);
translator.setUserDictRaw('学生\t학생\n');
const copy: UserDictEntry[] = translator.getUserDict();
copy.push({ ja: '学生', ko: '학생' });
const raw: string = translator.getUserDictRaw();
translator.clearUserDict();
// @ts-expect-error The typed setter does not accept raw TSV.
translator.setUserDict('学生\t학생\n');
// @ts-expect-error The raw setter does not accept objects.
translator.setUserDictRaw(entries);
// @ts-expect-error Entries need Japanese and Korean text.
translator.setUserDict([{ ja: '学生' }]);
// @ts-expect-error Use the native POS vocabulary.
translator.setUserDict([{ ja: '学生', ko: '학생', pos: 'noun' }]);

const filters: EhndFiles = { 'PostFilter_test.txt': '학생\t학우\t1\t0\n' };
translator.setEhnd(filters);
translator.clearEhnd();
const options: InitOptions = { ehnd: filters };
// @ts-expect-error Selection settings are not public initialization options.
HdTrans.init({ cg: false });
// @ts-expect-error Selection settings are not public initialization options.
HdTrans.init({ misread: true });
// @ts-expect-error Selection settings are not public initialization options.
HdTrans.init({ reference: true });
const initialized: Promise<HdTrans> = HdTrans.init(options);
const inspected: Promise<HdTransInfo> = HdTrans.getInfo({ ...options, directories: ['/custom.hdsnap'] });
const info: HdTransInfo = translator.getInfo();
const infoVersion: string = info.version;
const infoDictionary: string = info.dictionaryVersion;
const infoRuntime: 'wasm' = info.runtime;
// @ts-expect-error Metadata fields are read-only.
info.version = 'other';
// @ts-expect-error Metadata fields are read-only.
info.dictionaryVersion = 'other';
// @ts-expect-error The runtime is WASM.
const native: 'native' = info.runtime;
// @ts-expect-error Dictionary paths must be strings.
HdTrans.getInfo({ directories: [42] });
const version: string = translator.version;
const dictionaryVersion: string = translator.dictionaryVersion;
const runtime: 'wasm' = translator.runtime;
const diagnostics: string[] = translator.filterErrors;
const userError: string = translator.userDictError;
const choices: number = structured.choices[0].choice.index;
// @ts-expect-error The version is read-only.
translator.version = 'other';
// @ts-expect-error The dictionary version is read-only.
translator.dictionaryVersion = 'other';
// @ts-expect-error The runtime is read-only.
translator.runtime = 'native';

void [ordinary, disabled, omitted, structured, conditional, notString, notStructured, notAlwaysStructured,
  raw, initialized, inspected, info, infoVersion, infoDictionary, infoRuntime, native,
  version, dictionaryVersion, runtime, diagnostics, userError, choices];
