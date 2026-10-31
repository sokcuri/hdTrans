# 번역 파이프라인과 실행 층

번역 한 줄은 아래 표의 층을 위에서 아래로 지납니다. 순서는 코드에 들어 있고(`src/core/engine/driver.cpp`가
단계를, `Worker::DoLookup`이 찾기 단계를, `src/core/word/flush.cpp`의 `Flush`가 문장 패스를 차례로 부름),
이 문서는 그 순서를 적은 것입니다. `tests/unit/rules/layers_test.cpp`가 표의 **코드** 열을 읽어, 같은 함수
안에서 각 층의 호출이 표의 순서대로 나오는지 확인합니다. 코드의 순서를 바꾸면 이 표도 고쳐야 테스트가
통과합니다.

규칙 언어와 규칙 파일은 [rule-language.md](rule-language.md), 규칙 컴파일러는 [rule-compiler.md](rule-compiler.md)에 있습니다.

## 층

- **의미**: 한 층 안에서 무엇을 어떤 순서로 적용하는지. *처음 참*은 첫 번째로 맞는 것에서 멈춘다는 뜻이고,
  *모두 적용*은 맞는 것을 차례로 다 적용한다는 뜻입니다.
- **코드**: 파일, 함수, 그 함수 안의 호출. 같은 함수의 행끼리는 바로 앞 행의 호출보다 뒤에서 처음 나오는 곳을 찾습니다.

