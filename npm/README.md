# hdtrans

Japanese to Korean translation for Node.js 22 or newer. The package includes the WebAssembly engine and a self-contained `dicts.hdsnap` base dictionary, with no TSV dictionary tree. No compiler, dictionary download or filesystem setup is needed. Translation runs locally.

## Install

```sh
npm install hdtrans
```

```js
import { HdTrans } from 'hdtrans';

const translator = await HdTrans.init();
try {
  console.log(translator.translate('今日は本を読みます。'));
} finally {
  translator.close();
}
```

## Version information

```js
const info = await HdTrans.getInfo();
console.log(info.version);           // Actual engine build version
console.log(info.dictionaryVersion); // Loaded base dictionary version
console.log(info.runtime);           // 'wasm'
```

`HdTrans.getInfo(options)` accepts the same options as `init()`. It loads the engine and dictionary, then closes its temporary translator; it does not just read package metadata. If loading fails, it rejects. Importing the package alone does not load the engine.

For an existing translator, `translator.getInfo()` returns the same fields synchronously. The existing `version`, `dictionaryVersion` and `runtime` properties remain available. Instance metadata is captured at initialization and remains readable after `close()`. User dictionary and Ehnd changes do not change the base dictionary version. TypeScript users can import the `HdTransInfo` type.

## Options and JSON

```js
const translator = await HdTrans.init({
  ehnd: {
    'PreFilter_custom.txt': '魔導具\t学生\t1\t0\n',
  },
});
try {
  const result = translator.translate('学生です。', { json: true });
  console.log(result.text);
  console.log(result.choices); // Selected dictionary entries and rules
  console.log(result.logs);   // Ehnd-style stage and fired-rule messages
} finally {
  translator.close();
}
```

`init()` also takes `speech` (`'strict'` or `'soft'`), `speechLevels` (`'auto'` or `'keep'`) and `wrapped`; `translate(text, { speech, speechLevels, wrapped, context })` applies them to one call, where `context` lists the lines before `text` in the same document, read only for their cues and not returned. `setSpeech()`, `setSpeechLevels()` and `setWrapped()` set them for later calls. With `speechLevels: 'auto'` (the default) a dialogue line that 「」 or 『』 opens has its sentence end rewritten to the speech level of the dialogue rules (「白崎、俺が手伝うから大丈夫だ」 → 「시라사키, 내가 도우니까 괜찮아」); `'keep'` leaves the translation's own level (「시라사키, 내가 도우니까 괜찮다」). Nothing is kept from one call to the next. These are the shared engine options and results, not a separate npm translation mode. Ehnd files are UTF-8 strings keyed by their original `PreFilter*.txt`, `PostFilter*.txt`, `SkipLayer*.txt` or `UserDict*.txt` names. No external Ehnd dictionary is included or enabled by default. `setEhnd(files)` replaces an instance's files; `clearEhnd()` removes them.

## User dictionary

```ts
import { HdTrans, type UserDictEntry, type TranslationResult } from 'hdtrans';

const translator: HdTrans = await HdTrans.init();
try {
  const entries: UserDictEntry[] = [
    { ja: '蒼月', ko: '아오츠키', pos: 'surname', options: 'prio=2 force' },
  ];
  translator.setUserDict(entries);
  console.log(translator.getUserDict());

  translator.setUserDictRaw('# My dictionary\n学生\t학우\tn\n');
  console.log(translator.getUserDictRaw());
  console.log(translator.userDictError);
  const result: TranslationResult = translator.translate('学生', { json: true });
  console.log(result.text);
  translator.clearUserDict();
} finally {
  translator.close();
}
```

`getUserDict()` returns registered source rows as an independent copy. A malformed raw update keeps the active dictionary and reports `userDictError`; inspect it after updating. `filterErrors` reports rejected filter rows. `reset()` clears translation context. `close()` releases the translator and can be called more than once. Instances have independent user dictionaries, filters and context.

