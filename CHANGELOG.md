# Changelog

## [0.5.5](https://github.com/sokcuri/hdTrans/compare/v0.5.4...v0.5.5) (2026-10-25)


### Bug Fixes

* **dict:** read set phrases of dialogue tone by their use ([#157](https://github.com/sokcuri/hdTrans/issues/157)) ([ed3d893](https://github.com/sokcuri/hdTrans/commit/ed3d8938262558565809fb79336cdcc935453da0))
* **engine:** keep nothing from one translate call to the next ([#156](https://github.com/sokcuri/hdTrans/issues/156)) ([c4e4ea7](https://github.com/sokcuri/hdTrans/commit/c4e4ea73c04dbe3f0581f584c618e5c4f3907cdf))
* **release:** keep translate calls independent and read common dialogue phrases naturally ([b0a3dcb](https://github.com/sokcuri/hdTrans/commit/b0a3dcb29056c73a4d95f75ba77b6e053aab21a4))

## [0.5.4](https://github.com/sokcuri/hdTrans/compare/v0.5.3...v0.5.4) (2026-10-25)


### Bug Fixes

* **dict:** read 第一子, 第一歩 and 第一声 as 첫째 아이, 첫걸음 and 첫마디 ([#153](https://github.com/sokcuri/hdTrans/issues/153)) ([b62f24f](https://github.com/sokcuri/hdTrans/commit/b62f24f9ff849f3dd8766dd511867e73d5800a73))
* **grammar:** read particles, endings and quotes of the web-example review classes ([#152](https://github.com/sokcuri/hdTrans/issues/152)) ([d853a81](https://github.com/sokcuri/hdTrans/commit/d853a81f8ba7059d0b9e40a0c99a3836b50aa862))
* **grammar:** read ほしい after a て form as -아 줬으면 좋겠다 ([#154](https://github.com/sokcuri/hdTrans/issues/154)) ([d948478](https://github.com/sokcuri/hdTrans/commit/d9484784702214f1174dfe779361a45b791d66b6))
* **release:** read function nouns, discourse words and particles by use and stop the merge conflicts of generated files ([6f63e90](https://github.com/sokcuri/hdTrans/commit/6f63e9016de7b811e1dce8b72cd2883963b6d7b1))
* **translation:** read function nouns, discourse words, names and the ぽい tic by use ([#151](https://github.com/sokcuri/hdTrans/issues/151)) ([d389075](https://github.com/sokcuri/hdTrans/commit/d389075db2181351850c1ac79a870a37f3ae8139))

## [0.5.3](https://github.com/sokcuri/hdTrans/compare/v0.5.2...v0.5.3) (2026-10-25)


### Features

* **speech:** speak plain ends before marks, ask politely before ？, read lone やった as 앗싸 and お兄ちゃん as 오빠 ([#138](https://github.com/sokcuri/hdTrans/issues/138)) ([49c4885](https://github.com/sokcuri/hdTrans/commit/49c4885d35d9f4c81899f1ab94f5a96d659969ae))


### Bug Fixes

* **dict:** dialogue senses, missing naval compounds and 하기 nouns before する ([#118](https://github.com/sokcuri/hdTrans/issues/118)) ([4c62425](https://github.com/sokcuri/hdTrans/commit/4c62425284fe1e89d3bd9abd19f9f22be1265481))
* **grammar:** read retorts after a pause, lone なきゃ, から and し after a finished predicate, and かと思った ([#136](https://github.com/sokcuri/hdTrans/issues/136)) ([93fba6d](https://github.com/sokcuri/hdTrans/commit/93fba6dd341a92be8d1ed2cec8a2cc3d062fb196))
* **layout:** close brackets without a space, read typed escapes as boundaries and fold old kanji forms ([#116](https://github.com/sokcuri/hdTrans/issues/116)) ([e1b91c7](https://github.com/sokcuri/hdTrans/commit/e1b91c75b833ddb5c815eb54a65698a38757df08))
* **names:** read names that a cue in the line or document names, and ちゃん as 짱 ([#126](https://github.com/sokcuri/hdTrans/issues/126)) ([5831ca5](https://github.com/sokcuri/hdTrans/commit/5831ca5f0464fad36a8947a178746988d438a1e1))
* **particles:** read って, でも, とは, より, で, ごと, それに and なんで by the words around them ([#121](https://github.com/sokcuri/hdTrans/issues/121)) ([ef6e1a3](https://github.com/sokcuri/hdTrans/commit/ef6e1a3503d4b802532484682430c877330cdd5e))
* release review clean-ups for 0.5.3 ([#143](https://github.com/sokcuri/hdTrans/issues/143)) ([2191b80](https://github.com/sokcuri/hdTrans/commit/2191b80c09ba89fe63562d447a0e3fb53eda8217))
* **release:** read names by their cues, particles by their neighbours and spoken ends by their marks ([89bf344](https://github.com/sokcuri/hdTrans/commit/89bf34466d69bfb741e22c55ad570c77bd1db40a))
* **sound:** decide a drawn run inside a word once for every sound start before it ([#141](https://github.com/sokcuri/hdTrans/issues/141)) ([62366f2](https://github.com/sokcuri/hdTrans/commit/62366f24bfca84c54a4db9b2b4515fd789b99209))
* **sound:** read emphasized, stretched and spaced-out input as its words ([#115](https://github.com/sokcuri/hdTrans/issues/115)) ([be7fc38](https://github.com/sokcuri/hdTrans/commit/be7fc38bd66a1e24449b76dde322f3e79fcf870d))
* **translation:** keep the kana the analysis cannot place, and ratchet the inputs that keep them ([#122](https://github.com/sokcuri/hdTrans/issues/122)) ([3309283](https://github.com/sokcuri/hdTrans/commit/3309283211d644cb29fe8d2d6c60fa73c2d0dab7))
* **translation:** read ー inside hiragana words through one drawn-word decision, じゃねえ, lone そ and drawn ちゃーん tags ([#133](https://github.com/sokcuri/hdTrans/issues/133)) ([989e97f](https://github.com/sokcuri/hdTrans/commit/989e97fc7a329fa20677fe1631ad6240612531d9))
* **translation:** read acceptable いい, rhetorical ではないか and the adnominal before の, and conjugate old and polite sentence ends ([#123](https://github.com/sokcuri/hdTrans/issues/123)) ([dfd23c2](https://github.com/sokcuri/hdTrans/commit/dfd23c288c0036a3cd7a4f6618922e1514255c82))
* **translation:** read drawn cry adjectives, のよ questions, 申せ, 好き runs and drawn 様 by use ([#130](https://github.com/sokcuri/hdTrans/issues/130)) ([c810b19](https://github.com/sokcuri/hdTrans/commit/c810b199af68a90618324c829add0c06e6a3c6b3))
* **translation:** read giving volitionals as 주마, the benefactive potential もらえ as 주다 and stored 出す as 꺼내다 ([#139](https://github.com/sokcuri/hdTrans/issues/139)) ([d1eb6c3](https://github.com/sokcuri/hdTrans/commit/d1eb6c346d28e09def8836b64df447c170ebfd7b))
* **translation:** read greetings, causative せ, predicates before a copula, であります and hiragana name calls by use ([#135](https://github.com/sokcuri/hdTrans/issues/135)) ([d70dbd5](https://github.com/sokcuri/hdTrans/commit/d70dbd5ecadbf438daabe8cf30393690551eead3))
* **translation:** read potential negations, verb forms said with ん, っ or と, and the よかった of relief ([#120](https://github.com/sokcuri/hdTrans/issues/120)) ([0dc0c80](https://github.com/sokcuri/hdTrans/commit/0dc0c80fca0d7acdb8d9b332fd0762faa00c4cb2))
* **translation:** read the speaker's offers, humble acts, sorties, orders and short answers in dialogue ([#124](https://github.com/sokcuri/hdTrans/issues/124)) ([9321cc9](https://github.com/sokcuri/hdTrans/commit/9321cc9133404a96529fda7099cb5183dd25fd1a))
* **translation:** read unit numbers, 第一 ordinals, ている result states, the Kansai ちゃう and いい感じ by use ([#132](https://github.com/sokcuri/hdTrans/issues/132)) ([eb3e2dd](https://github.com/sokcuri/hdTrans/commit/eb3e2dd43e32ed0199099bd2836c69835fcc6757))
* **translation:** read かける aspect, 助かる, lone 違う and kana homographs in dialogue ([#137](https://github.com/sokcuri/hdTrans/issues/137)) ([fd87014](https://github.com/sokcuri/hdTrans/commit/fd870143639a9c1900bbe5b7f2fcc493b64efd41))

## [0.5.2](https://github.com/sokcuri/hdTrans/compare/v0.5.1...v0.5.2) (2026-10-24)


### Bug Fixes

* follow-ups to the drops fixes (name suffixes, a repeated row, spec rows, CI drops budget) ([#108](https://github.com/sokcuri/hdTrans/issues/108)) ([ce59bef](https://github.com/sokcuri/hdTrans/commit/ce59bef980cbf00c2fb5f8be70efece2945454dd))
* release review clean-ups for 0.5.2 ([#128](https://github.com/sokcuri/hdTrans/issues/128)) ([a6ed2d0](https://github.com/sokcuri/hdTrans/commit/a6ed2d02ef1f5f2daa194fde553163819f7fea19))
* **build:** report dev for builds that are not on a release tag ([#111](https://github.com/sokcuri/hdTrans/issues/111)) ([96122d2](https://github.com/sokcuri/hdTrans/commit/96122d2f6a4dc16d9464dd8698096c8302d5872e))
* **contracted:** read the dropped kana of いる, あげる and やる as the full form ([#117](https://github.com/sokcuri/hdTrans/issues/117)) ([d33a3b3](https://github.com/sokcuri/hdTrans/commit/d33a3b32493baa13530e9b2c250028d7cf046858))
* **normalize:** read a run of っ inside a word as one っ ([#107](https://github.com/sokcuri/hdTrans/issues/107)) ([06e7162](https://github.com/sokcuri/hdTrans/commit/06e7162790fb8b3d5b5d4f3e8a19a252fcab10e3))
* **release:** keep meanings from flipping, decide (ら)れ once and read stretched spellings and laughter as words ([#129](https://github.com/sokcuri/hdTrans/issues/129)) ([cc37a84](https://github.com/sokcuri/hdTrans/commit/cc37a84e86fcfefc7ffbd41e3f4b1451b39b1580))
* **sound:** close every clipped copy of a repeated cry with ㅅ and read 痛っ as 아얏 ([#112](https://github.com/sokcuri/hdTrans/issues/112)) ([20e5c0a](https://github.com/sokcuri/hdTrans/commit/20e5c0af0f1cde62e9c3e946ab0c9a294140d618))
* **sound:** decide laughing w from the source and join it to laughter words ([#110](https://github.com/sokcuri/hdTrans/issues/110)) ([34165f6](https://github.com/sokcuri/hdTrans/commit/34165f687b3f96359f1e3d357e639ffd7197b16f))
* **sound:** read drawn ちゃーん, くーん and たーんと in sound words, verb forms and the adverb correctly ([#106](https://github.com/sokcuri/hdTrans/issues/106)) ([1cffeb5](https://github.com/sokcuri/hdTrans/commit/1cffeb5e03c77d78a817172f364aec457b236e14))
* **translation:** add a critical meaning-flip suite and fix と思うな, 行ってきた and に before potentials ([#99](https://github.com/sokcuri/hdTrans/issues/99)) ([a223173](https://github.com/sokcuri/hdTrans/commit/a2231732cbde256ffa82734e049a172688430dce))
* **translation:** conjugate obligations, read ちゃいそう, ますね promises, やばい and drawn honorifics by use ([#100](https://github.com/sokcuri/hdTrans/issues/100)) ([7f15b8e](https://github.com/sokcuri/hdTrans/commit/7f15b8e469fdde8ad636b8737ea3d2104aa3b2e8))
* **translation:** decide the reading of (ら)れ once, before word choice ([#104](https://github.com/sokcuri/hdTrans/issues/104)) ([2757ed1](https://github.com/sokcuri/hdTrans/commit/2757ed12e1e6ddf72034576b30d10d1067874b42))
* **translation:** keep ぴょんと after a particle a sound, and add no copula after a finished utterance ([#109](https://github.com/sokcuri/hdTrans/issues/109)) ([6ca11e7](https://github.com/sokcuri/hdTrans/commit/6ca11e7bf4aa8d25a8208047a251ab91bd902e9e))
* **translation:** read drawn words and adnominal な before く words as the words (べーつに, ようなクサい相談) ([#113](https://github.com/sokcuri/hdTrans/issues/113)) ([b418fe9](https://github.com/sokcuri/hdTrans/commit/b418fe918542da8954fd9794fd5d076e499d87ef))
* **translation:** read stutters before kanji words, でも after counts, katakana endings and name に by use ([#127](https://github.com/sokcuri/hdTrans/issues/127)) ([c65d5e8](https://github.com/sokcuri/hdTrans/commit/c65d5e8bddcb0f2fb40f416526265d308b068eb2))
* **translation:** widen construction families to variant sentences: causative-passives, passives, giving verbs, habits, くせに and counterfactuals ([#105](https://github.com/sokcuri/hdTrans/issues/105)) ([fbfb77e](https://github.com/sokcuri/hdTrans/commit/fbfb77ef4602c940533d349691bed4d73d9b7b10))
* **translation:** write passives, potentials and the で of vehicles in forms Korean takes ([#119](https://github.com/sokcuri/hdTrans/issues/119)) ([b371916](https://github.com/sokcuri/hdTrans/commit/b3719162f182237df567ebcd77fee4bdccc9b36f))

## [0.5.1](https://github.com/sokcuri/hdTrans/compare/v0.5.0...v0.5.1) (2026-10-24)


### Bug Fixes

* **dict:** read internet slang, laughter and archaic attributive speech ([#83](https://github.com/sokcuri/hdTrans/issues/83)) ([c9449d2](https://github.com/sokcuri/hdTrans/commit/c9449d2a3a6ec9688cd05344ed90d9bc0beae757))
* **dict:** spell アルガルド as 아르가르드 and ユフィリア as 유필리아 in the name rows ([#85](https://github.com/sokcuri/hdTrans/issues/85)) ([e66745a](https://github.com/sokcuri/hdTrans/commit/e66745a746ac584747c6ae9682b974ec8fbf9150))
* **dict:** write Japanese broadcaster names as Korean usage does ([#95](https://github.com/sokcuri/hdTrans/issues/95)) ([bf45757](https://github.com/sokcuri/hdTrans/commit/bf457573e424d881425fb7d4fff39df3c3ac79c4))
* **layout:** keep symbol runs whole in their width and space them after a comma ([#77](https://github.com/sokcuri/hdTrans/issues/77)) ([6abda28](https://github.com/sokcuri/hdTrans/commit/6abda2807c5768a07980a4cb546f9ded1414eb34))
* **layout:** keep the main verb 捨てる spaced after a て form ([#97](https://github.com/sokcuri/hdTrans/issues/97)) ([ee1b5a8](https://github.com/sokcuri/hdTrans/commit/ee1b5a8a44089d4f1b6bebf8da1ae43121dd3d7f))
* **layout:** space an opening bracket after a word ([#89](https://github.com/sokcuri/hdTrans/issues/89)) ([fbb2c6b](https://github.com/sokcuri/hdTrans/commit/fbb2c6bfb6a97c53e1dde5c7b10dfcc80d241071))
* **release:** read causative-passives, repeated cries, slang and registers naturally and stop dropping source text ([8f906f3](https://github.com/sokcuri/hdTrans/commit/8f906f30dc4ca1e2dacdab4dd7f4f2c61af607ac))
* **sound:** write moans and cries as Korean writing does ([#88](https://github.com/sokcuri/hdTrans/issues/88)) ([7b923f4](https://github.com/sokcuri/hdTrans/commit/7b923f4e7f2f5492ceae2063cb29d15e1f6de0d9))
* **speech:** turn drawn predicates that end in っ into the soft form again ([#98](https://github.com/sokcuri/hdTrans/issues/98)) ([1e1123e](https://github.com/sokcuri/hdTrans/commit/1e1123e9475a3ee7a6563a0ec001abab9e4b4062))
* **translation:** correct lexical data, name renderings and capacity verb senses ([#91](https://github.com/sokcuri/hdTrans/issues/91)) ([8d70d81](https://github.com/sokcuri/hdTrans/commit/8d70d81a9e931a1bcb3a013f67ac183315c1f6bc))
* **translation:** keep ruby idioms outside the tag, read bag/big with a final consonant, いっぱい before filling verbs, and narrow そうだ and 国人 ([#79](https://github.com/sokcuri/hdTrans/issues/79)) ([ef66288](https://github.com/sokcuri/hdTrans/commit/ef662882c1dbc2f37468a8229a05de90aac132b4))
* **translation:** keep who acts in causative-passives, passives and giving verbs, and read ところだった, くせに and past counterfactuals ([#82](https://github.com/sokcuri/hdTrans/issues/82)) ([2f2b386](https://github.com/sokcuri/hdTrans/commit/2f2b3861b7b12a7b4d891f0bfe36b8929b8754dd))
* **translation:** keep words after passive and causative stems, and read なんかない, よーだ, ちょ、待っ and stretched greetings ([#78](https://github.com/sokcuri/hdTrans/issues/78)) ([cb0c1f7](https://github.com/sokcuri/hdTrans/commit/cb0c1f7fe7f3ec298b24f4103a6041db07031528))
* **translation:** read lone and repeated kanji, 〜先, kanji-noun な, katakana-spelled native words, もう before counts and quoting って as Korean writes them ([#92](https://github.com/sokcuri/hdTrans/issues/92)) ([4a7fc39](https://github.com/sokcuri/hdTrans/commit/4a7fc39835287e88ddc862bf5b5c0d285fa3e138))
* **translation:** read repeated predicates as cries and join 안 되다 and the auxiliary 버리다 ([#96](https://github.com/sokcuri/hdTrans/issues/96)) ([00dba3e](https://github.com/sokcuri/hdTrans/commit/00dba3e82d3ecf6bc41e09ea178756fafeca6d49))
* **translation:** read UI prompts, keigo, business mail, news and tech terms the way Korean writes them ([#84](https://github.com/sokcuri/hdTrans/issues/84)) ([0e8a83c](https://github.com/sokcuri/hdTrans/commit/0e8a83c4997b9b97f90492a2b4c5f8225ea79b06))
* **translation:** read 好き好き好き copy by copy, emphatic counts with も as (이)나, ちょっと before a request as 잠깐 and もう一人の as 또 다른 ([#81](https://github.com/sokcuri/hdTrans/issues/81)) ([46978eb](https://github.com/sokcuri/hdTrans/commit/46978eb10be12091390c5ee8cc228e5c8ce262b3))
* **translation:** stop dropping contracted forms, missing words, name suffixes and sentence-final tics, and ratchet the drops of the measured sets ([#94](https://github.com/sokcuri/hdTrans/issues/94)) ([b52917d](https://github.com/sokcuri/hdTrans/commit/b52917dea85e4e4119a7336f553e9f21367fe5c6))
* **web:** give the example menu a fixed width ([#93](https://github.com/sokcuri/hdTrans/issues/93)) ([e1e1b68](https://github.com/sokcuri/hdTrans/commit/e1e1b6895c8c0724dfa15547dec5e9182c335398))
* **word:** transliterate a joined katakana run once instead of at every step ([#87](https://github.com/sokcuri/hdTrans/issues/87)) ([a6f4205](https://github.com/sokcuri/hdTrans/commit/a6f4205d379372c23574f3f2c123c186b975e402))

## [0.5.0](https://github.com/sokcuri/hdTrans/compare/v0.4.7...v0.5.0) (2026-10-24)


### Features

* **release:** read spoken Japanese, repeated words and compounds naturally and move special cases into tables ([b962937](https://github.com/sokcuri/hdTrans/commit/b9629375265b1a7978e939a295dc88f5924e82ad))
* **tools:** measure the test-data documents and web examples, and audit an error rate per release ([#59](https://github.com/sokcuri/hdTrans/issues/59)) ([3169c5f](https://github.com/sokcuri/hdTrans/commit/3169c5f4e6e886161c245016c2b37af9b8ecd468))


### Bug Fixes

* **dict:** read a dictionary compound noun whole after a shorter noun row ([#69](https://github.com/sokcuri/hdTrans/issues/69)) ([c3260af](https://github.com/sokcuri/hdTrans/commit/c3260af34caa5419856d9bc13eb6a90658486c68))
* **dict:** read katakana spellings, phrase titles and idioms as the words they are ([#55](https://github.com/sokcuri/hdTrans/issues/55)) ([f6b1d58](https://github.com/sokcuri/hdTrans/commit/f6b1d58b5d17d496f0f91128d9c9d49bb918213d))
* **dict:** read 経験豊富 as an adjective noun before だ, な and よ ([#75](https://github.com/sokcuri/hdTrans/issues/75)) ([b79f46c](https://github.com/sokcuri/hdTrans/commit/b79f46c7eed881aad693c72bbb6857254fa62ad9))
* **rules:** resolve the rule lint warnings that change translations, or record why they stay ([#57](https://github.com/sokcuri/hdTrans/issues/57)) ([f1f95dd](https://github.com/sokcuri/hdTrans/commit/f1f95ddc5b8dac4ba3c42269c72db1471e2e487a))
* **sound:** stop four out-of-range reads in the contraction and drawl steps, and narrow farewell, 出る and quoting って readings ([#76](https://github.com/sokcuri/hdTrans/issues/76)) ([e061376](https://github.com/sokcuri/hdTrans/commit/e06137660f7610ceabeafe203e59dcb580f6614c))
* **translation:** attach particle-start glosses, read でーす as the copula and という after a sound as 하는 ([#74](https://github.com/sokcuri/hdTrans/issues/74)) ([6478f76](https://github.com/sokcuri/hdTrans/commit/6478f765ebff39e3804e57369ad544fb59eacc64))
* **translation:** attach particles and endings after a gloss that ends in a space ([#67](https://github.com/sokcuri/hdTrans/issues/67)) ([067117e](https://github.com/sokcuri/hdTrans/commit/067117ec30f43ab8ea45d128e822756ff214b385))
* **translation:** keep a katakana second self, read に向けて before a verb as 를 향해, XもYも as 도…도 and 後は before entrusting as 나머지는 ([#70](https://github.com/sokcuri/hdTrans/issues/70)) ([eb21876](https://github.com/sokcuri/hdTrans/commit/eb21876d59847c772d1694d5f4d7e4b4156ab3cb))
* **translation:** read kanji and kana spellings of grammatical words alike ([#71](https://github.com/sokcuri/hdTrans/issues/71)) ([408f590](https://github.com/sokcuri/hdTrans/commit/408f5907ef6d701e3cdc6ad63ecb993a5121191a))
* **translation:** read Latin-word particles, ruby spans, と、そうだ, ちゃったよ, いっぱい, honorific progressives, 頭 and province names ([#66](https://github.com/sokcuri/hdTrans/issues/66)) ([8315e27](https://github.com/sokcuri/hdTrans/commit/8315e27380433252dbe5ff8b7ae5e9230d1c325c))
* **translation:** read repeated words copy by copy and keep the spaces of the source ([#58](https://github.com/sokcuri/hdTrans/issues/58)) ([79f3958](https://github.com/sokcuri/hdTrans/commit/79f39582902be6eaeb049b64e1843e50577c8663))
* **translation:** read spoken contractions, sentence-final ね, ばいい questions and imperative よ ([#62](https://github.com/sokcuri/hdTrans/issues/62)) ([d71b9d3](https://github.com/sokcuri/hdTrans/commit/d71b9d3ef5daac6c6a8a5a245a7537adac5731d5))
* **translation:** read いう after a place as 言う and 누구 before the subject 가 as 누가 ([#56](https://github.com/sokcuri/hdTrans/issues/56)) ([05ef028](https://github.com/sokcuri/hdTrans/commit/05ef028fa8600986648c89a142c6236751b011a3))
* **translation:** read 回 after a number as 번 when it is stressed or counts an action in dialogue, and document the number rules ([#68](https://github.com/sokcuri/hdTrans/issues/68)) ([89870ed](https://github.com/sokcuri/hdTrans/commit/89870ed4340dac3d9e222f2611f4a013202b5fb9))
* **translation:** read 見て before ！ or ？ at the end of a sentence as 봐, not 보고 ([#64](https://github.com/sokcuri/hdTrans/issues/64)) ([87f89d3](https://github.com/sokcuri/hdTrans/commit/87f89d38cb2165e8fc288b64c3775968fe36830a))

## [0.4.7](https://github.com/sokcuri/hdTrans/compare/v0.4.6...v0.4.7) (2026-10-23)


### Features

* **tools:** surface budget, pass experiments and pass mutation check ([#46](https://github.com/sokcuri/hdTrans/issues/46)) ([761e3ae](https://github.com/sokcuri/hdTrans/commit/761e3aea61999bc5f6eb771fad48257a8276a0d0))


### Bug Fixes

* **release:** read past でしょ, っす, よかった and quoted と naturally and move special cases into tables ([02cfde8](https://github.com/sokcuri/hdTrans/commit/02cfde856bc71da1f86b822336ef4034f07a9cc5))
* **translation:** read stretched words, past でしょ, よかった, counts and humming as Korean writes them ([#49](https://github.com/sokcuri/hdTrans/issues/49)) ([2521760](https://github.com/sokcuri/hdTrans/commit/25217607766849e826169b84cd88d7baa9d7d480))
* **translation:** stop passes that misread と words, 夢で, いう, quoted greetings, であった and verse lines; fix 枚目, ages, っす, polite でしょ and bowls ([#50](https://github.com/sokcuri/hdTrans/issues/50)) ([8d57711](https://github.com/sokcuri/hdTrans/commit/8d57711a6f04b62326f713301a818008194bdf6a))

## [0.4.6](https://github.com/sokcuri/hdTrans/compare/v0.4.5...v0.4.6) (2026-10-23)


### Bug Fixes

* **release:** read numbers, questions and cries naturally and harden the engine ([177b3c4](https://github.com/sokcuri/hdTrans/commit/177b3c44ec5ba2015f4b09a0511e839ec25dcf8a))
* **release:** read numbers, questions and cries naturally and harden the engine ([#45](https://github.com/sokcuri/hdTrans/issues/45)) ([177b3c4](https://github.com/sokcuri/hdTrans/commit/177b3c44ec5ba2015f4b09a0511e839ec25dcf8a))
* **translation:** read counts, labels, cries and questions as Korean games write them ([#44](https://github.com/sokcuri/hdTrans/issues/44)) ([588508d](https://github.com/sokcuri/hdTrans/commit/588508d047d77c37464cc5e800dc8f04fce562f9))

## [0.4.5](https://github.com/sokcuri/hdTrans/compare/v0.4.4...v0.4.5) (2026-10-23)


### Bug Fixes

* **release:** read names, honorifics and table layouts more naturally ([43df625](https://github.com/sokcuri/hdTrans/commit/43df625a6f982eb09c400b879d8ec139d9ae28f9))
* **release:** read names, honorifics and table layouts more naturally ([#37](https://github.com/sokcuri/hdTrans/issues/37)) ([43df625](https://github.com/sokcuri/hdTrans/commit/43df625a6f982eb09c400b879d8ec139d9ae28f9))
* **translation:** read names, words, honorifics and layouts more naturally ([#36](https://github.com/sokcuri/hdTrans/issues/36)) ([80deed4](https://github.com/sokcuri/hdTrans/commit/80deed40ecbe81b7061c780c181b548cb4105e93))
* **web:** list the examples under their published Korean titles ([#31](https://github.com/sokcuri/hdTrans/issues/31)) ([8abd50b](https://github.com/sokcuri/hdTrans/commit/8abd50b1227b9d66f8e58539e3485843c722dbe7))

## [0.4.4](https://github.com/sokcuri/hdTrans/compare/v0.4.3...v0.4.4) (2026-10-22)


### Bug Fixes

* **release:** read dialogue, narration and scenes more naturally ([d47bffb](https://github.com/sokcuri/hdTrans/commit/d47bffb36ef0b5f61b1b820c59a939995c5ed189))
* **translation:** refine senses, grammar, aspect, colloquial speech, names and scenes ([#29](https://github.com/sokcuri/hdTrans/issues/29)) ([4cc0eb5](https://github.com/sokcuri/hdTrans/commit/4cc0eb5ed90f094fdff101e3ad2edf8b5c00d6e7))

## [0.4.3](https://github.com/sokcuri/hdTrans/compare/v0.4.2...v0.4.3) (2026-10-22)


### Bug Fixes

* **release:** make game dialogue read like spoken Korean ([c84cc83](https://github.com/sokcuri/hdTrans/commit/c84cc83d8c40946d599b826d0f097517c8af9371))
* **translation:** read game dialogue endings, contractions and senses the spoken way ([#27](https://github.com/sokcuri/hdTrans/issues/27)) ([f1446b5](https://github.com/sokcuri/hdTrans/commit/f1446b5ea4d3b341a97ca6c498a58e0e11792e3c))

## [0.4.2](https://github.com/sokcuri/hdTrans/compare/v0.4.1...v0.4.2) (2026-10-21)


### Bug Fixes

* **release:** read casual dialogue and Japanese grammar more naturally ([cea0f4a](https://github.com/sokcuri/hdTrans/commit/cea0f4a5cbfb6b55559d4273cd11c573766c4a7b))
* **translation:** read casual speech, imperatives and grammar patterns the Korean way ([#25](https://github.com/sokcuri/hdTrans/issues/25)) ([27c25fa](https://github.com/sokcuri/hdTrans/commit/27c25fae4629ca919d4d755a3862de1df69ef212))

## [0.4.1](https://github.com/sokcuri/hdTrans/compare/v0.4.0...v0.4.1) (2026-10-21)


### Bug Fixes

* **release:** keep the plain speech of quoted lines by default ([4c7bf3b](https://github.com/sokcuri/hdTrans/commit/4c7bf3b7aa7f9acbd4148a90a9257e2a3ee8ac21))
* **speech:** keep the plain -다 of quoted speech by default ([#23](https://github.com/sokcuri/hdTrans/issues/23)) ([8dce347](https://github.com/sokcuri/hdTrans/commit/8dce347b608c1d1887550d8b7aa406fcf3a5e482))

## [0.4.0](https://github.com/sokcuri/hdTrans/compare/v0.3.2...v0.4.0) (2026-10-21)


### Features

* **release:** soften dialogue and keep the spoken style of Japanese text ([2d9f16b](https://github.com/sokcuri/hdTrans/commit/2d9f16b3848c835dcf3f5db5f00c3d12bc27cfa9))
* **speech:** soften spoken lines and read stutters, drawls and spelled-out words ([#21](https://github.com/sokcuri/hdTrans/issues/21)) ([d7fec1b](https://github.com/sokcuri/hdTrans/commit/d7fec1bfb68fb343650edb8bf816892dbe597fc2))

## [0.3.2](https://github.com/sokcuri/hdTrans/compare/v0.3.1...v0.3.2) (2026-10-21)


### Bug Fixes

* **release:** improve Japanese word recognition and Korean grammar ([eba3f26](https://github.com/sokcuri/hdTrans/commit/eba3f2624816ec47317d0930abb01d15a0149e7d))
* **translation:** correct lexical senses, colloquial forms and Korean inflection ([#19](https://github.com/sokcuri/hdTrans/issues/19)) ([2af8df2](https://github.com/sokcuri/hdTrans/commit/2af8df239d15b300f832c33e44acfccb44d2ad71))

## [0.3.1](https://github.com/sokcuri/hdTrans/compare/v0.3.0...v0.3.1) (2026-10-20)


### Bug Fixes

* **cli:** show the engine version in the help header ([5226127](https://github.com/sokcuri/hdTrans/commit/522612701b93606b939cc386301bf2bf51088ba8))
* **cli:** show the engine version in the help header ([#17](https://github.com/sokcuri/hdTrans/issues/17)) ([dd1ebd4](https://github.com/sokcuri/hdTrans/commit/dd1ebd4cdc0b53c54e46a32fbef72ae65fae65f3))

## [0.3.0](https://github.com/sokcuri/hdTrans/compare/v0.2.1...v0.3.0) (2026-10-20)


### Features

* **rules:** carry each local rule in the dictionary row that names it ([68de569](https://github.com/sokcuri/hdTrans/commit/68de569bc2146487639dc75a4c9490ceb2371158))
* **rules:** carry each local rule in the dictionary row that names it ([#15](https://github.com/sokcuri/hdTrans/issues/15)) ([e64f0c1](https://github.com/sokcuri/hdTrans/commit/e64f0c1f28f0c828350503f22932127837e1751d))

## [0.2.1](https://github.com/sokcuri/hdTrans/compare/v0.2.0...v0.2.1) (2026-10-20)


### Bug Fixes

* **release:** finish publication after immutable tag creation ([d999452](https://github.com/sokcuri/hdTrans/commit/d9994524d5823980bf0cd54c49e823465dfbed0d))
* **release:** promote translation quality and JavaScript metadata fixes ([54d3deb](https://github.com/sokcuri/hdTrans/commit/54d3debfa007d1fd6fb4c63db6aa0f00ba82c7ed))
* **release:** publish stable tags through one automatic pipeline ([e8aa575](https://github.com/sokcuri/hdTrans/commit/e8aa575c44d1784b9f333004c4b03ddb60d299f6))
* **release:** skip direct main pushes without promotion PRs ([9c6a676](https://github.com/sokcuri/hdTrans/commit/9c6a676b7014b8e511223eb009ff9705b3b5d3bc))
* **translation:** integrate reviewed translation fixes and benchmarks ([#14](https://github.com/sokcuri/hdTrans/issues/14)) ([4187a08](https://github.com/sokcuri/hdTrans/commit/4187a08c5995336b6db75344003ea5a6e6b27761))
* **web:** add six existing Narou examples ([e35e566](https://github.com/sokcuri/hdTrans/commit/e35e566db37482fb55a140bd69219e50ca8e8bf1))
* **web:** restore example catalog and reject missing payloads ([1a32fa3](https://github.com/sokcuri/hdTrans/commit/1a32fa39919ba97ec0b9d4b7c75379fe78e1c164))

## [0.2.0](https://github.com/sokcuri/hdTrans/compare/v0.1.1...v0.2.0) (2026-10-19)


### Features

* **dict:** improve lexical entries and colloquial endings ([#5](https://github.com/sokcuri/hdTrans/issues/5)) ([85bd376](https://github.com/sokcuri/hdTrans/commit/85bd376e6f5352e7610ee7b370b1fc01bfc7eb5b))
* **release:** publish lexical improvements and compatibility coverage as 0.2.0 ([a18f722](https://github.com/sokcuri/hdTrans/commit/a18f722bd670a78ee0c8873349458f8d745dc7b9))

## 0.1.1 (2026-10-19)


### Features

* integrate TypeScript API and npm nightly packages ([cd9c2cf](https://github.com/sokcuri/hdTrans/commit/cd9c2cfa45dceac7330ca343a63aca111acce045))
* **wasm:** add typed API, npm CLI and Ehnd execution traces ([aca2bbf](https://github.com/sokcuri/hdTrans/commit/aca2bbfdbf02142aa07296a74f82a0a9bc2ae678))


### Bug Fixes

* **build:** pass native Windows paths to the Framework compiler ([05de687](https://github.com/sokcuri/hdTrans/commit/05de687d38fa84acba0a124fb2a1fb30f54561de))
* ezTrans 오역 사례의 어휘와 좁은 문맥 조립 교정 ([6598f79](https://github.com/sokcuri/hdTrans/commit/6598f7975e7a3b76482d57c84f5e798c130082cb))
* **release:** finish standalone distribution and automatic publishing ([348e653](https://github.com/sokcuri/hdTrans/commit/348e6536133e05e051a50734d3c4393ae3ca1de6))
* **release:** publish standalone distributions as 0.1.1 ([be9ffc1](https://github.com/sokcuri/hdTrans/commit/be9ffc1777df0fe10771334f321fc058c346e9da))
* **release:** publish standalone distributions as 0.1.1 ([be9ffc1](https://github.com/sokcuri/hdTrans/commit/be9ffc1777df0fe10771334f321fc058c346e9da))
* **release:** start GitHub releases above the published npm version ([3f3fcf5](https://github.com/sokcuri/hdTrans/commit/3f3fcf59c463a2269c9a4cfaa6bd1b40e305f8a2))
* **rules:** clean unreachable numeric-theme branches and ending pattern ([c9bce18](https://github.com/sokcuri/hdTrans/commit/c9bce187583036abcba42144aa9d3260544f0909))
* **rules:** constrain arithmetic, body and counter senses ([fbf98fd](https://github.com/sokcuri/hdTrans/commit/fbf98fd0b3f45831e270bda5acc2f114fb207e8b))
* **rules:** constrain food, kindness, quoted-name and limit senses ([8357dda](https://github.com/sokcuri/hdTrans/commit/8357ddaf8aae095bfd0b10337e52f1dee185ff08))
* **rules:** correct ending fields and remove inert alternatives ([bd8a5d0](https://github.com/sokcuri/hdTrans/commit/bd8a5d0e6cb24d081c979a914574f78a0d167d73))
* **rules:** correct frequent sense and colloquial negation choices ([e4eddd2](https://github.com/sokcuri/hdTrans/commit/e4eddd2f8a14ad56871fdab1f76aff89924dd2a8))
* **rules:** correct numeric fields, spelling and country counter ([c00e8b9](https://github.com/sokcuri/hdTrans/commit/c00e8b96886109666985b293a601804770daf7c6))
* **rules:** correct particle context checks and remove dead alternatives ([666db37](https://github.com/sokcuri/hdTrans/commit/666db376f55d3379f0222d4450ac3e8dda3f9283))
* **rules:** correct quotation, noun offsets and grammar fields ([ba8bb97](https://github.com/sokcuri/hdTrans/commit/ba8bb978ef57f7ba5a4c067147c251200e2312cc))
* **rules:** correct quoted endings and explicit subject promises ([e3249d8](https://github.com/sokcuri/hdTrans/commit/e3249d8ffaaa4a94f62eb8f91d8e168b7d2500e0))
* **rules:** distinguish discourse cues from referents and places ([5bf3969](https://github.com/sokcuri/hdTrans/commit/5bf3969ebbea4d63dd49dee1ef6a9e13451126a1))
* **rules:** distinguish extra quantities from times and connectors ([2b2caea](https://github.com/sokcuri/hdTrans/commit/2b2caea10a81e859eb9b8d3d18e40eb5e174d7bd))
* **rules:** distinguish home, spatial, deictic and lexical senses ([34c4f1e](https://github.com/sokcuri/hdTrans/commit/34c4f1eb5eb6f42d64a6af9eec2d61326a144f97))
* **rules:** distinguish literal objects, destinations and means ([1dfbd4f](https://github.com/sokcuri/hdTrans/commit/1dfbd4ff56bf4206282dc264d829eb1194197677))
* **rules:** guard invitation and greeting choices by context ([6a469eb](https://github.com/sokcuri/hdTrans/commit/6a469eb98b81fc03dae8c62cc372df3af451eb3c))
* **rules:** keep sightseeing and glance expressions nominal ([863a013](https://github.com/sokcuri/hdTrans/commit/863a013b1a0a0e565827385052b161359c3f8b01))
* **rules:** keep the provider as subject across benefactive objects ([c19c79a](https://github.com/sokcuri/hdTrans/commit/c19c79a1a42063fe56cd10e02546cafe2149994e))
* **rules:** narrow benefactive requests and repair honorific endings ([05a76df](https://github.com/sokcuri/hdTrans/commit/05a76df27fd933ed4b5340a15f68c363732752d6))
* **rules:** preserve concessive links and conditional connectives ([a9881b7](https://github.com/sokcuri/hdTrans/commit/a9881b7a905ff9846163f328d318b2c072152c03))
* **rules:** preserve contrast, subset and upward movement meanings ([84cd4b7](https://github.com/sokcuri/hdTrans/commit/84cd4b7cceada7854ba8f4eaa47a58cd4377b0f2))
* **rules:** preserve inclusive pronouns and relative-time particles ([b68a8d5](https://github.com/sokcuri/hdTrans/commit/b68a8d5ba59cf83bd8cfd43b3e8e2f7b6d484fba))
* **rules:** preserve pronouns, spatial gaps and following actions ([e15eb85](https://github.com/sokcuri/hdTrans/commit/e15eb850e906e922b87625eef560dc2a8549a051))
* **rules:** preserve species-specific flower counters ([523c820](https://github.com/sokcuri/hdTrans/commit/523c82053bef801c251cd1b9d50e777efc85555f))
* **rules:** preserve the noun before auxiliary のみ ([afbad96](https://github.com/sokcuri/hdTrans/commit/afbad96ba21311bb2242b2cceeb7d8a97b8183d1))
* **rules:** remove inert honorific checks and match the gozaru stem ([432d49f](https://github.com/sokcuri/hdTrans/commit/432d49fc50d1aa5beba2bcc71a9d472f2005c127))
* **rules:** repair low-frequency senses and redundant branches ([9abb041](https://github.com/sokcuri/hdTrans/commit/9abb041c30984f092bede548eb8f41e8404c0c40))
* **rules:** repair remaining kana-theme comparisons and dead conditions ([44c7404](https://github.com/sokcuri/hdTrans/commit/44c7404d4efea24549347d6cc4e3c9aabefd9a16))
* **rules:** retain negation, concessives and literal predicate senses ([5ce8e62](https://github.com/sokcuri/hdTrans/commit/5ce8e6213d9943be2177249886b7e4d46b55a00e))
* **rules:** retain transitive and contextual noun senses ([7e3e170](https://github.com/sokcuri/hdTrans/commit/7e3e17061e5a607a953eb73aad1a9b28ddb63ad2))
* **rules:** revert broad emotion sense activation after e2e review ([2cddb6e](https://github.com/sokcuri/hdTrans/commit/2cddb6e7145969e28c6302fd9a36baa94f9c3b6e))
* **rules:** revert jointly regressed quantity and age restrictions ([d538e1a](https://github.com/sokcuri/hdTrans/commit/d538e1ad5425b2013e10cfa80aa743b5e70cdf52))
* **rules:** revert particle changes rejected by both e2e judges ([c7b6409](https://github.com/sokcuri/hdTrans/commit/c7b640985203c777bb09d8073ef374c23ed886a3))
* **rules:** separate particle means, companions and conditions ([e7d9aa1](https://github.com/sokcuri/hdTrans/commit/e7d9aa1f800b1a0fc939c53e46d7de8c18ee0062))


### Reverts

* **rules:** preserve ellipsis-separated あれ interjections ([e2b3c12](https://github.com/sokcuri/hdTrans/commit/e2b3c12e42f0c215b7eab1f713707e74f7e40856))
* **rules:** withdraw benefactive and destination restrictions after e2e review ([511c957](https://github.com/sokcuri/hdTrans/commit/511c95798509ed8b88eedd913fc3a8855dcd1340))
