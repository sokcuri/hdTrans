# Changelog

## [0.9.0](https://github.com/sokcuri/hdTrans/compare/v0.8.4...v0.9.0) (2026-11-05)

### 라이선스 변경

라이선스를 MPL-2.0에서 AGPL-3.0-only로 바꿨습니다. 자세한 내용은 README의 라이선스 절을 참고하세요.


### Features

* **grammar:** end a quoted reason in し with -거든, -거든요 or -잖아, not -고 ([#511](https://github.com/sokcuri/hdTrans/issues/511)) ([9ef7763](https://github.com/sokcuri/hdTrans/commit/9ef77639d35fd78041582e05c339619fa4ec334d))
* **grammar:** read と before the same person's next act as -고는 ([#500](https://github.com/sokcuri/hdTrans/issues/500)) ([34a35c1](https://github.com/sokcuri/hdTrans/commit/34a35c14386044306cb5c42b6404f57207c8a3cd))
* **names:** learn unknown kana names and family names across a document ([#554](https://github.com/sokcuri/hdTrans/issues/554)) ([10bd1b4](https://github.com/sokcuri/hdTrans/commit/10bd1b4951c34ec43a85ccb2ce5cc220037400ef))
* **release:** cut critical mistranslations, learn names across a document and move to AGPL-3.0-only ([b92e49a](https://github.com/sokcuri/hdTrans/commit/b92e49ace4af9a8c1e5012f8b7598d22526c1330))
* 라이선스를 AGPL-3.0-only로 변경 ([#589](https://github.com/sokcuri/hdTrans/issues/589)) ([be52ec0](https://github.com/sokcuri/hdTrans/commit/be52ec04db1a841aee611f1454291ec0eac9b67f))


### Bug Fixes

* **compose:** keep a typed space before a noun after it ([#543](https://github.com/sokcuri/hdTrans/issues/543)) ([b372d2f](https://github.com/sokcuri/hdTrans/commit/b372d2f7f2f8a9a062af56a3c59b5f6dab6cf676))
* **dict:** add a second batch of frequent unknown words from the pools ([#556](https://github.com/sokcuri/hdTrans/issues/556)) ([89661ba](https://github.com/sokcuri/hdTrans/commit/89661ba40da2766600c27caab3567d38791b25cc))
* **dict:** add a second batch of kanji words of fiction read one character at a time ([#577](https://github.com/sokcuri/hdTrans/issues/577)) ([4ed6eaa](https://github.com/sokcuri/hdTrans/commit/4ed6eaa14121afb6bc9212ce17c9c7bc5e94db90))
* **dict:** add frequent unknown words from the subtitle, voice and old-novel pools ([#550](https://github.com/sokcuri/hdTrans/issues/550)) ([58909e4](https://github.com/sokcuri/hdTrans/commit/58909e46c546032de7c60ae109c87d3ee3fd3ead))
* **dict:** add kana spellings from ruby readings of the pools and read them before で ([#567](https://github.com/sokcuri/hdTrans/issues/567)) ([7f5b576](https://github.com/sokcuri/hdTrans/commit/7f5b576d4339360ef2665d2259243fd3a336d929))
* **dict:** add kanji words of fiction that were read one character at a time ([#573](https://github.com/sokcuri/hdTrans/issues/573)) ([54202d1](https://github.com/sokcuri/hdTrans/commit/54202d1cb6b0b2f20f8d86aea140989230c15593))
* **dict:** add missing words, idioms and colloquial forms ([#518](https://github.com/sokcuri/hdTrans/issues/518)) ([ecc7b4f](https://github.com/sokcuri/hdTrans/commit/ecc7b4f8d1c92248d87504c753d44aaf11fe6bba))
* **dict:** add senses with a clean cue and the missing senses of polysemous words ([#521](https://github.com/sokcuri/hdTrans/issues/521)) ([a6dfbbf](https://github.com/sokcuri/hdTrans/commit/a6dfbbfc0f05b27e1fa304853c6903e05f2a16f3))
* **dict:** choose 付き合う, 送る, 手を打つ, いただく, 言いつける and ゆるさない by their context ([#522](https://github.com/sokcuri/hdTrans/issues/522)) ([7d1ba4d](https://github.com/sokcuri/hdTrans/commit/7d1ba4d72b59bf6e244ce903b0ebfa417db3d0c9))
* **dict:** correct wrong glosses and defaults, names rows over common words, and colloquial contractions ([#520](https://github.com/sokcuri/hdTrans/issues/520)) ([2ffd252](https://github.com/sokcuri/hdTrans/commit/2ffd252adc52bf97e3778033548f448d9a1aa360))
* **dict:** drop or fence fused rows that won across the real word boundary ([#540](https://github.com/sokcuri/hdTrans/issues/540)) ([46997f8](https://github.com/sokcuri/hdTrans/commit/46997f8e9b0b73ed8dcf2ed49c49e7f99470c430))
* **dict:** fence more fused rows: まあな, いやな, まずいな, とかで+き, んでしょうか, person にしてもらう ([#542](https://github.com/sokcuri/hdTrans/issues/542)) ([e4f101e](https://github.com/sokcuri/hdTrans/commit/e4f101edb518dcb1cde2da3679dfa1c7972f80a4))
* **dict:** pick the common sense of 一人, 頃, 命, しっかり, もう and 気持ち from the words around them ([#551](https://github.com/sokcuri/hdTrans/issues/551)) ([90454d5](https://github.com/sokcuri/hdTrans/commit/90454d5be292ae63dd5488bd21b951e9ec247eed))
* **dict:** read katakana スキ as 좋아, 仰ぐ as 우러러보다, この手の中 as 이 손 안, and 目下, 本当なら, 全く by their cues ([#592](https://github.com/sokcuri/hdTrans/issues/592)) ([9a9eba7](https://github.com/sokcuri/hdTrans/commit/9a9eba781d3bd1261e258adc0c93bb5fb0c37625))
* **dict:** read negated 感心しない, 心中する and 炎上 by their sense ([#526](https://github.com/sokcuri/hdTrans/issues/526)) ([6013320](https://github.com/sokcuri/hdTrans/commit/60133206adcbffc6aeb620e6eebca9bb2ea70420))
* **dict:** read notice and narration words by their neighbours ([#498](https://github.com/sokcuri/hdTrans/issues/498)) ([046e49d](https://github.com/sokcuri/hdTrans/commit/046e49d7b80a6a27f3b10b3f438c3fbc1c63789d))
* **dict:** read うまい past degree adverbs and a topic comma by its subject ([#510](https://github.com/sokcuri/hdTrans/issues/510)) ([6a1561d](https://github.com/sokcuri/hdTrans/commit/6a1561dcd5acc95a1e9308c52db6cfc6218ac75f))
* **dict:** read かける, 取る, 持つ and よく by their object, 本当だね as 정말, and sentence-final もん as 걸 ([#558](https://github.com/sokcuri/hdTrans/issues/558)) ([b52d167](https://github.com/sokcuri/hdTrans/commit/b52d167e331572a87fcc2b74fc3b0a43dcf8f51f))
* **dict:** read それは違う, も付き合う, 止める, 最後の方, 叩く and 覚える by their common sense ([#553](https://github.com/sokcuri/hdTrans/issues/553)) ([f8d5a24](https://github.com/sokcuri/hdTrans/commit/f8d5a24aaafb8dff4517cf8a943768a3448c08fa))
* **dict:** read そんな気する as 생각이 들다, あと before a number or after a list, and a negated 気付く as 알아차리지 못하다 ([#571](https://github.com/sokcuri/hdTrans/issues/571)) ([ab7daf2](https://github.com/sokcuri/hdTrans/commit/ab7daf2fa61496a382657953a91156dd30d9cc6c))
* **dict:** read って事 after a past copula as -었다는, and drop middle dots from new spec text ([#583](https://github.com/sokcuri/hdTrans/issues/583)) ([6dc8380](https://github.com/sokcuri/hdTrans/commit/6dc838098568976f5151c73cbb8bfed8b3b508ff))
* **dict:** read って事 as (이)라는 것, 何のこと as 무슨 말, a closing ちょっと…… as 잠깐 and いっぱい before a verb as 많이 ([#566](https://github.com/sokcuri/hdTrans/issues/566)) ([8d6a300](https://github.com/sokcuri/hdTrans/commit/8d6a3005bf01fa6ffdf3de50fedb7e34e964b678))
* **dict:** read まったく before a negative as 전혀, a door that 開く as 열리다, の中で a place as 안에서, and 近づこう as 다가가다 ([#580](https://github.com/sokcuri/hdTrans/issues/580)) ([297664f](https://github.com/sokcuri/hdTrans/commit/297664fcf03e43196222ab9971111293fc638b69))
* **dict:** read 元 before a noun as 전, 元は and 元いた as 원래, が元で as 원인, and 元に戻す as 원래대로 ([#591](https://github.com/sokcuri/hdTrans/issues/591)) ([7e4023f](https://github.com/sokcuri/hdTrans/commit/7e4023f2561e05278fd10f2220811ad33bf3c060))
* **dict:** read 大事 as 중요 or 소중 by the word after it and its subject, and 大分 as 오이타 only with place cues ([#587](https://github.com/sokcuri/hdTrans/issues/587)) ([a6f680c](https://github.com/sokcuri/hdTrans/commit/a6f680c0648b03254ad9f8022bfd95ca7c37494c))
* **dict:** read 様方 as 들, space 中 after a person, and read 零点 as 영점 ([#504](https://github.com/sokcuri/hdTrans/issues/504)) ([318e1cf](https://github.com/sokcuri/hdTrans/commit/318e1cfab0a04981b0c11117803707a84b722b31))
* **dict:** read 特に before 없다 as 특별히 and a person's 所へ as 에게; check rule ids for duplicates ([#560](https://github.com/sokcuri/hdTrans/issues/560)) ([83dd16d](https://github.com/sokcuri/hdTrans/commit/83dd16dc85d39886d0ebe8c75e6f4190a06b0b8d))
* **dict:** read 諸方面 as 여러 방면, a causative before ていただきます as humble, and いつ入れる as 넣다 ([#578](https://github.com/sokcuri/hdTrans/issues/578)) ([4c1d400](https://github.com/sokcuri/hdTrans/commit/4c1d4004a86a3f3665125a5cb8c826629ff27734))
* **dict:** read 転がす as 굴리다, and a ら-less potential after an honoured subject as a potential ([#581](https://github.com/sokcuri/hdTrans/issues/581)) ([1aa561c](https://github.com/sokcuri/hdTrans/commit/1aa561cbbd07ff939464bedfbeda34ff86a855a2))
* **dict:** replace a third batch of Sino-reading and wrong-sense glosses ([#525](https://github.com/sokcuri/hdTrans/issues/525)) ([3af4322](https://github.com/sokcuri/hdTrans/commit/3af432231098e273d22f2c3d7fa65bf1973a44ad))
* **dict:** replace Sino non-word glosses, wrong 보다 markers and numeral name rows; read cut-off military times ([#523](https://github.com/sokcuri/hdTrans/issues/523)) ([32c0a02](https://github.com/sokcuri/hdTrans/commit/32c0a0267c0cd5fc68eba0061392936acfa00332))
* **dict:** replace Sino-reading and wrong-sense glosses with the common meaning ([#524](https://github.com/sokcuri/hdTrans/issues/524)) ([6f1f807](https://github.com/sokcuri/hdTrans/commit/6f1f807c9062377ed75681d34e0265aa7081d3c0))
* **dict:** stop kana spellings of kanji words from taking kana that spell other words ([#532](https://github.com/sokcuri/hdTrans/issues/532)) ([205a54a](https://github.com/sokcuri/hdTrans/commit/205a54afaf1a8931385a6af59bbb3dc52c1af261))
* **grammar:** do not count unknown marks such as ？ as nouns in rules ([#516](https://github.com/sokcuri/hdTrans/issues/516)) ([e26dc5d](https://github.com/sokcuri/hdTrans/commit/e26dc5d57e9c7a9086ea88bc0dd5113d6cee2580))
* **grammar:** end past copula and adjective forms before わ, わね, わよね, ぞ and のね ([#552](https://github.com/sokcuri/hdTrans/issues/552)) ([9481620](https://github.com/sokcuri/hdTrans/commit/9481620723a06b8b0fed489e8ae52e2de546e4a7))
* **grammar:** fix or clear the first meaning-flip suspect rows ([#508](https://github.com/sokcuri/hdTrans/issues/508)) ([370dd9b](https://github.com/sokcuri/hdTrans/commit/370dd9b30a19efe94fa702516b75fda5c7795893))
* **grammar:** fix or clear the next meaning-flip suspect rows ([#512](https://github.com/sokcuri/hdTrans/issues/512)) ([f6f83dc](https://github.com/sokcuri/hdTrans/commit/f6f83dc33fee17b0ca9576cf444f108fbe90e3f5))
* **grammar:** keep the meaning of ないと, 言ってるでしょ, ながら+な, じゃなくて and の before abstract nouns ([#495](https://github.com/sokcuri/hdTrans/issues/495)) ([58b3aa6](https://github.com/sokcuri/hdTrans/commit/58b3aa66171b8775a5dac8a64c63b72a6aa7132c))
* **grammar:** keep the voice of fixed passive and benefactive forms; kana-stem ん negatives; で after a medium ([#528](https://github.com/sokcuri/hdTrans/issues/528)) ([379e077](https://github.com/sokcuri/hdTrans/commit/379e07799fc0184dd860d01ca26f7a309b848cd6))
* **grammar:** keep 元気か before どうか or a verb as a question about health, not the greeting 잘 지내 ([#604](https://github.com/sokcuri/hdTrans/issues/604)) ([c970c98](https://github.com/sokcuri/hdTrans/commit/c970c98a11d86674ee070194ecd060f2781b974e))
* **grammar:** let a person who looks go on with -고는, and read only name-like お nouns as persons ([#509](https://github.com/sokcuri/hdTrans/issues/509)) ([e5ac833](https://github.com/sokcuri/hdTrans/commit/e5ac833a33ef543cb6989678ff9051c6eec7d327))
* **grammar:** narrow local rules that read the wrong word in crit lines ([#527](https://github.com/sokcuri/hdTrans/issues/527)) ([94a0f73](https://github.com/sokcuri/hdTrans/commit/94a0f738f0649ffa9f571f2d0fe730eec53d51bf))
* **grammar:** read a final ってば as -다니까/-라니까, and after a closing noun as -도 참 ([#562](https://github.com/sokcuri/hdTrans/issues/562)) ([c813fb2](https://github.com/sokcuri/hdTrans/commit/c813fb2820bad8e13b096fa7daced255645daf31))
* **grammar:** read a kana pronoun before a spaced だって as 도, take only a verb as the quoting verb after たって, and keep 胸高鳴る ([#602](https://github.com/sokcuri/hdTrans/issues/602)) ([eef3c66](https://github.com/sokcuri/hdTrans/commit/eef3c66fe5e5e676677c9a4ea630fa37f6ac1b6a))
* **grammar:** read a notice 方 as 분 and のため by its noun ([#494](https://github.com/sokcuri/hdTrans/issues/494)) ([e6f8bc4](https://github.com/sokcuri/hdTrans/commit/e6f8bc4653df3c08056970ba02f3dd65368fcbea))
* **grammar:** read a song topic's 配信開始, a questioning とでも思う, and the speaker's 困っている ([#514](https://github.com/sokcuri/hdTrans/issues/514)) ([b00f55c](https://github.com/sokcuri/hdTrans/commit/b00f55c0e4cdb0efd30928ef5bf6f26cfe5f3a44))
* **grammar:** read agents, sayings, gradual changes and 同時に after と; read 少しすると as 잠시 후 ([#506](https://github.com/sokcuri/hdTrans/issues/506)) ([83869ec](https://github.com/sokcuri/hdTrans/commit/83869ec7385ea64db952f6aed778ab44ad8118b0))
* **grammar:** read genteel ませんこと, rhetorical あるまい, reported だとよ and old かもしれぬ endings ([#546](https://github.com/sokcuri/hdTrans/issues/546)) ([0069dc5](https://github.com/sokcuri/hdTrans/commit/0069dc5d261fe2ab12b938bed981f249a22a22f2))
* **grammar:** read invitations after a place, そう言ってるでしょ, じゃなくて before a feeling and a kept ていないと ([#499](https://github.com/sokcuri/hdTrans/issues/499)) ([95bedc5](https://github.com/sokcuri/hdTrans/commit/95bedc55dfe4c634b4f73368c88ec4c31b1fc33c))
* **grammar:** read polite states, reached degrees, good results and manners of speaking after と ([#502](https://github.com/sokcuri/hdTrans/issues/502)) ([9d69e97](https://github.com/sokcuri/hdTrans/commit/9d69e979eb5491af5de72fa51cfb860bb03bda1a))
* **grammar:** read quoted たって, concessive ろうと, ませんと, てみせます and offered るよ by their use ([#536](https://github.com/sokcuri/hdTrans/issues/536)) ([11257ad](https://github.com/sokcuri/hdTrans/commit/11257ad2e86d6e1eb05445f21b6cd457ebdaa1d3))
* **grammar:** read the ん negative before だろう and じゃろう as ない ([#515](https://github.com/sokcuri/hdTrans/issues/515)) ([4bdbf46](https://github.com/sokcuri/hdTrans/commit/4bdbf4670325f9eeb3d5b15fd2b4ad21b953bac2))
* **grammar:** read who likes, who can and suffix nouns by their role ([#529](https://github.com/sokcuri/hdTrans/issues/529)) ([688018d](https://github.com/sokcuri/hdTrans/commit/688018d3103758343f0ba41193588e9b0933d926))
* **grammar:** read ㄹ stems before a finding as -니, ている before what breaks in as -는데, and past in だ ([#505](https://github.com/sokcuri/hdTrans/issues/505)) ([55b4b4b](https://github.com/sokcuri/hdTrans/commit/55b4b4b5d036c2e4b97505c90fd114e51bceb21d))
* **grammar:** read ㄹ stems before a finding as -니, ている before what breaks in as -는데, and past in だ ([#505](https://github.com/sokcuri/hdTrans/issues/505)) ([962fb34](https://github.com/sokcuri/hdTrans/commit/962fb3462813ff5ac3409daa7d3b7bfcd2984418))
* **grammar:** read ちゃう/ちまう before かも and いい, and ちまう after i-row stems ([#555](https://github.com/sokcuri/hdTrans/issues/555)) ([7aa75e8](https://github.com/sokcuri/hdTrans/commit/7aa75e87cc6f88c846ae399a48c093e48b2a2f45))
* **grammar:** read でしたっけ after appearance そう and adjectival nouns, and ちゃう before わね, わよね, んだもん ([#575](https://github.com/sokcuri/hdTrans/issues/575)) ([0c77ca3](https://github.com/sokcuri/hdTrans/commit/0c77ca3ae21d61268fd99b73875da95d24af8a0f))
* **grammar:** read と after an act as -자, keep -니 before what was found ([#493](https://github.com/sokcuri/hdTrans/issues/493)) ([913587d](https://github.com/sokcuri/hdTrans/commit/913587d3938101d6ce370ace62630f7a1fa797b4))
* **grammar:** read なんか無い, ても…やること, について before talk and Nでは厳しい by their sense ([#534](https://github.com/sokcuri/hdTrans/issues/534)) ([709c792](https://github.com/sokcuri/hdTrans/commit/709c792deef5163a20043462014f43031618c18b))
* **grammar:** read のために after a cause and 何のため、 by their sense ([#503](https://github.com/sokcuri/hdTrans/issues/503)) ([4e720ad](https://github.com/sokcuri/hdTrans/commit/4e720ad146fda2027b96c5288d32c4dd6eb75124))
* **grammar:** read のではありませぬか as 아니옵니까, 四人前 as 4인분, and an object を後ろ盾に出来れば as (으)로 할 수 있으면 ([#586](https://github.com/sokcuri/hdTrans/issues/586)) ([8efd10b](https://github.com/sokcuri/hdTrans/commit/8efd10bf44f2edfa30cf7d6a72b12aa8c7f5b2ef))
* **grammar:** read もんね after polite forms, もんな and drawn もんなー as 잖아 ([#565](https://github.com/sokcuri/hdTrans/issues/565)) ([729d969](https://github.com/sokcuri/hdTrans/commit/729d9695c2e5909eddf7bdffbdc09889046d7a30))
* **grammar:** read 元気 asked as a greeting or wished at parting as 잘 지내다, and kana 元気だせ as 힘내 ([#593](https://github.com/sokcuri/hdTrans/issues/593)) ([3202f73](https://github.com/sokcuri/hdTrans/commit/3202f73274f7f1b644a08cbd389dad3dda26acd7))
* **katakana:** read native words spelled in katakana for emphasis ([#537](https://github.com/sokcuri/hdTrans/issues/537)) ([dd958ce](https://github.com/sokcuri/hdTrans/commit/dd958ce6609a6165fb71d8e4404004641288b488))
* **katakana:** read whole i-adjectives, っと adverbs and polite endings spelled in katakana ([#548](https://github.com/sokcuri/hdTrans/issues/548)) ([cbd38b1](https://github.com/sokcuri/hdTrans/commit/cbd38b174903f00a2af7bbf0b6af2a87bd9f0288))
* **kin:** choose 오빠/형 and 언니/누나 by a clear sign of the speaker's sex, and read a bare お兄 as 오빠 ([#576](https://github.com/sokcuri/hdTrans/issues/576)) ([52630ff](https://github.com/sokcuri/hdTrans/commit/52630ffd9749ce51b66a04cd5fd46a9fdfa62c0b))
* **names:** drop a second batch of name-layer rows that override common words or write a gloss as a name ([#588](https://github.com/sokcuri/hdTrans/issues/588)) ([679170f](https://github.com/sokcuri/hdTrans/commit/679170f9e6d0d13bb7e3969d2c610535075836cf))
* **names:** drop name-layer rows that override common words or write a gloss as a name ([#582](https://github.com/sokcuri/hdTrans/issues/582)) ([9d85d95](https://github.com/sokcuri/hdTrans/commit/9d85d95249926bd4e33b07026a73ab2ea922b5a2))
* **names:** learn a hiragana name that is also a verb form when the document puts it before は, を or へ ([#570](https://github.com/sokcuri/hdTrans/issues/570)) ([cb00ef5](https://github.com/sokcuri/hdTrans/commit/cb00ef53323b3d77889218132a3e7cc80aa0e002))
* **names:** learn repeated hiragana names whose halves are words, and read unplaced kana after nouns and doubled sounds ([#564](https://github.com/sokcuri/hdTrans/issues/564)) ([ef83dd7](https://github.com/sokcuri/hdTrans/commit/ef83dd7ea31dce0a5340430cb205e89b5fdb870d))
* **names:** read a kanji name row as the person before a verb of saying after は or も, and full names with one- or three-kanji parts ([#579](https://github.com/sokcuri/hdTrans/issues/579)) ([c9e271d](https://github.com/sokcuri/hdTrans/commit/c9e271d112a615e729d42a35c4418fd408d2d4c5))
* **names:** read a word with a person's name row as the name before a predicate of a person ([#519](https://github.com/sokcuri/hdTrans/issues/519)) ([ae8a2fd](https://github.com/sokcuri/hdTrans/commit/ae8a2fdae7ac1dbf9ee327da6c65b2867b2a93f8))
* **names:** read kanji names no dictionary has by name readings of their kanji, and drawn one-kanji nicknames ([#568](https://github.com/sokcuri/hdTrans/issues/568)) ([bcca2c8](https://github.com/sokcuri/hdTrans/commit/bcca2c87afab4a31f640ad94ef507ccbd947b281))
* **names:** read the surname and given name of a full name the document writes as that person alone ([#574](https://github.com/sokcuri/hdTrans/issues/574)) ([8122237](https://github.com/sokcuri/hdTrans/commit/8122237b35adf33c739c74c66c4cf6927d391626))
* **names:** read 成 before an honorific as the name and drop hanja-gloss name rows ([#538](https://github.com/sokcuri/hdTrans/issues/538)) ([b5d36ac](https://github.com/sokcuri/hdTrans/commit/b5d36ac0adc2052baca364e04645e4fe3a389194))
* **names:** write hiragana names and nicknames as names, not kana words ([#531](https://github.com/sokcuri/hdTrans/issues/531)) ([9f10665](https://github.com/sokcuri/hdTrans/commit/9f106654d1b87776b2180ff1448cff95fa30c218))
* **normalize:** read a doubled 々 after two kanji as the word said again ([#584](https://github.com/sokcuri/hdTrans/issues/584)) ([cb4a92d](https://github.com/sokcuri/hdTrans/commit/cb4a92d4285c22003b7f7b233dbebedafb360923))
* **rules:** read なんで after a parting space as 왜 ([#545](https://github.com/sokcuri/hdTrans/issues/545)) ([d115e7d](https://github.com/sokcuri/hdTrans/commit/d115e7ddd8e031b3129ab45e253b5e6d34d73efd))
* **segment:** a particle reading that runs into the next word yields to the particle ([#544](https://github.com/sokcuri/hdTrans/issues/544)) ([eff6bd5](https://github.com/sokcuri/hdTrans/commit/eff6bd54cf6484932c712ed1e9435a08b44c33d2))
* **segment:** end a caption clause at a space before a personal pronoun ([#539](https://github.com/sokcuri/hdTrans/issues/539)) ([8ba9a2e](https://github.com/sokcuri/hdTrans/commit/8ba9a2e42fa4d2ec2c609d87ff7020358bc6a490))
* **segment:** give a kana left by a long reading back to the next word ([#533](https://github.com/sokcuri/hdTrans/issues/533)) ([928c22a](https://github.com/sokcuri/hdTrans/commit/928c22a58a8d16bcf049db2ceb8110087fc6061d))
* **segment:** keep short kana adjectives and the short やだ whole after nouns ([#561](https://github.com/sokcuri/hdTrans/issues/561)) ([40e8b8f](https://github.com/sokcuri/hdTrans/commit/40e8b8ff367637d919559f5f3683fe487e553564))
* **segment:** let an adverb take です, close a sentence at ≫ 》 】, and read a name before やん as a nickname ([#517](https://github.com/sokcuri/hdTrans/issues/517)) ([754d1fc](https://github.com/sokcuri/hdTrans/commit/754d1fc22330a97b8eca829bcb1be80522d524af))
* **segment:** read だって and たって before a space as going on, not as hearsay ([#590](https://github.com/sokcuri/hdTrans/issues/590)) ([1a34fc1](https://github.com/sokcuri/hdTrans/commit/1a34fc18d23099f419c39856e4edebdaa7683d86))
* **sense:** read スキ before に or で as 빈틈, and switch 兄 only on the line's own cue ([#605](https://github.com/sokcuri/hdTrans/issues/605)) ([c2c7930](https://github.com/sokcuri/hdTrans/commit/c2c79300d048ca80d0185bcc337951a546970865))
* **sound:** read a long mark inside a hiragana word as its vowel ([#549](https://github.com/sokcuri/hdTrans/issues/549)) ([6a2dfd7](https://github.com/sokcuri/hdTrans/commit/6a2dfd7f04c6eff6d0cb0da0c64b9fde5448aa29))
* **sound:** read baby talk, cat speech, long marks and hiragana loanwords as the words they spell ([#535](https://github.com/sokcuri/hdTrans/issues/535)) ([da8fbe0](https://github.com/sokcuri/hdTrans/commit/da8fbe02a94a0da53c37bd3cb3cc3e00c3d907ae))
* **sound:** read the ichidan potential said without its ら as られる ([#559](https://github.com/sokcuri/hdTrans/issues/559)) ([956e3a2](https://github.com/sokcuri/hdTrans/commit/956e3a243eedcd81dae8297c941d49ab542aa5d6))
* **sound:** read the Kansai へん after a kana stem of a verb as ない ([#563](https://github.com/sokcuri/hdTrans/issues/563)) ([ec344f5](https://github.com/sokcuri/hdTrans/commit/ec344f512b6efeeb9d3e92ff71835a7fb1534697))
* **sound:** read どうした after a て form and やだよ as phrases, and drop pause commas inside a unit ([#541](https://github.com/sokcuri/hdTrans/issues/541)) ([3db6f99](https://github.com/sokcuri/hdTrans/commit/3db6f9987201f5408b9b36a53f1517fd82a2cac1))
* **speech:** write old and role speech in 하오 and plain forms ([#513](https://github.com/sokcuri/hdTrans/issues/513)) ([b0b8e22](https://github.com/sokcuri/hdTrans/commit/b0b8e2232cd88436d85ef9ababa6917251b6634f))
* **spoken:** read a casual negative ねえ after a longer negative stem as ない ([#572](https://github.com/sokcuri/hdTrans/issues/572)) ([852ade2](https://github.com/sokcuri/hdTrans/commit/852ade2978beaa8b41f972ac14b451dfbae6829a))
* **spoken:** read Kansai やし, Kobe とう, なきゃなんない and かまへんで; rows for おめえ and Kansai set phrases ([#547](https://github.com/sokcuri/hdTrans/issues/547)) ([bee5159](https://github.com/sokcuri/hdTrans/commit/bee51590ef31c1fdee76e73375aa1de4b9aac459))
* **spoken:** read っす after verbs, ている and ます forms as a 해요체 end ([#569](https://github.com/sokcuri/hdTrans/issues/569)) ([66b3745](https://github.com/sokcuri/hdTrans/commit/66b3745bd34c9e5ba31c332e739f690cc1d45df3))
* **spoken:** read ていやがる as ている, the でやがる copula as です, and 承知 by its sense ([#557](https://github.com/sokcuri/hdTrans/issues/557)) ([c6d6219](https://github.com/sokcuri/hdTrans/commit/c6d6219b3b7101811b0c1d7513b519464a6e906d))
* **word:** read the form only one of two verbs on a stem takes; keep verb senses to their own conjugation ([#530](https://github.com/sokcuri/hdTrans/issues/530)) ([5d3416b](https://github.com/sokcuri/hdTrans/commit/5d3416b96f681dcdceb5a4dca6aabb67f3ef45d8))


### Performance Improvements

* **names:** keep the first-rune index of accepted names between lines ([#497](https://github.com/sokcuri/hdTrans/issues/497)) ([72f8a82](https://github.com/sokcuri/hdTrans/commit/72f8a82dff692287b7e65f38eedd4c7db34cedb8))
* **word:** check the source class first in MatchDictionary ([#501](https://github.com/sokcuri/hdTrans/issues/501)) ([3113963](https://github.com/sokcuri/hdTrans/commit/3113963a7bf2ebf4ae999b182fa1d872554501e5))
* **word:** read long runs of marks in linear time again ([#585](https://github.com/sokcuri/hdTrans/issues/585)) ([3848bcf](https://github.com/sokcuri/hdTrans/commit/3848bcf17325379cca3f54e0c52550b7db683fa3))
* **word:** remember atom values while trying construction rows ([#492](https://github.com/sokcuri/hdTrans/issues/492)) ([d61be53](https://github.com/sokcuri/hdTrans/commit/d61be533eb463ec058cc27d10eb3700fbc460faf))
* **word:** try the cheapest operands of construction conditions first ([#496](https://github.com/sokcuri/hdTrans/issues/496)) ([b456683](https://github.com/sokcuri/hdTrans/commit/b4566835b7327003c92dabb09e188b732a3bb2aa))

## [0.8.4](https://github.com/sokcuri/hdTrans/compare/v0.8.3...v0.8.4) (2026-11-04)


### Bug Fixes

* **compose:** keep the comma a word reads with it ([#474](https://github.com/sokcuri/hdTrans/issues/474)) ([c319b40](https://github.com/sokcuri/hdTrans/commit/c319b401580905f90237bddc15c4603953ef7360))
* **dict:** do not read 思う before より, 以上 or 通り as a topic verb after について ([#480](https://github.com/sokcuri/hdTrans/issues/480)) ([c5f5c6f](https://github.com/sokcuri/hdTrans/commit/c5f5c6f717c22083d7571a6b060912c8f6df6d23))
* **dict:** give frequent words their usual default reading ([#473](https://github.com/sokcuri/hdTrans/issues/473)) ([b9f5acd](https://github.com/sokcuri/hdTrans/commit/b9f5acd1027252c39f00bd17e9c596f4da35a6c4))
* **dict:** keep one negative after a してい read 하지 않다 ([#487](https://github.com/sokcuri/hdTrans/issues/487)) ([329ce2a](https://github.com/sokcuri/hdTrans/commit/329ce2a9f127242ea5fffb226544b8110117a9d4))
* **dict:** read critical misreadings of について, 巡る, きる, vocative よ and other constructions ([#462](https://github.com/sokcuri/hdTrans/issues/462)) ([b210727](https://github.com/sokcuri/hdTrans/commit/b2107270b58782a47d880e38aff80e19f2afb8fb))
* **dict:** read misglossed words of spoken lines ([#465](https://github.com/sokcuri/hdTrans/issues/465)) ([d0e29f3](https://github.com/sokcuri/hdTrans/commit/d0e29f398f414702e49c5e7ca3aa6322650cc3b7))
* **dict:** read misread words and set phrases such as 農具, 真っ直ぐ, 俺の中, 目を向ける ([#475](https://github.com/sokcuri/hdTrans/issues/475)) ([e24ef92](https://github.com/sokcuri/hdTrans/commit/e24ef9297c4cc0013d3c52c16a9bf2164290a503))
* **dict:** read the takeaway cry, the fight cheer and bare kana stems at line end ([#469](https://github.com/sokcuri/hdTrans/issues/469)) ([f07f14e](https://github.com/sokcuri/hdTrans/commit/f07f14e34e08323d91fdf1936d2cc8177d9d6c8c))
* **dict:** read the words and clause links the web novel examples misread ([#476](https://github.com/sokcuri/hdTrans/issues/476)) ([3f66a9d](https://github.com/sokcuri/hdTrans/commit/3f66a9d7ee3d0b74bdd73892dd32fe931e9415d0))
* **dict:** read the words and clause shapes the web examples misread ([#484](https://github.com/sokcuri/hdTrans/issues/484)) ([b48f70d](https://github.com/sokcuri/hdTrans/commit/b48f70d2e63aaf4e8520d1f8909322d7b91a8b5b))
* **dict:** read the words and constructions fresh news, novel and game lines misread ([#467](https://github.com/sokcuri/hdTrans/issues/467)) ([0938550](https://github.com/sokcuri/hdTrans/commit/09385505aa4cf02a737d47c3a38ca6b1d3e7ae94))
* **dict:** read the words and particles the narration examples misread ([#479](https://github.com/sokcuri/hdTrans/issues/479)) ([029af46](https://github.com/sokcuri/hdTrans/commit/029af4687df9e67615317338909762036bdaa07a))
* **dict:** read the words and senses the web examples misread ([#482](https://github.com/sokcuri/hdTrans/issues/482)) ([aaec244](https://github.com/sokcuri/hdTrans/commit/aaec2440e37c4228f2f94148d484e402f658e8c4))
* **dict:** read 事 after a verb as こと and fix particle, degree and quoting misreadings ([#472](https://github.com/sokcuri/hdTrans/issues/472)) ([26da037](https://github.com/sokcuri/hdTrans/commit/26da037b441c539ff4ac03a4d4c4cc7ed590a682))
* **dict:** spell katakana loanwords and foreign names as Korean readers write them ([#463](https://github.com/sokcuri/hdTrans/issues/463)) ([3bce04d](https://github.com/sokcuri/hdTrans/commit/3bce04d7db711ee312bb002330c3432680582193))
* **dict:** write つ as 츠 and later plain stops aspirated in every dictionary row ([#481](https://github.com/sokcuri/hdTrans/issues/481)) ([1a9ea43](https://github.com/sokcuri/hdTrans/commit/1a9ea43000b66de4b3dd6a32b4300c2716fb9054))
* **grammar:** read the quotes, endings and set forms the web examples broke ([#483](https://github.com/sokcuri/hdTrans/issues/483)) ([0bca491](https://github.com/sokcuri/hdTrans/commit/0bca491e10d5fa1cdf9f497f4bce193dc384a4fb))
* **grammar:** read じゃなく, 駄目か, ちゃう？, なんか, するに and the の before phrase verbs ([#477](https://github.com/sokcuri/hdTrans/issues/477)) ([30b8c50](https://github.com/sokcuri/hdTrans/commit/30b8c502a9498317d51a9137acd999ec8eda9182))
* **output:** keep the comma an ichidan or passive stem takes as its te ([#471](https://github.com/sokcuri/hdTrans/issues/471)) ([ccd08b7](https://github.com/sokcuri/hdTrans/commit/ccd08b748a2146f3285de3a6597a0769bde7ae60))
* **output:** put the adverb of a compound verb before the verb of a multi-word target ([#470](https://github.com/sokcuri/hdTrans/issues/470)) ([57bf417](https://github.com/sokcuri/hdTrans/commit/57bf417ef5a36482d236448b29544971b7082672))
* **release:** read the web examples, loanwords and Japanese readings more naturally ([a9e8b43](https://github.com/sokcuri/hdTrans/commit/a9e8b43045ab6e63f32a564869eb29e6e935b8eb))
* **rules:** end spoken sentences at phrase spaces and bare ないと ([#466](https://github.com/sokcuri/hdTrans/issues/466)) ([d4b2a25](https://github.com/sokcuri/hdTrans/commit/d4b2a2598e0a9f249ffaa476653efeebc0cc7806))
* **rules:** keep くれよ in mid-sentence and read よほど…ない限り as 있지 않으면 ([#486](https://github.com/sokcuri/hdTrans/issues/486)) ([6bbc19f](https://github.com/sokcuri/hdTrans/commit/6bbc19fd08d138717c5b6b7e9686e95be93a5d50))
* **rules:** read one-word negatives and dialect んと before a bare ないと as obligation ([#478](https://github.com/sokcuri/hdTrans/issues/478)) ([0eb04f8](https://github.com/sokcuri/hdTrans/commit/0eb04f87b349f204f4d33f9c9860a0ded477de07))
* **rules:** read report phrases in news lines ([#464](https://github.com/sokcuri/hdTrans/issues/464)) ([c07dd37](https://github.com/sokcuri/hdTrans/commit/c07dd37663a3724b9c8ab88f80f9c433c62e3a3f))
* **sound:** drop the みー sound row ([#488](https://github.com/sokcuri/hdTrans/issues/488)) ([97b75f1](https://github.com/sokcuri/hdTrans/commit/97b75f1132b488418a1bab55ff546aac62fd2982))
* **sound:** open an utterance after an interjection and its comma ([#485](https://github.com/sokcuri/hdTrans/issues/485)) ([6a04aab](https://github.com/sokcuri/hdTrans/commit/6a04aabc79431cec0b26bdfb98bf6b6d7774b3c1))
* **sound:** read drawls after kanji, spaced stutters, clipped volitionals and drawn objects ([#468](https://github.com/sokcuri/hdTrans/issues/468)) ([9a5aab9](https://github.com/sokcuri/hdTrans/commit/9a5aab98e7dfc9e44f30d060638bd5196e10d2c6))

## [0.8.3](https://github.com/sokcuri/hdTrans/compare/v0.8.2...v0.8.3) (2026-11-03)


### Bug Fixes

* **dict:** narrow construction rows the release review flagged and keep the drawn ええ of adjectives ([#459](https://github.com/sokcuri/hdTrans/issues/459)) ([b4239ae](https://github.com/sokcuri/hdTrans/commit/b4239ae984c0bb7a0a95e4ccaf1428630c96b79f))
* **dict:** read adjective そうな as -어 보이는 and the words the fantasy example and three issues misread ([#455](https://github.com/sokcuri/hdTrans/issues/455)) ([b5e42c4](https://github.com/sokcuri/hdTrans/commit/b5e42c4e1ebe092b7bbb1b093556db376e326035))
* **dict:** read novel lines the web examples misread: 降りしきる, 差し出す, 筈, と分かる and more ([#442](https://github.com/sokcuri/hdTrans/issues/442)) ([2fcc66d](https://github.com/sokcuri/hdTrans/commit/2fcc66d5e0568724e720317c001c145fe0f60ff4))
* **dict:** read the words the dahliya and monologue examples misread ([#456](https://github.com/sokcuri/hdTrans/issues/456)) ([c472947](https://github.com/sokcuri/hdTrans/commit/c47294777e51fbf924ef4fa1edc74feefc2f7fdd))
* **dict:** read the words the farming, dialogue and game-voice examples misread ([#458](https://github.com/sokcuri/hdTrans/issues/458)) ([52757bc](https://github.com/sokcuri/hdTrans/commit/52757bc22141699eb9fd27c755a9da5c47c97d77))
* **dict:** read the words the hamefura and deathmarch examples misread ([#453](https://github.com/sokcuri/hdTrans/issues/453)) ([e7060ad](https://github.com/sokcuri/hdTrans/commit/e7060ad28e4ee32ba436bcbbdfd0e25f622eda29))
* **dict:** read the words the koushaku, loop7 and tensura examples misread ([#452](https://github.com/sokcuri/hdTrans/issues/452)) ([5ba3e32](https://github.com/sokcuri/hdTrans/commit/5ba3e32ff9785d98cbd579b4d8200e35314c9f5f))
* **dict:** read the words the monologue and alya examples misread ([#457](https://github.com/sokcuri/hdTrans/issues/457)) ([f5f6720](https://github.com/sokcuri/hdTrans/commit/f5f6720b1f78e0618246c0f5434e5d722dd251c1))
* **dict:** read くべる, 窪み, 募る, 負う, kana どの and 器が大きい by their sense ([#427](https://github.com/sokcuri/hdTrans/issues/427)) ([73f0705](https://github.com/sokcuri/hdTrans/commit/73f0705bd0071bafe8719294dbcd8606032a55e6))
* **dict:** write つ as 츠 and later plain stops aspirated in Japanese readings ([#440](https://github.com/sokcuri/hdTrans/issues/440)) ([d40184c](https://github.com/sokcuri/hdTrans/commit/d40184c1a80895bc0a55f37a79a0091d14701c67))
* **grammar:** drop the stiff 의 before place nouns, after materials and after state mimetics ([#430](https://github.com/sokcuri/hdTrans/issues/430)) ([73fb4de](https://github.com/sokcuri/hdTrans/commit/73fb4dea135aa9c51b48f4c7c8cf2691b2055996))
* **grammar:** quote the copula with 라고, read なんじゃと思う, Nとしてしまう and na-adjective といったところだ ([#432](https://github.com/sokcuri/hdTrans/issues/432)) ([e394649](https://github.com/sokcuri/hdTrans/commit/e394649eabc366792361a6f02202e3080a1a42d8))
* **grammar:** read a keigo invitation ませんか as 합쇼 with -시- ([#450](https://github.com/sokcuri/hdTrans/issues/450)) ([b4bce78](https://github.com/sokcuri/hdTrans/commit/b4bce781ddbaf61a157cab6e2e4720e181ae93c2))
* **grammar:** read a person's に before 分かる and できる as the subject, and 誰にも before a denial as 아무도 ([#446](https://github.com/sokcuri/hdTrans/issues/446)) ([015828e](https://github.com/sokcuri/hdTrans/commit/015828e126b063edc26a38ec30461102d0e36710))
* **grammar:** read imperative ってば, なよっ, なんと before saying, Vはした and passive かねない ([#428](https://github.com/sokcuri/hdTrans/issues/428)) ([71593f7](https://github.com/sokcuri/hdTrans/commit/71593f758ec7daeac96b319871583b9f7ffb6c68))
* **grammar:** read permission ていい and なくていい as 되다, relief なくてよかった as 다행이다 ([#435](https://github.com/sokcuri/hdTrans/issues/435)) ([81a7e18](https://github.com/sokcuri/hdTrans/commit/81a7e18dcb568980f30279c14ea3f0ae1f17191d))
* **grammar:** read the Kansai negative before で, がな and でも, なんや, ええ, よな after an imperative and さ after a pronoun ([#441](https://github.com/sokcuri/hdTrans/issues/441)) ([9e43507](https://github.com/sokcuri/hdTrans/commit/9e43507f9777058c1185d3509e2b90bae8879818))
* **grammar:** read the lines the saint, rebuild and koushaku examples misread ([#451](https://github.com/sokcuri/hdTrans/issues/451)) ([78bc6d4](https://github.com/sokcuri/hdTrans/commit/78bc6d4e7267dda34f9730007350be1953def8b2))
* **grammar:** read the negative after 必ずしも as partial negation ([#443](https://github.com/sokcuri/hdTrans/issues/443)) ([28fa7f9](https://github.com/sokcuri/hdTrans/commit/28fa7f96a2fb6917c922db0fa2b0e6e87423429c))
* **grammar:** read the old negative せん, せぬ and へん after a verb or a noun of an act as a negative ([#436](https://github.com/sokcuri/hdTrans/issues/436)) ([13b7a38](https://github.com/sokcuri/hdTrans/commit/13b7a387fdab94c9bbd84c765d567980c1e15c06))
* **grammar:** read あまり before a negative as 별로 and the words the bookworm and kusuriya examples misread ([#454](https://github.com/sokcuri/hdTrans/issues/454)) ([a130b4e](https://github.com/sokcuri/hdTrans/commit/a130b4e0c8ab29903ff603d9f3c68ce7a3abfbea))
* **grammar:** read から after a thing as 에서 before taking-out and seeing verbs, and after a time as 부터 ([#433](https://github.com/sokcuri/hdTrans/issues/433)) ([ac3c2cd](https://github.com/sokcuri/hdTrans/commit/ac3c2cdb5996f662407318fd503546b526ce1d5c))
* **grammar:** read しまう after an adverb as 넣다, weather 晴れている as 맑다 and the godan causative-passive ([#439](https://github.com/sokcuri/hdTrans/issues/439)) ([560bf43](https://github.com/sokcuri/hdTrans/commit/560bf431e5a35e3dbafffbd40bb753592d4fd02c))
* **grammar:** read で after a counted group of people as 이서, and と after a count before a noun as 와/과 ([#448](https://github.com/sokcuri/hdTrans/issues/448)) ([0f238a5](https://github.com/sokcuri/hdTrans/commit/0f238a50dec965382131a9899bb9cabe97476d5d))
* **grammar:** read なる after a na-adjective as -어지다 and its ならない as the negative of なる ([#438](https://github.com/sokcuri/hdTrans/issues/438)) ([444c5e6](https://github.com/sokcuri/hdTrans/commit/444c5e699ac12eb99bae199e8573584771b4b309))
* **grammar:** read 気づかれる as 들키다 and the て form of a forced causative-passive as -어서 ([#449](https://github.com/sokcuri/hdTrans/issues/449)) ([37ee95a](https://github.com/sokcuri/hdTrans/commit/37ee95a285702535bd9ec342b1cb6f44573b42a9))
* **number:** count with Korean units, not 체 or word readings ([#434](https://github.com/sokcuri/hdTrans/issues/434)) ([402fb87](https://github.com/sokcuri/hdTrans/commit/402fb87695f6ccb40fdf927cc27734a84b15e39d))
* **number:** count 体 after a kanji numeral natively, keep の before a noun after a count, read もう一日 as 하루 더 ([#431](https://github.com/sokcuri/hdTrans/issues/431)) ([493ac25](https://github.com/sokcuri/hdTrans/commit/493ac25c5b313f4f2d9a15d2c81eb72ff0f9494e))
* **release:** read the words the web novel examples misread and the Kansai, keigo and count forms ([ef21560](https://github.com/sokcuri/hdTrans/commit/ef21560c0e3d063a966b1c52c05adbfe354a7d00))
* **segment:** keep kana words whole after a shorter word, counters before くらい and mimetics before する ([#429](https://github.com/sokcuri/hdTrans/issues/429)) ([4ada20f](https://github.com/sokcuri/hdTrans/commit/4ada20f5f6010d09cb8738f9b541c1edaa47c66b))

## [0.8.2](https://github.com/sokcuri/hdTrans/compare/v0.8.1...v0.8.2) (2026-11-02)


### Bug Fixes

* **dict:** read misread novel words, counters and kana verb splits by their sense ([#424](https://github.com/sokcuri/hdTrans/issues/424)) ([8b80499](https://github.com/sokcuri/hdTrans/commit/8b80499c37fa4e26f1c17906899aa4956235d2b4))
* **grammar:** read である after a nominalizer as the copula ([#422](https://github.com/sokcuri/hdTrans/issues/422)) ([36b9e9b](https://github.com/sokcuri/hdTrans/commit/36b9e9b1d9e1ec345355866b49ba8d73abb287bc))
* **grammar:** read と before laughing, saying or nodding as a quotation ([#423](https://github.com/sokcuri/hdTrans/issues/423)) ([feb6aae](https://github.com/sokcuri/hdTrans/commit/feb6aae7078f0b050dc98a623569ea5c4b27246c))
* **release:** read である as the copula, quote laughs and thoughts with と, and fix counters and kana verb splits ([d2c3296](https://github.com/sokcuri/hdTrans/commit/d2c3296e1181fce8f043394fdfb2402d0c41286f))

## [0.8.1](https://github.com/sokcuri/hdTrans/compare/v0.8.0...v0.8.1) (2026-11-01)


### Bug Fixes

* **compose:** read a noun whose Korean is a verb made a noun as that verb before する ([#417](https://github.com/sokcuri/hdTrans/issues/417)) ([b48b12f](https://github.com/sokcuri/hdTrans/commit/b48b12fc58a72f36dcaaa98623ee4586630e64c7))
* **dict:** choose senses by a same-clause noun and correct misread glosses ([#404](https://github.com/sokcuri/hdTrans/issues/404)) ([ff4bcd7](https://github.com/sokcuri/hdTrans/commit/ff4bcd716683eab28f59f6864087b1b7d280391d))
* **dict:** choose verb senses by their argument and correct everyday glosses ([#403](https://github.com/sokcuri/hdTrans/issues/403)) ([26401f2](https://github.com/sokcuri/hdTrans/commit/26401f2234ddcbd7a499a3a6b0427288e419fb28))
* **dict:** narrow local rules that ignored their neighbour ([#405](https://github.com/sokcuri/hdTrans/issues/405)) ([c0e3f8e](https://github.com/sokcuri/hdTrans/commit/c0e3f8e063477db409c93b941eb40fe42b5bbb7e))
* **dict:** read はだか, はみ出る, に仕える, 今ほど and other misglossed words by their sense ([#410](https://github.com/sokcuri/hdTrans/issues/410)) ([1bb7eb1](https://github.com/sokcuri/hdTrans/commit/1bb7eb1e40c20071727967e8bbc67ad71c8b4070))
* **dict:** read ポート as 포트, モメる as 다투다 and fix misspelled loanword glosses ([#409](https://github.com/sokcuri/hdTrans/issues/409)) ([8eaeda7](https://github.com/sokcuri/hdTrans/commit/8eaeda785201049f49ad03268f4151442562177f))
* **dict:** read 丸見えだ as 훤히 들여다보인다, 誰しもが as 누구나 and 捩じ込む as 쑤셔 넣다 ([#412](https://github.com/sokcuri/hdTrans/issues/412)) ([0baec83](https://github.com/sokcuri/hdTrans/commit/0baec83dab6a4b31cbbcf446f896df89371cf04b))
* **dict:** read 名をはせる, 無碍に, 何泊 and 前が見える by their sense ([#414](https://github.com/sokcuri/hdTrans/issues/414)) ([46f708c](https://github.com/sokcuri/hdTrans/commit/46f708c62b6e25ed5339299133999fdbd9048be9))
* **dict:** read 打つ with a sword as 벼리다 and 辛い with smells and tastes as 맵다 ([#413](https://github.com/sokcuri/hdTrans/issues/413)) ([d2c2af8](https://github.com/sokcuri/hdTrans/commit/d2c2af818e81183b13f83e59ee2ce61fc3e70090))
* **dict:** read 訳が分からない as 영문을 모르다 and 訳も分からず as 영문도 모른 채 ([#411](https://github.com/sokcuri/hdTrans/issues/411)) ([95c7363](https://github.com/sokcuri/hdTrans/commit/95c7363f926e0bdfbf853461bc6e3c0f8844b506))
* **dict:** spell misspelled loanword glosses and read misglossed 一 words by their sense ([#408](https://github.com/sokcuri/hdTrans/issues/408)) ([86f9963](https://github.com/sokcuri/hdTrans/commit/86f9963347430656f70749e1603abc1b7dc302b5))
* **grammar:** read それでいい, これでいい, ここでいい as 그거면, 이거면, 여기면 되다 ([#402](https://github.com/sokcuri/hdTrans/issues/402)) ([5a8871e](https://github.com/sokcuri/hdTrans/commit/5a8871e2873f0969ec1ea3772ba53ab8ddcaa91d))
* **grammar:** って contractions, concessive たって and embedded questions ([#406](https://github.com/sokcuri/hdTrans/issues/406)) ([1f2046a](https://github.com/sokcuri/hdTrans/commit/1f2046aebe31095f199f60fccbd2389293d0c822))
* **packs:** keep the Korean verb a voice row writes against sense rows ([#418](https://github.com/sokcuri/hdTrans/issues/418)) ([48697be](https://github.com/sokcuri/hdTrans/commit/48697be2ec8ba9e73305f93f954d6ee23c5e51d1))
* **release:** read それでいい as 그거면 돼, spoken って and Kansai forms, and verb senses by their context ([57cbe6a](https://github.com/sokcuri/hdTrans/commit/57cbe6a3c18aba26283ee2d2eb4695c973554712))
* **rewrites:** read a mimetic or adverb with した before a noun as its adjective ([#416](https://github.com/sokcuri/hdTrans/issues/416)) ([350a0f9](https://github.com/sokcuri/hdTrans/commit/350a0f9bb53a60ee38885f233ced0d534bedf014))
* round 2 classes (Kansai negatives, misread words, しか segmentation) ([#407](https://github.com/sokcuri/hdTrans/issues/407)) ([2ff1295](https://github.com/sokcuri/hdTrans/commit/2ff12952f177483005239b78e7c574bbbd8fc458))
* round 3 classes (next-word kana, emphatic spellings, garbled words, spoken contractions) ([#415](https://github.com/sokcuri/hdTrans/issues/415)) ([c8c90ea](https://github.com/sokcuri/hdTrans/commit/c8c90ea8e70674b74ea50663be2435076b6d608e))

## [0.8.0](https://github.com/sokcuri/hdTrans/compare/v0.7.6...v0.8.0) (2026-11-01)


### Features

* **dict:** read lexical collocations by the word they pair with ([#323](https://github.com/sokcuri/hdTrans/issues/323)) ([d95b466](https://github.com/sokcuri/hdTrans/commit/d95b466df4c4c3673071c23fa9730d1c20d13942))
* **j2k:** read Anemone's AneDic.txt as a user dictionary inside Anemone ([#399](https://github.com/sokcuri/hdTrans/issues/399)) ([1aeeaac](https://github.com/sokcuri/hdTrans/commit/1aeeaac448a7fa1e531ca426ea348379dd806535))
* **names:** a name list for one document ([#327](https://github.com/sokcuri/hdTrans/issues/327)) ([9c5bd55](https://github.com/sokcuri/hdTrans/commit/9c5bd55413682636568375cab36e8c31656ac037))
* **names:** read name lists everywhere: user folders, J2KEngine, npm and web ([#398](https://github.com/sokcuri/hdTrans/issues/398)) ([1821d62](https://github.com/sokcuri/hdTrans/commit/1821d622b10753c23e3340194c6174bf6cd699a7))
* **packs:** a voice row writes the particle of its agent phrase ([#337](https://github.com/sokcuri/hdTrans/issues/337)) ([25f7693](https://github.com/sokcuri/hdTrans/commit/25f7693ac3d58fd9c6697d0fe82ac0882b58062e))
* **packs:** apply colloc rows over the words of a pattern ([#316](https://github.com/sokcuri/hdTrans/issues/316)) ([bfcac92](https://github.com/sokcuri/hdTrans/commit/bfcac921612d3b1ebd8fc7db48dd0c624e387f39))
* **packs:** choose verb senses by the class of their arguments ([#330](https://github.com/sokcuri/hdTrans/issues/330)) ([56e9922](https://github.com/sokcuri/hdTrans/commit/56e99227a9be3c8d9cfb2fa65ebd0a1d8810fea7))
* **packs:** override one decision slot of a word from pack rows ([#315](https://github.com/sokcuri/hdTrans/issues/315)) ([5997d46](https://github.com/sokcuri/hdTrans/commit/5997d46ba77b4026c0044d6c5c9932059e1f6d23))
* **packs:** read attachment annotations from a provider ([#338](https://github.com/sokcuri/hdTrans/issues/338)) ([9ed5c76](https://github.com/sokcuri/hdTrans/commit/9ed5c766a5d74de4e4f9613f29c36e3b0a7fa90c))
* **packs:** read collocations and idioms as one phrase ([#347](https://github.com/sokcuri/hdTrans/issues/347)) ([67797cf](https://github.com/sokcuri/hdTrans/commit/67797cfe64438ccf99a14ef5df2a4f45e2a607aa))
* **register:** set the speech level of dialogue lines ([#320](https://github.com/sokcuri/hdTrans/issues/320)) ([5fdf72a](https://github.com/sokcuri/hdTrans/commit/5fdf72ad235a484162b02776678950a09e6d1af4))
* **register:** speech_levels auto|keep and a report of the changed lines ([#325](https://github.com/sokcuri/hdTrans/issues/325)) ([5f98060](https://github.com/sokcuri/hdTrans/commit/5f980606872fd249b71ac1f5d6e04fc417978d69))
* **release:** set the speech level of dialogue, read construction chains and take a document name list ([a13cfe7](https://github.com/sokcuri/hdTrans/commit/a13cfe79691fc2dff1eb57c5a6245c4bcbf8123e))


### Bug Fixes

* **cli:** speech-levels in any case, clean report rows and an environment error of its own ([#328](https://github.com/sokcuri/hdTrans/issues/328)) ([968caef](https://github.com/sokcuri/hdTrans/commit/968caefce1de6dc6db9ff9ecb1e742d2d5937d9c))
* **compose:** write an adverb read from a noun apart from the noun after it ([#356](https://github.com/sokcuri/hdTrans/issues/356)) ([d39b184](https://github.com/sokcuri/hdTrans/commit/d39b184c9368e7e881707fa2db177daa679e6141))
* **cx:** read clause chains — ことになる after adjectives, というのに, なのですか, そうみたい, もんじゃない and ようとも ([#392](https://github.com/sokcuri/hdTrans/issues/392)) ([c821378](https://github.com/sokcuri/hdTrans/commit/c8213782f70dddcfe9b2acada958780ffe4b670b))
* **cx:** read clause chains — だろうに, final だろうが, はず + のに, てるかも, ってだけ and seven more ([#357](https://github.com/sokcuri/hdTrans/issues/357)) ([081ca29](https://github.com/sokcuri/hdTrans/commit/081ca29601f25713e3de1fcb8c37ce498a8bcad8))
* **cx:** read clause chains — といったところ, と同時に, だけ無駄, にもなる, ほどでもない and eight more ([#381](https://github.com/sokcuri/hdTrans/issues/381)) ([f7fc8af](https://github.com/sokcuri/hdTrans/commit/f7fc8af5be568568a5bde60f10557934c548ab85))
* **cx:** read clause links and fixed constructions from the chain ([#339](https://github.com/sokcuri/hdTrans/issues/339)) ([e681eea](https://github.com/sokcuri/hdTrans/commit/e681eea59c0a41ceb279c426a27534cfbad74a59))
* **cx:** read ending and auxiliary chains — purpose ため, だろうけど, ってのは, ようと, もんね, てはダメ ([#332](https://github.com/sokcuri/hdTrans/issues/332)) ([2a33e1e](https://github.com/sokcuri/hdTrans/commit/2a33e1e53008804216b1a48952bf56906483ada1))
* **cx:** read ending and modality chains — ところだった, かのよう, そうになる, つもり, としか and five more ([#340](https://github.com/sokcuri/hdTrans/issues/340)) ([37abecd](https://github.com/sokcuri/hdTrans/commit/37abecd69ea2a3a5afa4dc787139070f9562e98a))
* **cx:** read local constructions from the chain — にしろ, かと思うと, つつある, ばよかった, くらいなら and seven more ([#343](https://github.com/sokcuri/hdTrans/issues/343)) ([6ce8f20](https://github.com/sokcuri/hdTrans/commit/6ce8f2040ee788ee34b5746b3a2ba0b22e9dda73))
* **cx:** read plain んだが as -는데 and よろしくない as 좋지 않다 ([#360](https://github.com/sokcuri/hdTrans/issues/360)) ([3a371e2](https://github.com/sokcuri/hdTrans/commit/3a371e2a376b74b51971ddedd37fbd46fab7137e))
* **cx:** read ten clause chains — たばかり, ことか, べきか, ことに, こそが, てさえ and four more ([#345](https://github.com/sokcuri/hdTrans/issues/345)) ([ffa7a78](https://github.com/sokcuri/hdTrans/commit/ffa7a781b2001f819fbb004c6c4976a04d1dfc1c))
* **cx:** read しかけた, ようともがく, ならいい, lone なんだろう, にしちゃ and 気が付いたら past the splits that broke them ([#394](https://github.com/sokcuri/hdTrans/issues/394)) ([254d3cb](https://github.com/sokcuri/hdTrans/commit/254d3cb2da9f7d7af8aa1c047d89085aaed721a0))
* **cx:** read 一緒 as 마찬가지, その先 as 그 뒤, adjective+ところ as 면, すごい as 엄청 and two more by context ([#368](https://github.com/sokcuri/hdTrans/issues/368)) ([8d2b255](https://github.com/sokcuri/hdTrans/commit/8d2b255075aae4c4b5b83caf37bc3840222e898a))
* **cx:** read 離れたところで as the place, not the concessive 봤자 ([#395](https://github.com/sokcuri/hdTrans/issues/395)) ([0df5833](https://github.com/sokcuri/hdTrans/commit/0df58331cc686f6affe0bc3386d8a16dba938f1a))
* **dict:** drop catalogue numbers that name rows wrote before the name ([#367](https://github.com/sokcuri/hdTrans/issues/367)) ([e78b7a4](https://github.com/sokcuri/hdTrans/commit/e78b7a4b997ff2001eaa1dba66c0e9c7bdc4d753))
* **dict:** read falling compound verbs and voice, drink, scolding and battle senses as phrases ([#369](https://github.com/sokcuri/hdTrans/issues/369)) ([37d9407](https://github.com/sokcuri/hdTrans/commit/37d94075ced00659ae02c7e935109f9a8aa51b8f))
* **dict:** read mined verb and adjective faults at their rows, pairs and auxiliaries ([#344](https://github.com/sokcuri/hdTrans/issues/344)) ([5dbb5d4](https://github.com/sokcuri/hdTrans/commit/5dbb5d4fef1842ab964d84df459cce219ccf07c9))
* **dict:** read missing everyday words and kana かく by their sense ([#361](https://github.com/sokcuri/hdTrans/issues/361)) ([ce18de0](https://github.com/sokcuri/hdTrans/commit/ce18de017bd1e1892b0ebbe60fe32735b1476c0a))
* **dict:** read passives with a Korean passive verb without a doubled 어지다 ([#362](https://github.com/sokcuri/hdTrans/issues/362)) ([724928b](https://github.com/sokcuri/hdTrans/commit/724928b1d2dc0e3c5b181cd725bbc5400b769a26))
* **dict:** read set phrases, compound verbs and wrong default glosses as their Korean ([#321](https://github.com/sokcuri/hdTrans/issues/321)) ([199fe81](https://github.com/sokcuri/hdTrans/commit/199fe817f6deb6cbd1ad52cf8f373e03a5b4ec85))
* **dict:** read Sino-Korean readings, wrong senses and name-read words as the Korean words they are ([#378](https://github.com/sokcuri/hdTrans/issues/378)) ([c6d32b5](https://github.com/sokcuri/hdTrans/commit/c6d32b50b82ccec1e38c9ee19a819f5c91c7cfcc))
* **dict:** read ten collocations as one phrase — 行動にでる, 元気でやる, 縁あって and more ([#351](https://github.com/sokcuri/hdTrans/issues/351)) ([6fdf6e5](https://github.com/sokcuri/hdTrans/commit/6fdf6e52e03ccca19f06a5501885f29f60eed399))
* **dict:** read verbs and adjectives by their pair, construction and place, and a harming てやる as a threat ([#336](https://github.com/sokcuri/hdTrans/issues/336)) ([eec7fbe](https://github.com/sokcuri/hdTrans/commit/eec7fbe152bc65e11e9eaae2685ba84b3b13a26d))
* **dict:** read wrongly glossed slang and variant words by their sense ([#374](https://github.com/sokcuri/hdTrans/issues/374)) ([67baf47](https://github.com/sokcuri/hdTrans/commit/67baf47207246dd385fbe1aa71039e2d2bc9a0a4))
* **dict:** read コンバート as 컨버트, おざなり as 무성의하다, おたから as 보물 and 25 more mis-glossed kana words ([#385](https://github.com/sokcuri/hdTrans/issues/385)) ([139a6b8](https://github.com/sokcuri/hdTrans/commit/139a6b88a68468373020adce9ee60b2f7298db62))
* **dict:** read ぬ限り, the といい wish, お〜になる and ten more constructions ([#365](https://github.com/sokcuri/hdTrans/issues/365)) ([ccd90ae](https://github.com/sokcuri/hdTrans/commit/ccd90ae78feee93131f3ee70648d39b9f288a308))
* **dict:** read ビジネスマン, パンサー, フォルティシモ and other katakana loanwords with their right Korean ([#390](https://github.com/sokcuri/hdTrans/issues/390)) ([13c2c09](https://github.com/sokcuri/hdTrans/commit/13c2c096e72a636032bab3238a2e1d1cd1adbe80))
* **dict:** read 不足により, いとも容易く, 御用達 and the noun 語らい as words ([#350](https://github.com/sokcuri/hdTrans/issues/350)) ([1befb21](https://github.com/sokcuri/hdTrans/commit/1befb21819b4f5df7808f3a2856e8ebe0d98bddf))
* **dict:** read 受け方, kana けっかい, 取りつける and あとをたたない with their right Korean ([#379](https://github.com/sokcuri/hdTrans/issues/379)) ([e5af5e6](https://github.com/sokcuri/hdTrans/commit/e5af5e6e5e89a30be29722319fdf413615e7af41))
* **dict:** read 嫁姑, 奇才, 好天, 好手, 好球, 太洋 and 好々爺 by their meaning ([#387](https://github.com/sokcuri/hdTrans/issues/387)) ([090eae3](https://github.com/sokcuri/hdTrans/commit/090eae393d6c0c32d93b7758ed7d7b98dedfad41))
* **dict:** read 建売, 弁舌, 弓師, 後書き, 恭順 and other misglossed nouns with their right Korean ([#388](https://github.com/sokcuri/hdTrans/issues/388)) ([fc98227](https://github.com/sokcuri/hdTrans/commit/fc9822737d16dea27771b2dbe006bc8704fbfd84))
* **dict:** read 掴まる, 果たす, 沈める, 融通が利かない and 長い付き合い by their objects and cues ([#373](https://github.com/sokcuri/hdTrans/issues/373)) ([2f1a2bb](https://github.com/sokcuri/hdTrans/commit/2f1a2bb860b1d65b38dabee9efff78ccff6b3903))
* **dict:** read 晴れる by its subject — 걷히다 for smoke and mist, 트이다 for sight, 풀리다 for doubt, 밝아지다 for a face ([#370](https://github.com/sokcuri/hdTrans/issues/370)) ([6bbe9d4](https://github.com/sokcuri/hdTrans/commit/6bbe9d4790886a1810520219bede25173944a3f5))
* **dict:** read 気がする, 声が上がる, 名前を挙げる and eight more by their partner word ([#371](https://github.com/sokcuri/hdTrans/issues/371)) ([62b5dce](https://github.com/sokcuri/hdTrans/commit/62b5dce9808318f6ebe4d17cf9d7066a3490cfbe))
* **dict:** read 食い荒らす, 骨張る, 飛越す and eight food and animal nouns by their meaning ([#389](https://github.com/sokcuri/hdTrans/issues/389)) ([6ccdaf2](https://github.com/sokcuri/hdTrans/commit/6ccdaf2028e605303fba32cec8621b12a557b7f0))
* **dict:** spell katakana loanword nouns by the loanword rules and read ナット as 너트 beside fastening verbs ([#380](https://github.com/sokcuri/hdTrans/issues/380)) ([2d9d833](https://github.com/sokcuri/hdTrans/commit/2d9d833a025b4bef78b3672f05335bfaa2038ab9))
* **dict:** spell ブレンダー, ペンチ, ホッケー and other loanword rows right, read ボール盤 as 드릴링 머신 ([#382](https://github.com/sokcuri/hdTrans/issues/382)) ([05ca4e8](https://github.com/sokcuri/hdTrans/commit/05ca4e80e833b590ee362ddb4c9d4b4f5d0a6ac5))
* **dict:** write loanword glosses such as ピクルス, ファインプレー, ブレンダー and ブルゾン in their Korean spelling ([#376](https://github.com/sokcuri/hdTrans/issues/376)) ([e872717](https://github.com/sokcuri/hdTrans/commit/e872717505a101cb5d378be6d676a6b250333235))
* **dict:** write loanwords by their standard spelling and read お品書き, お流れ, かきわける and メドが立つ by their sense ([#383](https://github.com/sokcuri/hdTrans/issues/383)) ([cfb0993](https://github.com/sokcuri/hdTrans/commit/cfb09931584cd0bb5535b9318234ec2cca532b3f))
* **dict:** write misspelled and wrong-sense loanword glosses as マゾヒズム → 마조히즘, マダニ → 진드기, モールス → 모스 ([#377](https://github.com/sokcuri/hdTrans/issues/377)) ([ec26738](https://github.com/sokcuri/hdTrans/commit/ec267380cd400cbeaeb84f4793c77ef1987ddf03))
* **dict:** write misspelled loanword tech terms by their standard spelling, and read おりもの, 怨敵, ノスタルジック by their sense ([#384](https://github.com/sokcuri/hdTrans/issues/384)) ([df61046](https://github.com/sokcuri/hdTrans/commit/df6104617ef1b78af6b0ae29d9bf4d51773cda68))
* **dict:** write the Sino-Korean reading of 熟, 戊, 舵, 虞 and eight more kanji rows ([#363](https://github.com/sokcuri/hdTrans/issues/363)) ([5e5f642](https://github.com/sokcuri/hdTrans/commit/5e5f6425b8a7ec93505a1f1c97a9b24a4f1b7586))
* **grammar:** keep the quote verbs of QuoteTo after the PastSequence review fix ([#393](https://github.com/sokcuri/hdTrans/issues/393)) ([e45f262](https://github.com/sokcuri/hdTrans/commit/e45f2629ae8a0a0bd93ef18c81723ad4b4aef041))
* **grammar:** narrow the と and たら sequence of PastSequence after review ([#391](https://github.com/sokcuri/hdTrans/issues/391)) ([f969fc5](https://github.com/sokcuri/hdTrans/commit/f969fc59c704b4e5b99fcf49dfbfda3311447fd5))
* **grammar:** read clause-internal negatives by their function ([#331](https://github.com/sokcuri/hdTrans/issues/331)) ([7bfaa2a](https://github.com/sokcuri/hdTrans/commit/7bfaa2aa8c279d75d5d150f170845d4f301b3bc5))
* **grammar:** read と and たら before a past event as a step, not 면 ([#386](https://github.com/sokcuri/hdTrans/issues/386)) ([f223233](https://github.com/sokcuri/hdTrans/commit/f2232336a9268e49bebe6119999048934286c1f3))
* **grammar:** read 見受けられる as the spontaneous 보이다, not a potential of 판단하다 ([#352](https://github.com/sokcuri/hdTrans/issues/352)) ([0e57e00](https://github.com/sokcuri/hdTrans/commit/0e57e0024868ee931b4fa97851b279616a092ce9))
* **gui:** apply speech_levels from the settings ([#396](https://github.com/sokcuri/hdTrans/issues/396)) ([9193286](https://github.com/sokcuri/hdTrans/commit/91932862fb91b5a959d4b25f549650c59090b793))
* **lookup:** read kana spellings of kanji words as those words ([#317](https://github.com/sokcuri/hdTrans/issues/317)) ([3bef96f](https://github.com/sokcuri/hdTrans/commit/3bef96fa2dc99fd19724df5cfad437802c9aac15))
* **names:** read a kana name before a kinship term or a drawn-out honorific, and the spoken おねいさん ([#354](https://github.com/sokcuri/hdTrans/issues/354)) ([059c4b0](https://github.com/sokcuri/hdTrans/commit/059c4b054132b30c353b5dd3f5e93d41f54de85f))
* **names:** read a kanji name before an honorific when the honorific does not inflect its verb reading ([#364](https://github.com/sokcuri/hdTrans/issues/364)) ([3ca8ba5](https://github.com/sokcuri/hdTrans/commit/3ca8ba55721ab7496cab19946b329b69a59feafd))
* **normalize:** read kanji 様 after a predicate or の as よう ([#359](https://github.com/sokcuri/hdTrans/issues/359)) ([1d14b9f](https://github.com/sokcuri/hdTrans/commit/1d14b9fd7e286b9e3d6380a6fd5bedcf454ee9cc))
* **register:** rewrite only sentence-final predicates, and keep the levels the data showed wrong ([#333](https://github.com/sokcuri/hdTrans/issues/333)) ([908af9c](https://github.com/sokcuri/hdTrans/commit/908af9ce16fa75629722c0ae547a95f0bb3eb266))
* **register:** write a んです end after a 해요체 statement of the same quote in 해요체 ([#353](https://github.com/sokcuri/hdTrans/issues/353)) ([37814aa](https://github.com/sokcuri/hdTrans/commit/37814aa7a9402f781dab3837035a3d0812d1fc88))
* **rules:** read a name and くん before じゃない, not a rebuke ([#346](https://github.com/sokcuri/hdTrans/issues/346)) ([44e8176](https://github.com/sokcuri/hdTrans/commit/44e817601033b2ad31d30b181cbeed4a4bb80a7d))
* **rules:** read fixed constructions and set phrases from the chain ([#334](https://github.com/sokcuri/hdTrans/issues/334)) ([1c6b390](https://github.com/sokcuri/hdTrans/commit/1c6b390f69f8509f36d9333f59e12504d20d9bc9))
* **rules:** read quote constructions in と, って, とか and なんて from the chain ([#329](https://github.com/sokcuri/hdTrans/issues/329)) ([8d232f3](https://github.com/sokcuri/hdTrans/commit/8d232f3335c7e3bc3c8ecbbcaccde8bc6b09f67c))
* **rules:** read the function of sentence-final negatives ([#326](https://github.com/sokcuri/hdTrans/issues/326)) ([27c257e](https://github.com/sokcuri/hdTrans/commit/27c257eacf77452ac2ad7f454b0b2873459672dc))
* **rules:** read など, なんか, なんて after a noun before a negative as 따위 or 같은 건 ([#318](https://github.com/sokcuri/hdTrans/issues/318)) ([061740c](https://github.com/sokcuri/hdTrans/commit/061740c557d81a081ac5266329dadf87e9ce5b81))
* **segment:** read a kanji noun spelled out across a space after a lone kanji ([#372](https://github.com/sokcuri/hdTrans/issues/372)) ([c6e096a](https://github.com/sokcuri/hdTrans/commit/c6e096a038f5449fd222bd76b9a3960b99301987))
* **sound:** stutters separated by periods, kana しょうち read like 承知 ([#314](https://github.com/sokcuri/hdTrans/issues/314)) ([14fe19d](https://github.com/sokcuri/hdTrans/commit/14fe19d9208ed106237dfb0b0ec19f69cd5863a8))
* **speech:** ask plain and wh questions of dialogue by sentence type ([#348](https://github.com/sokcuri/hdTrans/issues/348)) ([a8cc8eb](https://github.com/sokcuri/hdTrans/commit/a8cc8eb4a05ce6c711f752e4fd7285c1d38d7461))
* **word:** katakana compounds — count kana no word covers as a stray piece; ウィーク 위크 ([#375](https://github.com/sokcuri/hdTrans/issues/375)) ([aba25a6](https://github.com/sokcuri/hdTrans/commit/aba25a616a0ec3f0d87fa84cad5d7ec548dc9b65))
* **word:** katakana loanword compounds — no stray name piece, -ckle and -pple endings ([#355](https://github.com/sokcuri/hdTrans/issues/355)) ([06c12e0](https://github.com/sokcuri/hdTrans/commit/06c12e03f81ea080af4d76112ec368b956cdef48))
* **word:** read godan e-row forms before 、 and kana みな as 皆 ([#349](https://github.com/sokcuri/hdTrans/issues/349)) ([e0ddb72](https://github.com/sokcuri/hdTrans/commit/e0ddb72f668ea8f722b04ea54448278216d1bffa))
* **words:** choose senses by the word, ending or mark next to them ([#319](https://github.com/sokcuri/hdTrans/issues/319)) ([e005b88](https://github.com/sokcuri/hdTrans/commit/e005b88c5b560e618367b8f596884cfec2a8fa2d))


### Performance Improvements

* index colloc patterns and skip construction families by shared atoms ([#366](https://github.com/sokcuri/hdTrans/issues/366)) ([0b74c49](https://github.com/sokcuri/hdTrans/commit/0b74c49ad88a08808f795687d2fd231cccfdf1c3))
* **register:** speech-level stage within 1.2× time and +20% memory of keep ([#335](https://github.com/sokcuri/hdTrans/issues/335)) ([10cc34a](https://github.com/sokcuri/hdTrans/commit/10cc34a8432b9f60e238bfb01fbb9bf977a407ef))

## [0.7.6](https://github.com/sokcuri/hdTrans/compare/v0.7.5...v0.7.6) (2026-10-31)


### Bug Fixes

* **analysis:** read a kanji verb continuative before the copula as its noun (311) ([2abcc3a](https://github.com/sokcuri/hdTrans/commit/2abcc3a12db2c0135409020a1266cdffb32b86ce))
* **release:** read 晴れ and other kanji continuatives before the copula as nouns ([85fc186](https://github.com/sokcuri/hdTrans/commit/85fc18690f8ff5031e2c2fbf030c3a7c3ccd1cdc))

## [0.7.5](https://github.com/sokcuri/hdTrans/compare/v0.7.4...v0.7.5) (2026-10-31)


### Bug Fixes

* **dict:** read broadcast terms, word senses and spoken phrasing of captions ([#304](https://github.com/sokcuri/hdTrans/issues/304)) ([e3894ea](https://github.com/sokcuri/hdTrans/commit/e3894eafe6eb3dc1e527bb2e81374562b16b2d9d))
* **dict:** read idioms, archaic and spoken endings, and context senses in fiction prose ([#306](https://github.com/sokcuri/hdTrans/issues/306)) ([8c0de6a](https://github.com/sokcuri/hdTrans/commit/8c0de6a2ae02654d29ce557e6a83b8de64d58af1))
* **dict:** read nouns and verbs of fiction prose by their Korean words ([#305](https://github.com/sokcuri/hdTrans/issues/305)) ([cadea27](https://github.com/sokcuri/hdTrans/commit/cadea271d48dc3ba7478774a3b9016a6b6c7a9ea))
* **names:** keep common words from becoming document-wide names ([#307](https://github.com/sokcuri/hdTrans/issues/307)) ([6570437](https://github.com/sokcuri/hdTrans/commit/65704374ad74ad8f9afbf17b4efe162c63c005fe))
* **names:** read person names, readings and honorifics consistently ([#302](https://github.com/sokcuri/hdTrans/issues/302)) ([4d59ff9](https://github.com/sokcuri/hdTrans/commit/4d59ff92b92c5d0b7b78f766cd503821eb554c8b))
* read spoken forms, Korean spelling and sound words at their cause ([#308](https://github.com/sokcuri/hdTrans/issues/308)) ([654e762](https://github.com/sokcuri/hdTrans/commit/654e7629a87b512b55d5f39c04e5df3ea384e9be))
* **release:** read fiction and caption text better and keep common words out of names ([a9d2139](https://github.com/sokcuri/hdTrans/commit/a9d21391b040b382e56c53cee989731cf4858eb2))

## [0.7.4](https://github.com/sokcuri/hdTrans/compare/v0.7.3...v0.7.4) (2026-10-29)


### Features

* **web:** two translation cards with output modes, a settings layer and per-card expand ([#283](https://github.com/sokcuri/hdTrans/issues/283)) ([9e01d40](https://github.com/sokcuri/hdTrans/commit/9e01d404575ed6b327455990fcf1cfb0c5113457))


### Bug Fixes

* **dict:** keep work titles off ordinary phrases and conjugate でございます ([#288](https://github.com/sokcuri/hdTrans/issues/288)) ([415e8d6](https://github.com/sokcuri/hdTrans/commit/415e8d654e7b0253d8a5deeaf77134335fdfb011))
* **dict:** read よろしく greetings as 잘 부탁 and regards to a person as 안부 ([#287](https://github.com/sokcuri/hdTrans/issues/287)) ([9f17712](https://github.com/sokcuri/hdTrans/commit/9f177126f1fc5df3fae1b0abcec25ad60b34275f))
* **release:** analyse spoken forms, join split words and keep source spacing ([24622c6](https://github.com/sokcuri/hdTrans/commit/24622c6bf4c11e977b873284f88e0896878a6f32))
* **sound:** keep the source spaces between sounds and copies of a word ([#284](https://github.com/sokcuri/hdTrans/issues/284)) ([e14ab67](https://github.com/sokcuri/hdTrans/commit/e14ab678c75c0d7656c06beea382e2d437820ed3))
* **sound:** write no comma the source lacks after a sound or conjunction ([#286](https://github.com/sokcuri/hdTrans/issues/286)) ([a0a0812](https://github.com/sokcuri/hdTrans/commit/a0a081210dd6503c2de345822c40c045261a68ec))
* **word:** analyse spoken and Kansai forms instead of sounding them out ([#285](https://github.com/sokcuri/hdTrans/issues/285)) ([5dcf600](https://github.com/sokcuri/hdTrans/commit/5dcf600e6da28e9eb82c259084a90c347928a9a8))
* **wrapped:** join a word split across a line break and read 半ば by its use ([#282](https://github.com/sokcuri/hdTrans/issues/282)) ([e82a7e5](https://github.com/sokcuri/hdTrans/commit/e82a7e5c0cd1d88b97df5f4797a1ad735f7bf36c))

## [0.7.3](https://github.com/sokcuri/hdTrans/compare/v0.7.2...v0.7.3) (2026-10-28)


### Bug Fixes

* **release:** space sign-led words by the word before them and keep Ehnd marks working ([2130884](https://github.com/sokcuri/hdTrans/commit/213088425802e9521afa72d20471aee818ffd92e))
* **spacing:** space sign-led words by the row before them and keep pre-filter marks out of words ([#278](https://github.com/sokcuri/hdTrans/issues/278)) ([4d32c37](https://github.com/sokcuri/hdTrans/commit/4d32c374ea4ae483c9771ff9a5738bf34906f55b))

## [0.7.2](https://github.com/sokcuri/hdTrans/compare/v0.7.1...v0.7.2) (2026-10-27)


### Features

* **tools:** find dictionary rows that shadow the words they are made of ([#260](https://github.com/sokcuri/hdTrans/issues/260)) ([0bc9123](https://github.com/sokcuri/hdTrans/commit/0bc9123ffd123d524bb1616d3cf504f6640ca05e))


### Bug Fixes

* **dict:** drop name rows that read ordinary phrases as unrelated titles ([#259](https://github.com/sokcuri/hdTrans/issues/259)) ([6cb5055](https://github.com/sokcuri/hdTrans/commit/6cb50557a9ce1218b33c54b215f4f2895eb34fd6))
* **dict:** drop name rows whose key starts with a particle ([#261](https://github.com/sokcuri/hdTrans/issues/261)) ([62727b2](https://github.com/sokcuri/hdTrans/commit/62727b286e963311985a1ac762ecca2c65ea63fd))
* **dict:** keep 쓰리 in established loanwords ([#269](https://github.com/sokcuri/hdTrans/issues/269)) ([f197f04](https://github.com/sokcuri/hdTrans/commit/f197f04d93e1d9b9a1e320a3da1a3c74c8d0057a))
* **dict:** read web-novel phrases as Korean words ([#263](https://github.com/sokcuri/hdTrans/issues/263)) ([191e33f](https://github.com/sokcuri/hdTrans/commit/191e33f2ec670eb3f00e3f72dc17a7ddac349acb))
* **dict:** read 一人暮らしの and 当てつけ as Korean words ([#265](https://github.com/sokcuri/hdTrans/issues/265)) ([a66e3fa](https://github.com/sokcuri/hdTrans/commit/a66e3fad16bdb1abb4b94f1a0bb224d63020fb03))
* **dict:** read 聖さん as a name with さん, not 성찬 ([#264](https://github.com/sokcuri/hdTrans/issues/264)) ([1a39d92](https://github.com/sokcuri/hdTrans/commit/1a39d921da2ce878dcad0e8774f3b39a48292062))
* **dict:** spell katakana loanwords by the standard and transliterate English phrases ([#262](https://github.com/sokcuri/hdTrans/issues/262)) ([4ced747](https://github.com/sokcuri/hdTrans/commit/4ced747ccec23fd5dab9f11d7252b6afeed6d3b7))
* **grammar:** read greeting どうも, ことも + できず and 両親を養う naturally ([#266](https://github.com/sokcuri/hdTrans/issues/266)) ([d371bc5](https://github.com/sokcuri/hdTrans/commit/d371bc54ab4be4ae26af4d371fd5b00edbdb3aea))
* **grammar:** read quoting などと as a quote, not a list ([#267](https://github.com/sokcuri/hdTrans/issues/267)) ([9d63d8b](https://github.com/sokcuri/hdTrans/commit/9d63d8bc56eb375542184324c5e8e3de1f1833b8))
* **release:** read name rows, loanwords and quoting などと correctly ([720f754](https://github.com/sokcuri/hdTrans/commit/720f754c73dd7253539f3ec6124b9ab2bdb2985a))

## [0.7.1](https://github.com/sokcuri/hdTrans/compare/v0.7.0...v0.7.1) (2026-10-27)


### Bug Fixes

* **dict:** read 気味悪がる, 面白がる and 可愛がられる as verbs ([#252](https://github.com/sokcuri/hdTrans/issues/252)) ([bc6c792](https://github.com/sokcuri/hdTrans/commit/bc6c7929747925dd0f34c105f2fb94cfd57f9d4d))
* **release:** read 気味悪がる, 面白がる and 可愛がられる as verbs ([d183523](https://github.com/sokcuri/hdTrans/commit/d183523c32f6b096320846f5c26696bf88e05cfe))

## [0.7.0](https://github.com/sokcuri/hdTrans/compare/v0.6.2...v0.7.0) (2026-10-27)


### ⚠ BREAKING CHANGES

* **data:** users of 0.6.2 or earlier move user/ and Ehnd dictionaries into hdtrans-data/. Expectation sets are unchanged.

### Features

* **data:** read native data from hdtrans-data next to the program ([#230](https://github.com/sokcuri/hdTrans/issues/230)) ([f3a7840](https://github.com/sokcuri/hdTrans/commit/f3a7840f65084f99135671b46c4a1de261f715f4))
* **dict:** skip bad dictionary rows and keep the old stack on failed reloads ([#226](https://github.com/sokcuri/hdTrans/issues/226)) ([35fd56c](https://github.com/sokcuri/hdTrans/commit/35fd56c0b7f26565b4b5863c3c4592fb793e1f9f))
* **gui:** add a 사전 확인 button to the log tab ([#235](https://github.com/sokcuri/hdTrans/issues/235)) ([1ddba56](https://github.com/sokcuri/hdTrans/commit/1ddba5636fc2394a56dc90a46c78cf76d61171d0))
* **gui:** log dictionary changes as full rows and pick up external edits ([#247](https://github.com/sokcuri/hdTrans/issues/247)) ([e1269c8](https://github.com/sokcuri/hdTrans/commit/e1269c8f878e4a26352b08872aad3f2009830851))
* **gui:** search the 사전 tab entries ([#249](https://github.com/sokcuri/hdTrans/issues/249)) ([187432c](https://github.com/sokcuri/hdTrans/commit/187432c0c7a418533613d281445a759913e005bb))
* **release:** hdtrans-data folder, dictionary warnings and a fuller dictionary tab ([f6c87f2](https://github.com/sokcuri/hdTrans/commit/f6c87f222e7ec175a4e1f3f44287ec77478f45be))
* **web:** credit the author in the title and put the version on its own line ([#213](https://github.com/sokcuri/hdTrans/issues/213)) ([c57d82f](https://github.com/sokcuri/hdTrans/commit/c57d82f22b9ee36f13c04c07ae5ebc70bf6bacef))


### Bug Fixes

* **conjugation:** read short causatives and the past of かねる ([#217](https://github.com/sokcuri/hdTrans/issues/217)) ([6e74d62](https://github.com/sokcuri/hdTrans/commit/6e74d620660eb64627eee2f50ac26ea5831655f7))
* **dict:** add the missing ざわつき noun ([#198](https://github.com/sokcuri/hdTrans/issues/198)) ([860fbd2](https://github.com/sokcuri/hdTrans/commit/860fbd2c6b030cf4684bb5308da4462c48d8088a))
* **dict:** add the missing 横たえる verb stem ([#191](https://github.com/sokcuri/hdTrans/issues/191)) ([4c6b441](https://github.com/sokcuri/hdTrans/commit/4c6b441bc91d1dc42924453c0dfc1c6133810939))
* **dict:** give 32 rows marker families that fit their class ([#227](https://github.com/sokcuri/hdTrans/issues/227)) ([a39b124](https://github.com/sokcuri/hdTrans/commit/a39b124a888eb40d4fc12e39f52b81984abd9b90))
* **dict:** keep work-title rows from matching ordinary phrases ([#219](https://github.com/sokcuri/hdTrans/issues/219)) ([2e11737](https://github.com/sokcuri/hdTrans/commit/2e11737277a9a7108f6a8772f0d3a4a1bc33ca78))
* **dict:** mark 連綿 as a tari noun ([#199](https://github.com/sokcuri/hdTrans/issues/199)) ([a50be6c](https://github.com/sokcuri/hdTrans/commit/a50be6c64965cdc73309a1955f562cc4e09ec235))
* **dict:** read common idioms as the Korean idioms and 苦手 of things as 질색 ([#237](https://github.com/sokcuri/hdTrans/issues/237)) ([f58fb68](https://github.com/sokcuri/hdTrans/commit/f58fb6838907ea4e6a11f99a09e3342c80fce53e))
* **dict:** read 月光花 as 월광화 and keep two coined titles ([#220](https://github.com/sokcuri/hdTrans/issues/220)) ([c5a3bb5](https://github.com/sokcuri/hdTrans/commit/c5a3bb5a7587de251508d3d0cfc6dc79a420944b))
* **dict:** read 迷惑だ as 민폐다, 迷惑する as 곤란해하다 and a past before laughter as ended ([#250](https://github.com/sokcuri/hdTrans/issues/250)) ([09f2c1b](https://github.com/sokcuri/hdTrans/commit/09f2c1bd6cbdbdc7f083a9a74c63cc0757217b6a))
* **dict:** regenerate the rules program and fix two targets after the bulk merge ([0ce2ad2](https://github.com/sokcuri/hdTrans/commit/0ce2ad22facccd9e2ef237c8418f2dbe54892542))
* **dict:** render coined work titles literally and keep their symbols ([#221](https://github.com/sokcuri/hdTrans/issues/221)) ([3f2250e](https://github.com/sokcuri/hdTrans/commit/3f2250ed0bc548f1b2c079ac5a2be475b5c0acfb))
* **dict:** render work titles literally instead of unrelated release titles ([#239](https://github.com/sokcuri/hdTrans/issues/239)) ([8b8884d](https://github.com/sokcuri/hdTrans/commit/8b8884d530eec07472885688f4cb839969b69df1))
* **dict:** tell literal 油を売る, read が苦手 of food and noun に越したことはない ([#244](https://github.com/sokcuri/hdTrans/issues/244)) ([5b94787](https://github.com/sokcuri/hdTrans/commit/5b94787941c38d68ea3685982edd2d419851120c))
* **dict:** type 瞼 as an eyelid body part ([#197](https://github.com/sokcuri/hdTrans/issues/197)) ([a8c5c88](https://github.com/sokcuri/hdTrans/commit/a8c5c8879a64ae1050d1ea5298f4683a7a2be196))
* **dict:** write Korean in 87 targets that held kana or kanji ([#228](https://github.com/sokcuri/hdTrans/issues/228)) ([76713da](https://github.com/sokcuri/hdTrans/commit/76713daa4a8e462cc950056cde81ee4fc0fe8208))
* **grammar:** distinguish synthetic causative potential from passive ([#208](https://github.com/sokcuri/hdTrans/issues/208)) ([fdebfae](https://github.com/sokcuri/hdTrans/commit/fdebfae999f9210258dca5c509e0afb57a023091))
* **grammar:** exclude plain passive volition from presumptive selection ([#222](https://github.com/sokcuri/hdTrans/issues/222)) ([514905b](https://github.com/sokcuri/hdTrans/commit/514905b0fc09515e3034639e16964e5a431a00e3))
* **grammar:** preserve lexical causative-passive polite statements ([#216](https://github.com/sokcuri/hdTrans/issues/216)) ([afafeb3](https://github.com/sokcuri/hdTrans/commit/afafeb3b3f756cbfba38f828802ce87feca2d702))
* **grammar:** read a derived causative in られよう as the causative-passive ([#234](https://github.com/sokcuri/hdTrans/issues/234)) ([b8d59ec](https://github.com/sokcuri/hdTrans/commit/b8d59ec460b0cfdaf59156a8f35d0a92b07c39e8))
* **grammar:** read させてもらう as the speaker's act with leave ([#218](https://github.com/sokcuri/hdTrans/issues/218)) ([2b9bb6f](https://github.com/sokcuri/hdTrans/commit/2b9bb6f5592d0d9cb9f84901942bdd48bbb8c685))
* **gui:** clear the entry fields after adding a dictionary row ([#243](https://github.com/sokcuri/hdTrans/issues/243)) ([7724bfb](https://github.com/sokcuri/hdTrans/commit/7724bfb168fd1b347499be61808cbfcffe07f497))
* **layout:** preserve spaced fillers and bound grammar ([#201](https://github.com/sokcuri/hdTrans/issues/201)) ([8283606](https://github.com/sokcuri/hdTrans/commit/8283606b860fa78c3ec51c1f4049c628c091b831))
* **lookup:** read a prefix kanji before a common word instead of a place name ([#224](https://github.com/sokcuri/hdTrans/issues/224)) ([8becc8e](https://github.com/sokcuri/hdTrans/commit/8becc8e6372c0d40e0869ac3225317e29d05dbfa))
* **markup:** read &lt;R&gt; ruby tags like brace ruby and keep text brackets ([#242](https://github.com/sokcuri/hdTrans/issues/242)) ([5c4b005](https://github.com/sokcuri/hdTrans/commit/5c4b0056c64f54484be451068666c46e76e19ae7))
* **names:** honor explicit readings in speaker labels ([#206](https://github.com/sokcuri/hdTrans/issues/206)) ([2a4201e](https://github.com/sokcuri/hdTrans/commit/2a4201ef9baa0e35acebdcc898599c53cdcc3a21))
* **names:** keep compounds of common katakana words out of document names ([#211](https://github.com/sokcuri/hdTrans/issues/211)) ([0426a8a](https://github.com/sokcuri/hdTrans/commit/0426a8ad438e828160be3fc001fe688a20211fc2))
* **names:** read marked lone name kanji like the bare line, and read unread kana ([#245](https://github.com/sokcuri/hdTrans/issues/245)) ([d622eba](https://github.com/sokcuri/hdTrans/commit/d622eba5d1fda0b9c0d47a400574d6cddda755a6))
* **names:** read work titles with the marks and kana of the text ([#225](https://github.com/sokcuri/hdTrans/issues/225)) ([41eeea6](https://github.com/sokcuri/hdTrans/commit/41eeea62a94ab4a91d22bfbeac11b9eda79f7f0d))
* **normalize:** compose canonical kana before ruby parsing ([#209](https://github.com/sokcuri/hdTrans/issues/209)) ([aae477f](https://github.com/sokcuri/hdTrans/commit/aae477f7f5ab63908ba3b2ca4cd93886d8ed976a))
* **normalize:** compose kana before name and reading discovery ([#212](https://github.com/sokcuri/hdTrans/issues/212)) ([988faa6](https://github.com/sokcuri/hdTrans/commit/988faa6328b112ad802bcacbe120a5b089ef59ca))
* **output:** end 아니다 in 아니야 and 아니에요 for every family, also before laughter ([#246](https://github.com/sokcuri/hdTrans/issues/246)) ([6c5fdb8](https://github.com/sokcuri/hdTrans/commit/6c5fdb8f82fd9c6fb36fda97e3d28a33d9075f26))
* **output:** never emit raw conjugation markers ([#229](https://github.com/sokcuri/hdTrans/issues/229)) ([6b7e6cf](https://github.com/sokcuri/hdTrans/commit/6b7e6cf9494a251a25f57d3814fd320a24b1506b))
* **release:** date new release entries in Asia/Seoul ([#215](https://github.com/sokcuri/hdTrans/issues/215)) ([0817e60](https://github.com/sokcuri/hdTrans/commit/0817e60f4c0ea1c6e1240db1358ed1d19b0f560b))
* **rewrite:** end spoken lines in 해체 and read quoted ちゃう by what befalls one ([#241](https://github.com/sokcuri/hdTrans/issues/241)) ([dec707e](https://github.com/sokcuri/hdTrans/commit/dec707e447e9957b5cb2db835d30043c1d7ee49a))
* **rewrite:** read Xとしている of a 하다 adjective as that adjective ([#233](https://github.com/sokcuri/hdTrans/issues/233)) ([b74b2e1](https://github.com/sokcuri/hdTrans/commit/b74b2e1935720e6dc44ed2e0cf81508740a0d494))
* **rewrite:** read しんとしている as 고요하다 ([#248](https://github.com/sokcuri/hdTrans/issues/248)) ([b14a399](https://github.com/sokcuri/hdTrans/commit/b14a399e56e31926c916967f0bb601259e3965b5))
* **rules:** normalize terminal negative imperatives ([#203](https://github.com/sokcuri/hdTrans/issues/203)) ([d2109d2](https://github.com/sokcuri/hdTrans/commit/d2109d2cae0d089bb15da353f53c824c00079ed1))
* **rules:** read marks, ＄ and stretched kana after the symbol fix ([#238](https://github.com/sokcuri/hdTrans/issues/238)) ([e63062c](https://github.com/sokcuri/hdTrans/commit/e63062c22c2069ee627b09f6be424ca073fecc13))
* **spacing:** read unit signs by their reading and space numbers written with signs ([#236](https://github.com/sokcuri/hdTrans/issues/236)) ([3116d30](https://github.com/sokcuri/hdTrans/commit/3116d307637f396ece9023c501c4a6b0f70a1ce9))
* **spacing:** space a Latin word from the Korean noun after it ([#223](https://github.com/sokcuri/hdTrans/issues/223)) ([c72f584](https://github.com/sokcuri/hdTrans/commit/c72f58438bc6b1a98a835973165fb56894894ac9))
* **speech:** distinguish おいで requests from respectful progressives ([#200](https://github.com/sokcuri/hdTrans/issues/200)) ([0a6fccf](https://github.com/sokcuri/hdTrans/commit/0a6fccff30d5e279150f6581fcea0a84f8b5e883))
* **text:** keep Chinese text as written and read every Japanese kanji ([#240](https://github.com/sokcuri/hdTrans/issues/240)) ([b7ad335](https://github.com/sokcuri/hdTrans/commit/b7ad33516ba42af137dd403d985d7554033bc593))
* **tokenizer:** preserve Latin identifiers after kana ([#210](https://github.com/sokcuri/hdTrans/issues/210)) ([138235c](https://github.com/sokcuri/hdTrans/commit/138235cc059b4bc12afa886a49cfd8714f44b9cb))
* **tokenizer:** write symbols as they stand and read 〇, 々, ゝ, ～ by context ([#231](https://github.com/sokcuri/hdTrans/issues/231)) ([816a1a0](https://github.com/sokcuri/hdTrans/commit/816a1a00ffaaf04f2455cb7b550c42aa9083ce93))
* **word:** keep degree-suffix negation out of adnominal parsing ([#207](https://github.com/sokcuri/hdTrans/issues/207)) ([12c0c32](https://github.com/sokcuri/hdTrans/commit/12c0c328d98f2d1f4f568b376e0ee15d0b00c7dc))
* **word:** preserve unknown kana stems before particles and endings ([#202](https://github.com/sokcuri/hdTrans/issues/202)) ([80eb903](https://github.com/sokcuri/hdTrans/commit/80eb9032182355dc315699fc0c50adfaf31381f4))
* **word:** read だけ before a verb as the particle ([#232](https://github.com/sokcuri/hdTrans/issues/232)) ([d0c1ded](https://github.com/sokcuri/hdTrans/commit/d0c1ded94e7eb98a46613312bef5f843eb30dc49))

## [0.6.2](https://github.com/sokcuri/hdTrans/compare/v0.6.1...v0.6.2) (2026-10-26)


### Bug Fixes

* **dict:** keep compound event and dish nouns together ([#188](https://github.com/sokcuri/hdTrans/issues/188)) ([b7b9984](https://github.com/sokcuri/hdTrans/commit/b7b9984ad35bb8d69289e59404216170c890e015))
* **dict:** read 真逆 as the noun 정반대 ([#189](https://github.com/sokcuri/hdTrans/issues/189)) ([7871883](https://github.com/sokcuri/hdTrans/commit/7871883b4e5cb0eca5e7bb1bf71a391fa25be8f4))
* **dict:** restore predicate markers and action noun glosses ([#190](https://github.com/sokcuri/hdTrans/issues/190)) ([f1eeacc](https://github.com/sokcuri/hdTrans/commit/f1eeacce964d3241eccc3299e8289d2569a04f92))
* **names:** read caption speaker labels as names and start the speech after them ([#183](https://github.com/sokcuri/hdTrans/issues/183)) ([d6dd31e](https://github.com/sokcuri/hdTrans/commit/d6dd31e95d8ec9554c57bc4deba49bca87bc1530))
* read spoken contractions and causative passives in captions ([#185](https://github.com/sokcuri/hdTrans/issues/185)) ([967cac1](https://github.com/sokcuri/hdTrans/commit/967cac197f42dff4f01839af8a5a266aff8f438c))
* **release:** read TV captions, spoken contractions and caption names better ([94a178a](https://github.com/sokcuri/hdTrans/commit/94a178a57988be9a4752438d4e4dbf04f71b6352))
* **segment:** read a number in round brackets at a line start as an item number ([#193](https://github.com/sokcuri/hdTrans/issues/193)) ([30eff5a](https://github.com/sokcuri/hdTrans/commit/30eff5a318a5d3c34cd0cb2ef7dc51e79992de79))
* **sound:** read fused and clipped adjective endings as the adjective ([#192](https://github.com/sokcuri/hdTrans/issues/192)) ([8da4892](https://github.com/sokcuri/hdTrans/commit/8da4892f30dc8d156b7eb19bf75519fb5e3ded65))
* **tokenizer:** read a space between Japanese words as a word boundary ([#184](https://github.com/sokcuri/hdTrans/issues/184)) ([8e4466a](https://github.com/sokcuri/hdTrans/commit/8e4466a78eb81c351f7043dcd4bb27e56ff3e4e1))
* **wrapped:** join a bare predicate with the noun it describes on the next line ([#187](https://github.com/sokcuri/hdTrans/issues/187)) ([0602f09](https://github.com/sokcuri/hdTrans/commit/0602f097da89719de07a19c873716c8fb471de8f))


### Performance Improvements

* skip dictionary lookups that cannot match, and keep 言っとくけど ([#194](https://github.com/sokcuri/hdTrans/issues/194)) ([d203112](https://github.com/sokcuri/hdTrans/commit/d2031122b8306f982b084fabd8f0844fc3d52aee))

## [0.6.1](https://github.com/sokcuri/hdTrans/compare/v0.6.0...v0.6.1) (2026-10-26)


### Features

* join lines broken mid-sentence when asked ([#179](https://github.com/sokcuri/hdTrans/issues/179)) ([2fdb13f](https://github.com/sokcuri/hdTrans/commit/2fdb13f43d541bf6290a92fa429e068d93f5a9a6))


### Bug Fixes

* **release:** translate lines broken mid-sentence and take earlier lines as context ([2f792e7](https://github.com/sokcuri/hdTrans/commit/2f792e70b64604160273cc943bdea119233e9627))

## [0.6.0](https://github.com/sokcuri/hdTrans/compare/v0.5.9...v0.6.0) (2026-10-26)


### Features

* **release:** license hdTrans under MPL-2.0 and add a security policy ([ae21362](https://github.com/sokcuri/hdTrans/commit/ae21362a7081b2ebbc18228293112639c7d8aff4))

## [0.5.9](https://github.com/sokcuri/hdTrans/compare/v0.5.8...v0.5.9) (2026-10-26)


### Features

* **gui:** merge the windows into tabs and add a dictionary tab ([#170](https://github.com/sokcuri/hdTrans/issues/170)) ([2ad8e7c](https://github.com/sokcuri/hdTrans/commit/2ad8e7ce5c7e96efda1dc8eaefc8b8977f2b09b0))
* **gui:** take dropped files in the main and document windows ([#168](https://github.com/sokcuri/hdTrans/issues/168)) ([640792d](https://github.com/sokcuri/hdTrans/commit/640792de02e6c3524907223d81210df29a04d07d)), refs [#165](https://github.com/sokcuri/hdTrans/issues/165)
* **gui:** translate several documents into the source or a chosen folder ([#166](https://github.com/sokcuri/hdTrans/issues/166)) ([d324609](https://github.com/sokcuri/hdTrans/commit/d324609b50a20e41d6f870299e42eddff2c4d31e)), refs [#165](https://github.com/sokcuri/hdTrans/issues/165)
* **web:** add a dictionary tab with several user dictionaries ([#169](https://github.com/sokcuri/hdTrans/issues/169)) ([fa190f8](https://github.com/sokcuri/hdTrans/commit/fa190f871dbfb56096d5b2b2cd53db3f09f833c7))
* **web:** add a document tab that translates several text files ([#167](https://github.com/sokcuri/hdTrans/issues/167)) ([e4e2cda](https://github.com/sokcuri/hdTrans/commit/e4e2cda015117646a0323ef870fb6c37c26498f9))


### Bug Fixes

* **gui:** keep the window and the translate row inside the screen ([#171](https://github.com/sokcuri/hdTrans/issues/171)) ([f6ae136](https://github.com/sokcuri/hdTrans/commit/f6ae136dc8dc89d54b31122c0e7ca3bdd2f5eed7))
* keep the source's line endings in translated documents ([#173](https://github.com/sokcuri/hdTrans/issues/173)) ([7b61b86](https://github.com/sokcuri/hdTrans/commit/7b61b86b5037edea96f0807068622ef3247f6549))
* **release:** translate documents and manage user dictionaries on the web and in the Windows app ([870abec](https://github.com/sokcuri/hdTrans/commit/870abec7df42025be52a44fa7ba292aebbef04c9))

## [0.5.8](https://github.com/sokcuri/hdTrans/compare/v0.5.7...v0.5.8) (2026-10-25)


### Bug Fixes

* **release:** make snapshot lookups as fast as loaded dictionaries ([7a11b8f](https://github.com/sokcuri/hdTrans/commit/7a11b8f3eb3fce060cd59783e891f24aaa3e8a78))


### Performance Improvements

* **dict:** check the record bytes of a key group once per lookup ([#163](https://github.com/sokcuri/hdTrans/issues/163)) ([8a7fdbf](https://github.com/sokcuri/hdTrans/commit/8a7fdbf1575c5cfe30a12b1538f83276b968326e))

## [0.5.7](https://github.com/sokcuri/hdTrans/compare/v0.5.6...v0.5.7) (2026-10-25)


### Features

* **speech:** read bare lines of speech in soft as quoted speech ([#160](https://github.com/sokcuri/hdTrans/issues/160)) ([095ae97](https://github.com/sokcuri/hdTrans/commit/095ae97a76d1b92cb2169b47b72c7e6d3319789d))


### Bug Fixes

* **release:** let soft speech cover bracketless dialogue and explain speech and translation modes ([6889a47](https://github.com/sokcuri/hdTrans/commit/6889a47aa3e64e49dddbc7f48085978b314a4170))

## [0.5.6](https://github.com/sokcuri/hdTrans/compare/v0.5.5...v0.5.6) (2026-10-25)


### Bug Fixes

* **release:** cut hot-path work and let soft speech cover bracketless dialogue ([0d14f69](https://github.com/sokcuri/hdTrans/commit/0d14f69834f92a88fe7932845c1ffe475cabb07b))


### Performance Improvements

* cut about 6% of instructions from hot paths with no change in output ([#159](https://github.com/sokcuri/hdTrans/issues/159)) ([2b56a9c](https://github.com/sokcuri/hdTrans/commit/2b56a9ccc4860341a0667c2746c96c4e93f957a4))

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