## Name list

```js
const translator = await HdTrans.init({ names: '衣更\t이사라\n' });
translator.translate('衣更は今日も元気だ。'); // 이사라는 오늘도 건강하다.
translator.setNames('');                       // drops the list
```

A name list gives the names of one work, one per line: `JA<TAB>KO[<TAB>KIND[<TAB>hon]]`, in the format of the native `--names` (kinds `given`, `family`, `full`, `nick`, `place`, `org`, `title`). It applies to each call's document and does not change the dictionaries. `init({ names })` and `setNames(text)` set it for later calls; `translate(text, { names })` for one call. `setNames` compiles the list, so give it once per document, not per line. A malformed line or a name that does not compile throws after the other lines are applied; `getNames()` returns the last text given.

## CLI

```sh
npx hdtrans '今日は本を読みます。'
printf '学生です。\n' | npx hdtrans
npx hdtrans --in input.txt --json
npx hdtrans --file input.txt --out output.txt
npx hdtrans --in input.txt --json --out result.json
printf '学生です。\n' | npx hdtrans --json
npx hdtrans --version
npx hdtrans --why '学生です。'
npx hdtrans --userdict userdict.tsv '学生'
npx hdtrans --ehnd ./Ehnd '学生'
npx hdtrans --wrapped --context previous.txt 'ならない。'
```

Files and stdin are UTF-8. `--file` is an alias of `--in`. `--out` writes the result to a file instead of stdout. `--json` writes one JSON document, including when used with `--out`. `--why` writes execution logs verbatim and `WHY` choice records to stderr, leaving the translation on stdout. `--version` loads the bundled dictionary and prints one line: `hdTrans v<engine> (wasm) / dict <dictionary>`. The versions are also available as `translator.version` and `translator.dictionaryVersion`; `translator.runtime` is `'wasm'`. User dictionary and Ehnd changes do not change the base dictionary version. `--wrapped` reads input whose lines break mid-sentence (captions, hooked text) and keeps the line count; a blank line always ends a sentence. `--context PATH` reads UTF-8 lines that come before the input in the same document: they are translated only for their cues (names, quotes, speech) and are not written. The CLI accepts one `--userdict` file. It does not discover dictionaries or configuration from the current directory. Run `hdtrans --help` for options.

## License

Copyright (c) Sokcuri (https://github.com/sokcuri). Licensed under MPL-2.0, see `LICENSE`. Dependency licenses are in `THIRD-PARTY-NOTICES.txt`. Source and dictionary updates are available at [sokcuri/hdTrans](https://github.com/sokcuri/hdTrans).

## Building the package from source

This section is for package maintainers, not npm users. Build the native CLI and WebAssembly engine from the same source tree, then compile the shared TypeScript API:

```sh
npm --prefix web ci
npm --prefix web run compile
npm --prefix npm ci
node tools/package-npm.mjs --cli build/mac/hdtrans --wasm build/wasm --wrapper build/web/translator.js
npm --prefix npm test
npm pack ./npm
```

`--cli` and `--wasm` also accept `HDTRANS_CLI` and `HDTRANS_WASM`. `--wrapper` defaults to `build/web/translator.js` and accepts `HDTRANS_NPM_WRAPPER`. The tool compiles a fresh standalone snapshot from repository `dicts/`. To reuse the snapshot made for native archives, replace `--cli CLI` with `--snapshot FILE`:

```sh
node tools/package-npm.mjs --snapshot build/dicts.hdsnap --wasm build/wasm
```

Both modes verify that the bundled WASM can open the snapshot. `dictionary.json` records its checksum and builtin/data fingerprints, with an empty `files` list. The dictionary version is the npm version plus a hash of every source `dicts/*.tsv`, including `kana.tsv`, using sorted UTF-8 basenames and original file bytes. Source TSVs are never rewritten or packed. The tool does not publish anything.