| 층 | 의미 | 바꾸는 것 | 부수 효과 | 읽는 것 | 코드 |
|---|---|---|---|---|---|
| prefilter | 모든 행 적용(층 순서, 그 안은 불러온 순서) | 줄 텍스트 | 없음 | Ehnd 폴더의 prefilter | `src/core/engine/driver.cpp` `TranslateDocument` `filter::Stage::Pre` |
| whole-line | 줄이 정확히 같으면 그 번역으로 끝냄 | 줄 전체 | 뒤 층을 건너뜀 | `src/core/dict/builtin/lines.tsv` | `src/core/engine/driver.cpp` `TranslateBody` `facts.wholeLine` |
| normalize | 모두 적용 | 줄 텍스트 | 없음 | 코드 | `src/core/engine/driver.cpp` `TranslateBody` `pipeline::Normalize(` |
| sound | 모두 적용 | 가나 소리를 한국어로 | 없음 | `masked.tsv`, `sound.tsv`, `phrases.tsv` | `src/core/engine/driver.cpp` `TranslateBody` `pipeline::Sound(` |
| protect | 모두 적용 | 보호 구간(이미 한글인 부분 등) | 없음 | 코드 | `src/core/engine/driver.cpp` `TranslateBody` `pipeline::ProtectTyped(` |
| lookup | 사전 순서로 후보를 모음. 뒤 사전이 같은 키, 품사의 `endings`, `phrases`, `patterns` 행을 바꿈 | 후보 목록 | 이 순서가 뒤 층의 "처음 참" 순서 | `dicts/*.tsv`, 사용자 사전 | `src/core/word/lookup.cpp` `Worker::DoLookup` `snapshot.GetWordInfo(` |
| constraints | 표 순서로 모두 적용. 후보를 모두 지우지는 않음 | 후보 제거, 이름 항목 추가 | 추가한 이름 항목은 `-why`에 `Fixed`로 보임 | `src/core/word/constraints.cpp`의 `kRules` | `src/core/word/lookup.cpp` `Worker::DoLookup` `ApplyConstraints(found, query)` |
| lookup-drops | 모두 적용 | 후보 제거, 번역 음역 | 없음 | 코드 | `src/core/word/lookup.cpp` `Worker::DoLookup` `DropBoundUtterances(l, query)` |
| pre-select | 모두 적용 | 항목 삽입, 자리를 정하지 못한 조사를 조사로 | 없음 | 코드 | `src/core/word/flush.cpp` `Flush` `"DrainNmTail"` |
| voice | 동사마다 (ら)れ의 읽기를 한 번, 신호 순서대로 처음 맞는 것(spec GRM-10) | 수동, 가능, 높임, 자발의 기록(문법 코드 Ka→Pi), 맞지 않는 후보 제거, 바탕 동사로 다시 읽기, 행위자 に의 `에게` | 뒤 층은 그 읽기를 따름(`rewrites`의 `RespectfulPassive`가 높임을 씀) | `classes.tsv`, 코드 | `src/core/word/flush.cpp` `Flush` `"DecideVoice"` |
| local | 단어마다 후보를 사전 순서로 두고 그 행의 지역 규칙을 실행, 처음 참인 후보 | 후보 선택, `D=`, `Y=` 번역 | 거짓인 규칙의 대입은 되돌림 | 사전 `ctx` 열 | `src/core/word/selectors.cpp` `CleanupSelectors` `RunLocalRule(f, it, source.id)` |
| global-stem | 후보마다 품사키가 맞는 어간 전역 규칙을 PRIO 순으로, 번호 1001~4999, 처음 참 | 후보 선택, 번역 | 거짓인 규칙의 대입은 되돌림 | `src/core/rules/globals.txt` | `src/core/word/selectors.cpp` `CleanupSelectors` `false, &source.id)` |
| defaults | 하나뿐 → 첫 `XXX` → `J` 숫자 | 후보 선택 | 없음 | 사전 `sem`, `gram` | `src/core/word/selectors.cpp` `CleanupSelectors` `SelectLastXXX(it)` |
| ending | 어미 고르기: 아래 세 층 | 어미 선택 | 없음 | 아래 세 층 | `src/core/word/selectors.cpp` `CleanupSelectors` `SelectDirect(f, it)` |
| local-ending | 어미 후보 순서로 그 행의 지역 규칙, 처음 참 | 어미 선택, 대입 | 거짓인 규칙의 대입은 되돌림 | 사전 `ctx` 열(어미 행) | `src/core/word/selectors.cpp` `SelectDirect` `RunLocalRule(f, it, source.id)` |
| global-ending | 어미 후보마다 어미 전역 규칙을 PRIO 순으로, 처음 참 | 어미 선택, 대입 | 거짓인 규칙의 대입은 되돌림 | `src/core/rules/globals.txt` | `src/core/word/selectors.cpp` `SelectDirect` `RunGlobalRule(f, it, false` |
| ending-defaults | 하나뿐 → 첫 `XXX` → `J` 숫자 | 어미 선택 | 없음 | 사전 `sem`, `gram` | `src/core/word/selectors.cpp` `SelectDirect` `SelectDirectLastXXX(it)` |
| global-last | 후보마다 어간 전역 규칙을 PRIO 순으로, 번호 1~999, 처음 참 | 앞에서 고른 후보를 **바꿈**(어미는 다시 고르지 않음) | 대입은 남음 | `src/core/rules/globals.txt` | `src/core/word/selectors.cpp` `CleanupSelectors` `true, &source.id)` |
| class3 | 모두 적용 | 선택, 번역 | 없음 | 코드 | `src/core/word/flush.cpp` `Flush` `"CleanupClass3"` |
| senses | 키마다 행을 파일 순서로, 처음 맞는 행 | 선택 또는 번역 | 없음 | `src/core/dict/builtin/senses.tsv` | `src/core/word/select.cpp` `Select` `ChooseSenses(f)` |
| scorers | 처음 다른 후보를 고른 채점기 | 선택 | 없음 | 문서 의미 기억 | `src/core/word/select.cpp` `Select` `scorer.score(` |
| rewrites | `kTextPasses`의 패스를 차례로, 각 패스 뒤에는 그 뒤에 두기로 한 구문 표 가족. 각 패스와 가족은 왼쪽부터, 가족은 단어마다 PRIO 순으로 처음 참인 행 | 선택, 어간, 어미 번역 | 뒤 패스가 앞 패스를 덮음 | 코드, `patterns.tsv`, `constructions.tsv` | `src/core/word/flush.cpp` `Flush` `kTextPasses` |
| misread | 결정을 모두 계산한 뒤 적용(`-misread`) | 후보, 어미, 가나 소리 | 없음 | 모델 | `src/core/word/flush.cpp` `Flush` `"FilterMisreads"` |
| cg-lite | tier 오름차순, 각 tier는 고정점까지. REMOVE와 SELECT | 후보 제거, 선택 | 고른 후보가 지워지면 첫 살아 있는 후보 | `cg_rules.tsv`, `cg_learned.tsv` | `src/core/word/flush.cpp` `Flush` `"FilterCg"` |
| packs | 단어마다, 칸(뜻, 이름, 부정, 시제)마다 조건이 모두 맞는 팩 행 가운데 가장 구체적인 행([packs.md](packs.md)). 태 행은 `voice` 층 안에서 | 선택, 번역, 어미 번역 | 팩이 없으면 돌지 않음 | 사전 폴더의 `#! layer=pack` 파일 | `src/core/word/flush.cpp` `Flush` `"ApplyPacks"` |
| compose | 항목을 오른쪽에서 왼쪽으로 | 조사 형태, 활용 | 없음 | 코드 | `src/core/engine/driver.cpp` `TranslateChunk` `s.composer.Compose(` |
| cleanup | 모두 적용 | 출력 텍스트 | 없음 | 코드 | `src/core/engine/driver.cpp` `TranslateBody` `output::Cleanup(sentences, gaps)` |
| restore | 차례로. `cleanup.tsv`는 위치마다 파일 순서의 첫 행 | 출력 텍스트 | 없음 | `src/core/dict/builtin/extra/cleanup.tsv` | `src/core/engine/driver.cpp` `TranslateBody` `pipeline::RestoreAll(` |
| register | 문서의 대사 줄마다, 말투 수준이 바뀌는 줄만(spec REG-10~REG-12) | 대사 줄의 말투, 그 줄의 입말 꼴 | 자막 문서와 `-reference`는 건너뜀 | `src/core/register/data/` | `src/core/engine/driver.cpp` `TranslateDocument` `reg::Retrofit(` |
| postfilter | 모든 행 적용 | 출력 줄 | 없음 | Ehnd 폴더의 postfilter | `src/core/engine/driver.cpp` `TranslateDocument` `filter::Stage::Post` |

