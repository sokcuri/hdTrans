# 부착 정보 제공자

[팩](packs.md#부착-정보)의 조건은 단어가 어느 절에 있는지, 어느 술어의 논항인지, 그 절의 자질이 무엇인지를 읽을 수
있습니다. 엔진은 이 정보를 규칙 분석기로 만듭니다(`src/core/attach/`). 단어 분절은 엔진의 것을 그대로 쓰고, 분석기는
부착 정보만 사슬의 단어에 붙입니다.

## 하는 일

청크의 원문을 다음 순서로 읽습니다. 팩 층이 그 청크의 단어에 처음 부착 정보를 물을 때 한 번 읽습니다.

1. 규칙 분석기가 단단위 형태소로 나눕니다. 비용은 학습한 값이 아니고 명시한 규칙입니다. 연결 규칙은 금지할 이웃을
   정하고(終止形 뒤 終助詞, 連体形 뒤 準体助詞), 선호 규칙은 순위대로 비교합니다(미지어, 파생형, 표기 변이, 단위 수,
   동점 처리).
2. 어절을 묶고 각 어절이 걸리는 곳을 정합니다(`data/heads.tsv`, 가장 구체적인 행이 이김).
3. 절을 접고 자질과 논항을 붙입니다(`data/aux.tsv`, `data/bareobj.tsv`).
4. 생략된 주어의 부류를 제약 규칙으로 정합니다(`data/zero.tsv`, `data/persons.tsv`). 경어, 수수, 명령·의지·희망,
   인칭 대명사의 화제가 단서입니다. 맞는 행이 없으면 `none`입니다.
5. 결과를 사슬의 단어에 옮깁니다. 단어의 원문 위치가 형태소와 겹치면 그 단어가 형태소를 가집니다.

단어마다 절, 술어 여부, 논항이면 그 절·술어·역할·쓰임(how), 꾸미는 명사를 줍니다. 절마다 술어 단어, 부모 절, 자질,
논항을 줍니다.

| 자질 | 값 |
|---|---|
| `link` | `final` `te` `tara` `to` `ba` `nara` `node` `kara` `noni` `tame` `you-ni` `nagara` `tari` `adn` `nom` `quote` `ga` `temo` `shi` `zu` `renyo` `time` |
| `tense` | `nonpast` `past` |
| `aspect` | `none` `te-iru` `te-aru` `te-shimau` `te-oku` `te-kuru` `te-iku` `te-miru` |
| `pol` | `pos` `neg` |
| `negfn` | `none` `plain` `rhetorical` `prohibitive` `obligation` `potential-neg` `archaic-vol` `double` |
| `voice` | `active` `rare` `passive` `potential` `honorific` `spontaneous` `causative` `caus-pass` |
| `bene` | `none` `give-out` `give-in` `receive` |
| `force` | `decl` `q` `imp` `prohib` `prop` `excl` `request` |
| `polite` | `plain` `desu-masu` `sonkei` `kenjo` `gozaimasu` |
| `vol` | `+` `-`: 수동·가능·자발은 `-`, 의지·희망·명령은 `+`, 그 밖의 동사와 する 명사는 `data/volition.tsv`에 없으면 `+` |
| `subj` | `overt` `speaker` `addressee` `third` `generic` `none` |

논항의 쓰임은 `marked`(조사), `topic`(は), `relhead`(연체절이 꾸미는 명사), `omitted`, `bare`(조사 없는 목적어:
迷惑かけた)입니다. 주어가 생략된 절은 생략 주어 논항을 하나 가지며, 그 부류는 `subj`가 `speaker`, `addressee`,
`third`이면 `@person`입니다.

## 언제 도는가

다음이 모두 참일 때만 분석기를 읽어 들이고 씁니다.

- 불러온 팩의 어느 행이 부착 정보를 읽는 원자를 가짐: `how`, `X.how`, `cl.*`, `main.*`
  (`dict::ReadsAttachment`).
- 사전 폴더 가운데 하나에 `analysis.bin`이 있음(뒤 폴더가 앞 폴더를 이김). 폴더가 아닌 스냅샷 파일(`.hdsnap`)만
  쓰면 제공자가 없습니다.
- `attachment`가 `auto`(기본)임. 기준 모드(`--reference`)에서는 쓰지 않습니다.

그런 행이 없으면 분석기를 읽지 않으므로 출력이 바이트 단위로 같습니다. 그런 행이 있으면 다른 팩 행의 논항 칸, `head`,
`pred`, `mod`도 분석기의 정보를 읽습니다([팩](packs.md#부착-정보)).

4096바이트가 넘는 청크와 절이 없는 청크에는 부착 정보가 없습니다. 그러면 팩 층은 가까운 문맥을 읽습니다.

## 끄기

| 방법 | 값 |
|---|---|
| CLI | `--attachment auto` 또는 `off` |
| 설정 파일 | `attachment: auto` 또는 `off` |
| 환경 변수 | `HDTRANS_ATTACHMENT=auto` 또는 `off` (설정 파일보다 앞섬) |
| C++ | `EngineOptions::attachment`(`Off`면 읽어 들이지 않음), `Session::SetAttachment(Attachment)` |

C API와 J2K 함수는 설정 파일과 환경 변수를 따릅니다.

## 비용

40,000줄 게임 대사 표본에서 잰 값입니다. 모든 동사와 명사가 부착 정보를 묻는 팩(가장 나쁜 경우)을 기준으로 합니다.

| 항목 | 값 |
|---|---|
| `dicts/analysis.bin` | 9.4 MB, gzip 2.4 MB |
| 메모리 | 약 +45 MB(+16%) |
| 번역 시간 | +9~10% (CPU 시간) |

부착 정보를 읽는 행이 없으면 비용이 없습니다.

## analysis.bin 만들기

`analysis.bin`은 규칙 분석기의 모델 폴더를 컴파일한 것입니다. 모델 폴더는 UniDic 어휘 목록에 연결 규칙
(`ja/conn.tsv`)과 선호 규칙(`ja/prefs.tsv`), 부류(`ja/classes.tsv`), 파생형(`ja/derive.tsv`), 제약
(`ja/constraints.tsv`)을 적용해 만듭니다. 비용은 규칙 순위를 자릿수로 쓴 정수입니다.

```sh
hdtrans --compile-analysis MODEL_DIR --out dicts/analysis.bin
```

`MODEL_DIR`에는 `udpos.tsv`, `conj.tsv`, `pos-map.tsv`, `model/`(classes, templates, lexicon, connrules,
chardef, unk, params), `ja/derive.tsv`, `ja/constraints.tsv`가 있어야 합니다. 컴파일할 때 단어 읽기는 분석이 읽는 첫
글자만 남깁니다. 분석기 규칙의 원본과 모델 빌드 도구는 분석기 작업 브랜치(`poc/v08-analyzer`의 `poc08/analyzer/`)에
있습니다.

구조 단계의 표(`src/core/attach/data/*.tsv`)는 엔진에 컴파일되어 들어갑니다.

| 파일 | 내용 |
|---|---|
| `aux.tsv` | 기능 형태소가 절에 주는 자질과 역할 |
| `ud.tsv`, `forms.tsv` | 품사와 활용형의 갈래 |
| `heads.tsv` | 어절이 걸리는 곳의 규칙과 절 연결의 범위(A < B < C < D) |
| `bareobj.tsv` | 조사 없는 명사를 목적어로 받지 않는 동사 |
| `zero.tsv`, `persons.tsv` | 생략 주어의 제약 규칙과 인칭어 |
| `volition.tsv` | 일부러 하지 않는 술어(`vol=-`) |

## 시험

- `ctest -R cli.attach`: `tests/packs/attach`의 행이 부착 정보가 있을 때만 맞고, `--attachment off`와
  `HDTRANS_ATTACHMENT=off`에서는 맞지 않으며, 부착 정보를 읽는 행이 없으면 출력이 같은지 봅니다.
- `ctest -R rules.packs.attach`: `tests/packs/attach.cases.tsv`의 고침·유지 사례.