`local`부터 `global-last`까지는 `Flush`가 부르는 `CleanupSelectors` 안에서, `senses`와 `scorers`는 `Select`
안에서 돕니다. 단어는 왼쪽부터 처리하므로, 한 단어의 `global-last`가 끝난 뒤에 다음 단어의 `local`이 돕니다.

### 스위치

- `-reference`는 `word::Improvements`와 `register`를 모두 끕니다. 그래도 `constraints`, 텍스트 패스, `senses`는 돕니다.
- `-raw`(텍스트 경로가 아님)는 `constraints`, `voice`, `senses`~`packs`, `restore`, `register`를 건너뜁니다.
- `-no-cg`는 `cg-lite`의 손 규칙을, `-cg-learned`는 학습 규칙을 켭니다. `-misread`는 `misread`를 켭니다.

### 호출과 문서

한 번의 번역 호출(`Session::Translate`, `hdTrans_Translate`, `J2K_*`, 웹의 `translate`, CLI 한 번)에 넣은 줄이 한 문서입니다.
이름 등록부, 친족 호칭의 성별, 「」로 대사를 쓰는 문서의 서술(그 밖의 줄은 평서 `-다`), 이어지는 노랫말 줄, 문서 의미 기억,
여러 줄에 걸친 「」는 그 호출의 줄에서만 정합니다. 앞선 호출의 것은 남지 않으므로 같은 줄은 어느 세션, 어느 순서에서도
같은 번역이 됩니다. 게임처럼 줄을 하나씩 넣으면 줄마다 따로 읽힙니다. `Reset`, `hdTrans_Reset`, `reset()`은 엔진의 지금
사전을 가져올 뿐 번역을 바꾸지 않습니다.

## 파일 위치와 순서

순서가 파일 안의 위치에 달려 있지 않게 합니다.

- 지역 규칙은 사전 `rule` 열의 번호로 찾고, 본문은 그 행의 `ctx` 열입니다.
- 전역 규칙은 `G` 다음의 **PRIO**로 정렬해 돕니다(오름차순, 같은 PRIO는 읽은 순서이고 `hdrulec lint`가
  `-Wprio-tie`로 알림). 지금 규칙은 1, 2와 10, 20, …, 360이고, 새 규칙은 사이 번호를 씁니다.
- `senses.tsv`는 키마다 파일 순서의 첫 행이 이깁니다. 파일이 하나뿐이라 지금은 PRIO가 없습니다. 나누게 되면 PRIO를 둡니다.
- `cg_rules.tsv`, `cg_learned.tsv`는 첫 열 tier가 순서입니다. 같은 tier 안에서는 고정점까지 돌리므로 대개 순서가 상관없지만,
  SELECT끼리 부딪치면 파일 순서가 이깁니다.
- `cleanup.tsv`는 위치마다 파일 순서의 첫 행이 이깁니다.
- `constructions.tsv`의 가족은 `after`로 이름한 패스나 가족 바로 뒤에 돌고, 가족 안의 행은 PRIO 순입니다.

엔진은 사전을 불러올 때 `ctx` 열의 본문과 빌드에 들어간 전역 규칙(`cmake/builtin_tables.cmake`)으로 규칙 프로그램을
조립합니다. `ctest`의 `rules.identity`는 그 프로그램(`hdtrans --dump-rules`)을 실행 순서로 덤프한 것의 해시가
`src/core/rules/program.hash`와 같은지 봅니다. 규칙을 일부러 바꿨으면 `hdrulec dump build/rules.txt -hash`로
해시를 새로 씁니다([rule-compiler.md](rule-compiler.md#사용법)).

## 누가 정했는지: `-why`

`-why`의 `WHY` 줄 끝에 네 열이 있습니다: 단어 후보를 정한 층과 규칙, 어미를 정한 층과 규칙.

```
WHY  1  0  0  0:0  だが  1  LocalRule  285  하지만  -1  None  -1  local  L285
```

- 층 이름은 위 표의 이름입니다(`local`, `global-stem`, `global-last`, `defaults`, `local-ending`,
  `global-ending`, `ending-defaults`, `senses`, `rewrites`, `scorers`, `misread`, `cg-lite`, `packs`, `fixed`).
- 규칙은 `L번호`(지역) 또는 `파일:GPRIO`(전역), 의미 표는 `senses.tsv:행`, 구문 표는 `cx:가족:PRIO`, 팩 행은 `pack:id`입니다.
- `fixed`는 단어 선택 전에 번역이 정해진 구간입니다. `sound` 층(`masked.tsv`, `sound.tsv`, `phrases.tsv`)이나
  `protect` 층이 한국어로 바꾼 부분과 `constraints`가 더한 이름 항목이 여기에 듭니다.
- 구문 표의 행이 정한 번역은 `rewrites`와 그 행의 id(`cx:return-to:10`)로 보입니다
  ([rule-language.md](rule-language.md#구문-표-constructionstsv)).
- 기록하지 않는 층: `constraints`(추가한 이름 항목은 `fixed`), 찾기의 거르기, `voice`(남긴 후보 가운데 고른 층이
  보임), `rewrites`의 C++ 패스 이름, `cg-lite`의 행, `compose`와 그 뒤(`register` 포함). 이 층들은 출력 텍스트를 바꿔도 `WHY`에 이름이 없습니다.
- `defaults`로 고른 후보의 번역을 규칙의 대입이 바꿨으면 그 규칙이 보입니다(`CandidateChoice`).

## 층을 나눌 때의 한계

- 층 사이의 결합은 그대로 있습니다. 예: `local`이 참이면 그 대입으로 이웃 단어의 번역을 바꾸고,
  `global-last`는 앞 층의 선택을 덮고, `rewrites`는 그 뒤에서 다시 덮습니다. 표는 이것을 보이게 할 뿐 없애지 않습니다.
- 층의 순서를 바꾸는 것은 모든 번역에 걸친 변경입니다. 전체 회귀(예제, 웹소설 51장, `rules.cases.themes`)가 필요합니다.
- 층을 너무 잘게 나누면 순서와 상호작용을 따라가기 어렵습니다. 새 층은 의미(처음 참, 모두 적용, 고정점)가
  다를 때만 둡니다.
- 층 안의 우선순위는 여전히 필요합니다: 사전 순서(후보), PRIO(전역 규칙, 구문 표의 행), 파일 순서(senses, cleanup),
  tier(CG).

## 층별 테스트

- 규칙마다: `tests/rules/cases/`의 행(`rule` 열이 그 규칙을 이름한 사전 행의 id, [rule-language.md](rule-language.md#규칙-테스트)).
- 층마다: 같은 표의 `layer` 열. `tools/rule-cases.sh`(ctest `rules.cases.themes`)는 실패를 층별로 묶어
  보여 주므로, 한 층을 고쳐 다른 층의 행이 깨지면 그렇게 보입니다.
