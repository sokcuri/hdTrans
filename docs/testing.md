# 테스트

## ctest

`ctest --preset <프리셋>`이 실행하는 테스트:

| 이름 | 내용 |
|---|---|
| `unit` | 단위 테스트 `hdtrans_tests` |
| `tools.selfbase` | `tools/snapshot.sh`와 `compare.sh`를 작은 사전으로 두 번 실행해 결과가 같은지 확인 (Unix) |
| `dicts.ids` | `dicts/`의 열 파일마다 키가 다른 두 행이 같은 `id`를 갖지 않는지 확인. 다른 표기를 모은 `words-variants.tsv`는 제외 (Unix) |
| `dicts.classes` | `src/core`가 부르는 낱말 부류마다 `classes.tsv`에 설명 줄과 행이 있고, 표의 부류마다 부르는 코드가 있는지 확인 |
| `tools.<이름>` | `tools/`의 Python 작업 도구의 `--self-test`와 `tools/pick.sh`를 작은 저장소에서 돌리는 `tests/pick.sh` (Unix, Python 3) |
| `tools.surface-budget` | `src/core`의 일본어, 한국어 문자열이 `tools/surface-budget.tsv`의 상한을 넘지 않는지 확인 ([아래](#표면-문자열-예산-surface-budgetpy)). `.self-test`는 도구 자체 시험 (Unix, Python 3) |
| `examples` | `examples/` 번역 확인. `HDTRANS_DICTS`가 없으면 건너뜀 |
| `rules.cases.themes` | `tests/rules/cases/<주제>.tsv`의 규칙 사례를 `dicts/`로 번역해 확인 ([아래](#규칙-사례-testsrulescases)) |
| `rules.cases.speech-<단계>` | `tests/rules/speech-soft/`, `speech-strict/`의 사례를 그 말투 단계로 확인 |
| `rules.drops` | 규칙 사례와 `tests/rules/drops/`의 입력에서 낱말이 번역에서 사라지지 않는지 확인 ([아래](#낱말-빠짐-dropspy)). `tools.drops`는 도구 자체 시험 |
| `sets.drops` | 측정 세트에서 글이 빠지는 입력 수가 `tools/drops-budget.tsv`와 같은지 확인 ([아래](#낱말-빠짐-dropspy)). 세트 변수가 없으면 건너뜀 |
| `testdata` | 테스트 데이터의 기대 출력과 비교 ([테스트 데이터](#테스트-데이터-비공개-저장소)). `HDTRANS_TESTDATA`가 없으면 건너뜀 |
| `host.admin_priority` | C API에서 초기화, 다시 읽기, 종료가 대기 중인 번역보다 먼저 처리되는지 확인 |
| `host.j2k` | `J2KEngine.dll`과 `hdTransEngine.dll`의 초기화, 종료와 동시 번역 확인 (Windows) |

```sh
HDTRANS_DICTS=$PWD/dicts ctest --preset mac
```

## 단위 테스트

`tests/unit/`에 있으며, `src/core/`와 같은 단계별 폴더(`dict/`, `tokenizer/`, `word/` 등)로 나뉩니다. 프레임워크 없이
`tests/unit/hdtest.h`의 `HD_TEST`, `HD_CHECK`, `HD_REQUIRE`, `HD_SKIP`을 씁니다. 실제 입력을 쓰는 표
형식 테스트이며, 픽스처는 `tests/unit/**/testdata/`의 작은 파일입니다.

```sh
build/mac/hdtrans_tests            # 전체
build/mac/hdtrans_tests userdict   # 이름에 userdict가 들어간 테스트만
```

러너는 `HOME`, `LOCALAPPDATA`, `XDG_CACHE_HOME`을 임시 폴더로 바꾸고, `HDTRANS_CACHE_DIR`가 없으면 임시
캐시 폴더를 지정합니다. 실행 중 사용자 캐시 폴더가 바뀌면 실패합니다. `tests/runner.sh`는 이 러너
동작 자체를 확인하며, Windows가 아닌 빌드에서 ctest `tests.runner`로 돕니다.

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_REQUIRE_ALL=1` | 건너뛴 테스트가 하나라도 있으면 실패 |
| `HDTRANS_TEST_EXCLUDE=a,b` | 쉼표로 구분한 이름의 테스트를 건너뜀 |
| `HDTRANS_CACHE_DIR` | 테스트가 쓸 캐시 폴더 (절대 경로) |

## 규칙 사례 (tests/rules/cases/)

주제마다 표 하나(`tests/rules/cases/<주제>.tsv`)로 규칙의 단어 선택을 확인합니다. 열은 `rule input expected kind layer`이고
입력 한 줄을 한 세션으로 번역합니다(`-each -why`). 실패는 `layer`별로 묶어 보입니다.

- `rule`: 사례가 겨냥하는 규칙. 지역 규칙은 그 규칙을 이름한 사전 행의 id(`dict:jisjk:848`), 전역 규칙은 PRIO(`G330`),
  그 밖의 단계는 `<단계>:<이름>`(`sense:お盆`, `cx:noun-to:10`)
- `input`: 직접 쓴 짧은 일본어 문장
- `expected`: 번역 전체, 또는 `rule:dict:<id>=<번역>`(그 줄의 어떤 단어를 그 행의 지역 규칙이 그 번역으로 골랐음, `-why` 기준),
  `!rule:dict:<id>`(그 규칙이 아무것도 고르지 않음)
- `kind`: `fix`(고친 규칙이 이제 맞게 고르는 문장), `keep`(고치기 전 선택이 맞아서 그대로 남아야 하는 문장),
  `current`(지금 동작을 그대로 적어 둔 문장)
- `layer`: 그 규칙이 도는 실행 층([pipeline-layers.md](pipeline-layers.md)의 `local`, `local-ending`, `global-stem`,
  `global-last`, `global-ending`, `senses` 등)

머리줄 앞의 `#` 줄은 행이 아닙니다. 표가 지키게 하는 [번역 규칙](../spec/README.md)의 id를 `# 규칙: NUM-1, NUM-3`처럼 적습니다.

```sh
tools/rule-cases.sh build/mac/hdtrans dicts                                  # 다르면 규칙, 입력, 기대값, 실제값을 보이고 실패
tools/rule-cases.sh build/mac/hdtrans dicts tests/rules/cases/<주제>.tsv --accept   # 그 표 current 행의 번역 전체 기대값을 새 출력으로 바꿈
```

규칙을 고칠 때는 `fix` 행과 `keep` 행을 하나 이상씩 더하고, 커밋 전에 표 전체와 `examples/`를 돌립니다.

## 예제 문서 (examples/)

`examples/`에는 짧은 일본어 문서와 번역 결과가 있습니다.

- `<case>.ja.txt`: 첫 줄은 사례를 설명하는 `#` 줄, 그다음은 한 문서로 번역할 입력
- `<case>.ko.txt`: 같은 `#` 줄, 그다음은 검토를 마친 현재 엔진 출력

```sh
tools/examples.sh build/mac/hdtrans dicts            # *.ko.txt와 다르면 실패
tools/examples.sh build/mac/hdtrans dicts --accept   # 새 출력을 *.ko.txt로 받아들임
```

출력은 `build/examples/`에 생깁니다. 사전 인자가 비어 있으면 77로 끝나며, ctest는 이를 건너뜀으로
처리합니다.

## 표면 문자열 예산 (surface-budget.py)

`tools/surface-budget.py`는 `src/core`의 `.cpp`, `.h`마다 엔진 코드에 직접 쓴 일본어와 한국어를 셉니다.
첫 줄이 `// Generated ...; do not edit`인 생성 파일만 빼고, 이름이 `_gen`이어도 손으로 고치는 파일은 셉니다.
열의 뜻은 도구 첫머리에 있습니다. 문자열, 낱말 목록, 표면형 비교 조건(`In(`, `OneOf(`, `Starts(` 같은 도우미
호출, 여러 줄에 걸친 비교, `case U'…'` 포함)과 그 우회(가나 문자 리터럴, `\u` 이스케이프, 가나, 한자, 한글
범위의 16진 상수와 범위 경계로 쓴 10진 상수, 헤더 상수 사용)를 셉니다. 비트 마스크와 BOM은 세지 않습니다.
코드와 분류 비교 조건(`cond_code`)은 상한 없이 셉니다.

- `tools/surface-budget.tsv`가 파일마다 상한입니다. 표에 없는 파일의 상한은 0이라서, 새 파일에 문자열을
  쓰면 실패합니다. 파일 이름을 바꾸거나 나누는 것도 새 파일의 상한을 올리는 일입니다.
- `--check`(기본)는 상한을 넘는 수를 보이고 실패합니다. 낮출 수 있는 상한이 있으면 그 수와 `--update`
  명령을 알립니다. `--base REF`를 주면 REF 뒤에 바뀐 파일의 수가 상한보다 낮을 때도 실패합니다. 남은
  여유를 표에 적어 두지 않으면 나중에 트레일러 없이 다시 채울 수 있기 때문입니다.
- `--update`는 지금 수를 상한으로 씁니다. 낮추기와 지운 파일의 행 지우기는 그냥 되지만, 올리기는
  develop과의 merge base(`--base REF`) 뒤의 커밋 하나에 `Surface-Budget: <이유>` 트레일러가 있을 때만
  씁니다. 트레일러 하나가 그 범위의 모든 파일에 통합니다. 올려야 하면 코드를 그 트레일러와 함께
  커밋하고, `--update`를 돌린 뒤 표를 커밋합니다.
- `--report`는 열마다 합계, 표면형 비교와 코드 비교 조건의 비(`surface:code`), 문자열이 많은 파일을
  보입니다. 구조를 바꾼 뒤 수가 줄었는지 볼 때 씁니다.
- CI의 `release-tools` 작업은 `--self-test`와 `--check`를 돌립니다. PR에서는 PR이 갈라진 기준 커밋
  (`github.event.pull_request.base.sha`)으로 `--check --base`를 돌려, 표의 수가 오르거나 행이 늘었는데
  PR의 어느 커밋에도 트레일러가 없을 때와 PR이 바꾼 파일에 적지 않은 여유가 있을 때 실패합니다.

## 낱말 빠짐 (drops.py)

`tools/drops.py`는 단어 단계가 한 글자씩 읽고 번역을 비운 글자(가나, 한자)가 둘 이상 이어져 번역에서 사라진
입력을 찾습니다. 그 글자를 홀로 번역한 것이 줄의 번역에 있으면 뒤 단계가 쓴 것이라 빠짐으로 치지 않습니다.

- ctest `rules.drops`는 규칙 사례와 `tests/rules/drops/`의 입력을 보고, `known-gaps.txt`의 입력만 봐줍니다.
  고친 입력은 그 목록에서 지웁니다.
- `--sets`는 `measure.sh`의 네 세트(변수가 있는 것만)를 보고 세트마다 입력 수와 글이 빠지는 입력 수를 씁니다.
  ctest `sets.drops`는 그 수가 `tools/drops-budget.tsv`의 행과 같아야 통과합니다. 빠지는 입력을 줄인 변경은
  `--update`로 행을 낮추고, 늘린 변경은 고칩니다. 입력 수가 행과 다른 세트는 다른 데이터라서 보지 않습니다.

```sh
tools/drops.py build/mac/hdtrans dicts --sets --budget tools/drops-budget.tsv [--update]
```

## 패스 실험 빌드

단어 단계의 문장 패스(`src/core/word/flush.cpp`)가 순서에 얼마나 민감한지, 테스트가 패스를 얼마나 붙잡는지
재는 빌드입니다. CMake 옵션 `HDTRANS_PASS_EXPERIMENTS`(기본 꺼짐)를 켠 빌드만 아래 두 변수를 읽고, 릴리스와
CI 빌드에는 이 코드가 들어가지 않습니다. 켠 빌드도 변수가 없으면 출력이 같습니다.

```sh
cmake --preset mac -B build/mac-passexp -DHDTRANS_PASS_EXPERIMENTS=ON
cmake --build build/mac-passexp --parallel --target hdtrans_cli
HDTRANS_PASS_SKIP=QuoteTo build/mac-passexp/hdtrans -dicts dicts -in in.txt  # 그 패스를 빼고 번역
HDTRANS_PASS_SKIP=cx:noun-to:20 build/mac-passexp/hdtrans -dicts dicts -in in.txt  # 구문 표의 행 하나를 빼고 번역
HDTRANS_PASS_SHUFFLE=7 build/mac-passexp/hdtrans -dicts dicts -in in.txt     # 씨앗 7로 패스 순서를 섞어 번역
```

- `HDTRANS_PASS_SKIP`: 뺄 패스 이름, 쉼표로 구분하고 앞뒤 공백은 무시. 이름은 `flush.cpp`의 `Run` 호출과
  `kTextPasses`에 있고, 구문 표의 가족 이름이나 행 id(`cx:return-to:10`)도 됩니다. 모르는 이름은 표준 오류에 알립니다.
- `HDTRANS_PASS_SHUFFLE`: 10진 씨앗. `kTextPasses`(`Select`부터 `EmphaticMo`까지)의 순서만 섞고(구문 표의 가족은
  자기 패스를 따라감), 그 앞뒤 패스는 그대로 둡니다. 같은 씨앗은 같은 순서입니다. 10진수가 아니면 표준 오류에 알리고
  멈춥니다(abort).
- 두 변수는 프로세스에서 처음 번역할 때 한 번 읽습니다.

`tools/pass-mutation.py [-c CLI] [-d DICTS] [-j JOBS] [-t SECONDS] [PASS...]`는 이 빌드(기본
`build/<프리셋>-passexp/hdtrans`, 사전 기본 `dicts/`)로 패스를 하나씩 빼고 규칙 사례 세 묶음
(`cases/`, `speech-soft/`, `speech-strict/`)과 `examples/`를 돌려, 패스마다 묶음별 실패 수와 어느 테스트도
잡지 못한 패스를 보입니다. 구문 표의 행도 하나씩 뺍니다. 실행마다 시간 제한(기본 900초)이 있고, CLI가 죽거나(`crash`) 시간을 넘긴
(`timeout`) 묶음은 그 패스를 잡은 것으로 칩니다. 기본으로 꺼진 개선 옵션(`src/core/word/analyze.h`)만 돌리는
패스(`FilterMisreads`)는 `not exercised`로 따로 보입니다.

## 한 입력 살펴보기: 단계 기록 (-stages)

```sh
printf '%s' '学生です。' | build/mac/hdtrans -dicts dicts -stages stages
```

입력 하나가 각 파이프라인 단계를 지나며 남긴 기록을 `stages/input.<TAG>.got.tsv`에 씁니다.
`#LINE` 머리줄 아래에 이스케이프된 TSV 레코드가 있습니다. `HDTRANS_DRIVER_DUMP=stages`로도 켤 수
있습니다. `-raw`를 붙이면 단어 파이프라인만, `-each`를 붙이면 줄마다 새 세션으로 기록합니다.
`-why`는 단어 선택과 shadow 점수를 표준 오류로 씁니다([api.md](api.md#명령줄-hdtrans)).

오독 필터의 기록은 문장의 선택이 끝난 뒤(S와 같은 시점)에 남습니다. `MI`는 체인의 단어마다 위치, 표면형,
앞뒤 글자, 줄 첫머리 여부, 선택과 출처, `MC`는 단어의 후보(W)와 어미 후보(E)마다 품사, 의미, 문법 코드,
사용 가능 여부, 한국어와 사전형, `MS`는 소리 읽기, `MF`는 결정의 후보마다 내장 모델 점수와 특징입니다.
`tools/misread/check.py`는 MI, MC, MS로 같은 특징을 다시 만들어 MF와 비교합니다([misread-classifier.md](misread-classifier.md)).

## 자체 기준선

```sh
tools/snapshot.sh <사전 폴더> <이름>                      # 빌드한 CLI의 출력을 기록
tools/compare.sh <runA> <runB>                            # 두 기록의 차이
tools/explain.sh <binA> <binB> <사전 폴더> '学生です。'   # 처음 달라지는 단계와 레코드
```

- `snapshot.sh`는 빌드하지 않고, 이미 빌드한 CLI를 기본 번역 설정과 기록마다 새 캐시로 실행합니다.
  같은 이름의 기록이 있으면 거부합니다.
- 기록은 `${HDTRANS_RUNS:-../hdtrans-data/cpp-runs}` 아래의 바꾸지 않는 폴더입니다. 입력과 출력의 복사본,
  커밋, 바이너리, 사전의 SHA-256, 세트별 시간(`sets.tsv`)이 들어 있습니다.
- 입력: `HDTRANS_DATA`(기본 `../hdtrans-data`), `HDTRANS_INPUTS`(기본 `$HDTRANS_DATA/hdtrans-golden`,
  최상위 `.in` 파일만). `*.raw.in`은 `-raw`, `*.each.in`은 `-each`, 나머지는 문서로 번역합니다.
  `lyrics/cases.tsv`의 가사는 곡별로 묶이고, `syosetu/*.ja.txt`는 문서로 번역합니다.
- `HDTRANS_BIN`: 사용할 CLI (기본 `build/mac/hdtrans`).
- `compare.sh`는 기록 이름이나 폴더 경로를 받아 세트, 줄, 입력, A, B와 세트별, 전체 개수를 출력합니다.
- `compare.sh`와 `explain.sh`의 종료 코드: 0 같음, 1 다름, 2 잘못된 입력.

## 테스트 데이터

엔드투엔드 테스트의 입력과 기대 출력은 이 저장소 밖에 두고 `HDTRANS_TESTDATA`로 찾습니다. 데이터가 없으면
아래 테스트는 건너뜁니다.

- 출처마다 폴더 하나(`syosetu/`, 앞으로 `corpus/`, `lyrics/`, `eroge/` 등)이고, 하위 폴더는 없습니다.
- `<set>.<NNN>.ja.txt`: 첫 줄은 작품, 작가나 권리자, 원문 URL을 적은 `#` 줄, 그다음은 입력
- `<set>.<NNN>.ko.txt`: 같은 `#` 줄, 그다음은 입력 전체를 한 문서로 기본 설정으로 번역한 엔진 출력

```sh
export HDTRANS_TESTDATA=/path/to/testdata                            # 테스트 데이터 (절대 경로)
tools/e2e.sh --expected "$HDTRANS_TESTDATA" build/mac/hdtrans dicts            # 기대 출력과 비교
tools/e2e.sh --expected "$HDTRANS_TESTDATA" build/mac/hdtrans dicts --accept   # 새 출력을 받아들임
tools/e2e.sh <cliA|runA> build/mac/hdtrans dicts                    # 두 빌드 비교
ctest --preset mac -R testdata                                       # --expected를 ctest로
```

- `e2e.sh --expected DATA CLI DICTS [--accept]`: `DATA/*/*.ja.txt`를 첫 줄을 빼고 한 문서로 번역해
  `build/e2e/<폴더>/<name>.ko.txt`에 쓰고, `DATA`의 `.ko.txt`(A)와 비교합니다. 달라진 문서마다 최대 20줄,
  전체 최대 200줄의 변경(파일 줄 번호, A, B)을 출력하고, 마지막 줄에 문서, 줄 개수를 씁니다. `.ko.txt`가
  없는 문서는 모든 줄이 바뀐 것으로 셉니다. `--accept`는 달라진 `.ko.txt`를 새 출력으로 덮어씁니다.
- `e2e.sh A B DICTS [DATA]`: 같은 문서를 두 빌드로 번역해 `build/e2e/<폴더>/`(B)와
  `build/e2e/base/<폴더>/`(A)에 쓰고 비교합니다. A에는 CLI 대신 `snapshot.sh` 기록 폴더를 줄 수 있으며,
  그러면 입력이 같은 문서 세트를 A로 씁니다. `HDTRANS_DICTS_A`는 A에 다른 사전 폴더를 줍니다.
- `DATA`가 비었거나 없으면 `HDTRANS_TESTDATA`를 씁니다. 둘 다 없으면 77로 끝나며, ctest의 `testdata`는
  이를 건너뜀으로 처리합니다. `testdata`는 저장소의 `dicts/`를 씁니다.
- 종료 코드: 0 같음 또는 받아들임, 1 바뀜, 2 잘못된 인자, 데이터, CLI 실패, 비교한 문서 없음, 77 테스트
  데이터 없음.

### 번역이 바뀌었을 때

`testdata`는 `main`의 출력을 기대값으로 삼으므로, 작업 브랜치에서는 기대값과 다를 수 있습니다. 내 변경의 영향은
기대값이 아니라 고정한 기준 빌드와 비교해 봅니다([변경 판정](#변경-판정-e2e--모델-판정)). `build/e2e/`의 출력은
저장소에 커밋되지 않지만 비공개 본문의 번역이므로 다 쓰면 지웁니다.

번역 동작을 바꾸는 변경은 이유를 적은 별도 커밋으로 만듭니다. 그 커밋이 `main`에 들어가면, 그 커밋으로
빌드한 CLI와 `dicts/`로 `--accept`를 실행하고, 테스트 데이터 저장소에 엔진 커밋을 적어 커밋합니다.

### 챕터 더하기 (syosetu)

```sh
printf '# <제목> <화> (작가: <작가>) <URL>\n' > "$HDTRANS_TESTDATA/syosetu/syosetu.052.ja.txt"
tools/fetch-syosetu.sh                  # 첫 줄만 있는 파일에 본문을 받음
tools/e2e.sh --expected "$HDTRANS_TESTDATA" build/mac/hdtrans dicts --accept
```

`fetch-syosetu.sh [DIR]`(기본 `$HDTRANS_TESTDATA/syosetu`)는 첫 줄만 있는 `.ja.txt`마다 그 줄 끝의
小説家になろう URL에서 챕터를 받아, 챕터 제목과 단락마다 한 줄(루비 읽기 제거)을 덧붙입니다. 1초에 한 번
요청하고, 받지 못한 챕터는 보고하고 그대로 둡니다. 작품 제목과 작가는 小説家になろう API
(`https://api.syosetu.com/novelapi/api/?out=json&of=t-w&ncode=<ncode>`)의 `title`, `writer`입니다.

### CI

Build(`.github/workflows/build.yml`)의 `linux` job이 방금 빌드한 CLI로 `--expected` 비교를 합니다. gate가
matrix를 건너뛴 run(이미 테스트한 트리의 `main` push 등)에는 이 비교도 없습니다([build.md](build.md#ci)). 테스트 데이터는 Actions 비밀 `HDTRANS_TESTDATA_KEY`(테스트 데이터 저장소의 읽기 전용 배포 키)로
받습니다. 비밀을 쓸 수 없는 실행(포크의 풀 리퀘스트)은 알림만 남기고 건너뜁니다. 작업 요약에는 개수만,
로그에는 보고의 처음 몇 줄만 남깁니다. 종료 코드 1(바뀜)은 실패가 아니고 2만 실패입니다.

## 변경 판정 (e2e + 모델 판정)

챕터 본문은 위 [비공개 테스트 데이터 저장소](#테스트-데이터-비공개-저장소)에 있습니다. 본문, 번역, 판정 파일은
이 저장소에 커밋하지 않습니다. 아래 도구는 Python 3 표준 라이브러리만 씁니다.

1. 비교할 **커밋을 먼저 고정**하고 별도 git worktree에서 base를 빌드합니다. 움직이는 `main`과 비교하지 않습니다.
   CLI뿐 아니라 사전도 그 커밋의 것을 씁니다. 아래 명령은 저장소 루트에서 시작합니다.

   ```sh
   BASE_REF=$(git rev-parse HEAD)  # 비교 기준 커밋 해시로 바꿔도 됨
   BASE="$(dirname "$PWD")/hdtrans-base"  # 아직 없는 형제 폴더
   git worktree add --detach "$BASE" "$BASE_REF"
   (cd "$BASE" && export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer &&
     cmake --preset mac && cmake --build build/mac)
   export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
   cmake --preset mac
   cmake --build build/mac
   ```

2. `e2e-pairs.py`에 base CLI, 사전, head CLI, 사전, 출력 파일, 입력 폴더들을 차례로 줍니다.
   `RUN`은 저장소 밖의 결과 폴더입니다. 비공개 저장소의 `syosetu/`가 개발 세트(고칠 때 보는 챕터), `heldout/`이
   held-out 세트(고칠 때 보지 않는 챕터, 다른 작품)입니다: `DEV=$HDTRANS_TESTDATA/syosetu`, `HELDOUT=$HDTRANS_TESTDATA/heldout`.
   바뀐 줄이 없으면 판정 출력은 빈 파일이고 다음 단계도 대상 없이 끝납니다.

   ```sh
   mkdir -p "$RUN"
   python3 tools/e2e-pairs.py "$BASE/build/mac/hdtrans" "$BASE/dicts" \
     build/mac/hdtrans dicts "$RUN/dev.pairs.tsv" "$DEV"
   python3 tools/e2e-pairs.py "$BASE/build/mac/hdtrans" "$BASE/dicts" \
     build/mac/hdtrans dicts "$RUN/heldout.pairs.tsv" "$HELDOUT"
   ```

   - 각 폴더의 `.txt`를 재귀적으로 찾되 `.ko.txt`는 제외합니다. 첫 줄이 `#`로 시작하면 빼고 챕터 전체를
     한 문서로 번역합니다. CLI를 부를 때마다 새 임시 `HOME`, 캐시를 쓰므로 사용자 캐시나 base/head 캐시를 공유하지 않습니다.
   - 출력은 머리줄 없는 `key ja old new` TSV입니다. `key`는 `입력폴더이름/상대파일명:원본줄번호`이며
     줄 번호는 `#` 머리줄도 포함한 1부터 셉니다. 겹치지 않는 폴더 이름을 씁니다. 바뀐 줄만 기록합니다.
   - 모든 도구의 TSV는 탭, 따옴표가 든 필드를 CSV 방식으로 인용합니다. 직접 파싱할 때도 TSV용 CSV 리더를 씁니다.

3. 판정은 바뀐 줄을 만든 쪽이 아닌 판정자가, 어느 쪽이 새 출력인지 모르고 합니다. `judge.py`는 줄마다 X/Y 순서를
   섞어 묻습니다. Sonnet(`claude-sonnet-5-5`)으로 **모든 변경 줄**을 판정합니다.
4. Sonnet이 `worse` 또는 `same`으로 본 줄만 `gpt-6.1-sol`의 **high effort**로 다시 판정합니다.

   ```sh
   python3 tools/judge.py claude-sonnet-5-5 "$RUN/dev.sonnet.tsv" "$RUN/dev.pairs.tsv"
   awk -F '\t' '$2 == "worse" || $2 == "same" {print $1}' \
     "$RUN/dev.sonnet.tsv" > "$RUN/dev.rejudge.keys.txt"
   python3 tools/judge.py gpt-6.1-sol "$RUN/dev.gpt.tsv" "$RUN/dev.pairs.tsv" \
     --only "$RUN/dev.rejudge.keys.txt" --effort high
   ```

   - `judge.py MODEL OUT.tsv PAIRS.tsv [--only KEYS.txt] [--effort low|high] [--grade]`
     (`--grade`는 [오류율 감사](#오류율-감사-auditpy)).
     `--only`는 키를 한 줄에 하나씩 적은 파일입니다. 출력은 머리줄 없는 `key verdict model`이며
     `verdict`는 `improved`, `same`, `worse`입니다. X/Y 순서를 섞어 판정한 뒤 old/new 기준으로 돌립니다.
   - 출력에 같은 모델로 판정한 키가 있으면 건너뛰고 이어 씁니다. 입력이나 판정 설정을 바꾸면 새 출력 파일을 씁니다.
     effort는 `--effort` → `JUDGE_EFFORT` → `low` 순입니다.
   - `ANTHROPIC_BASE_URL`(기본 `https://api.anthropic.com`)과 `ANTHROPIC_AUTH_TOKEN`(Bearer) 또는
     `ANTHROPIC_API_KEY`(`x-api-key`)를 환경에서 읽습니다. 둘 다 있으면 Bearer를 씁니다. 키, 토큰을 파일에 넣지 않습니다.
     GPT 모델은 해당 모델을 제공하는 Anthropic Messages 호환 게이트웨이가 필요합니다. 그런 게이트웨이나 API 키가
     없으면 아래 블라인드 판정으로 합니다.

   - 줄이 많으면 표본을 뽑지 않고 판정 입력을 나눠(`split -l 500 PAIRS.tsv part.`) 조각마다 판정을 동시에 돌리고
     출력을 이어 붙입니다. 직접 판정한 결과도 같은 열(`key verdict judge`, 넷째 열에 메모)로 씁니다.

   **블라인드 판정 (blind.py).** API 없이 판정할 때 씁니다. 바뀐 줄을 만들지 않은 판정자 둘이 따로 블라인드 파일만 보고
   답하고, 답이 갈린 줄은 셋째 판정자가 봅니다. 판정자마다 run 폴더 밖의 새 폴더에 파일을 주고, 열쇠 파일, 판정 입력,
   run 폴더, 다른 판정자의 답, 저장소를 보거나 엔진을 돌리지 않게 합니다.

   ```sh
   J1=$(mktemp -d) J2=$(mktemp -d) J3=$(mktemp -d)
   python3 tools/blind.py make "$RUN/dev.pairs.tsv" "$RUN/dev.key.tsv" "$J1/blind.tsv"
   cp "$J1/blind.tsv" "$J2/"
   # 판정자 1은 $J1/blind.tsv를 읽고 $J1/answers.tsv에, 판정자 2는 $J2에서 같은 일을 함
   python3 tools/blind.py disagree "$J1/blind.tsv" "$J1/answers.tsv" "$J2/answers.tsv" "$J3/blind.tsv"
   # $J3/blind.tsv가 비어 있지 않으면 판정자 3이 읽고 $J3/answers.tsv를 씀
   [ -s "$J3/blind.tsv" ] || : > "$J3/answers.tsv"
   cp "$J1/answers.tsv" "$RUN/dev.answers-1.tsv"; cp "$J2/answers.tsv" "$RUN/dev.answers-2.tsv"
   cp "$J3/answers.tsv" "$RUN/dev.answers-3.tsv"
   python3 tools/blind.py final "$RUN/dev.key.tsv" "$RUN"/dev.answers-[123].tsv > "$RUN/dev.final.tsv"
   ```

   - 블라인드 파일은 `id ja X Y`입니다. `id`는 줄 번호일 뿐이고 X, Y는 줄마다 무작위로 바꾼 이전, 이후 출력입니다.
     답은 `id 답 [메모]`이고 답은 더 나은 쪽 `X`, `Y`, 또는 `same`입니다.
   - `make`는 이미 있는 열쇠 파일이나 블라인드 파일을 덮어쓰지 않습니다. 다시 만들면 답도 새로 받습니다.
   - `final`은 두 답이 같으면 그 답, 다르면 셋째가 고른 쪽, 셋 다 다르면 `same`을 `key verdict judges [메모]`로
     씁니다. 그래서 `worse`는 두 판정자가 나빠졌다고 본 줄입니다. 답이 모자란 줄은 빠지고 5에서 판정 안 됨으로 셉니다.

5. 개수를 셉니다: `python3 tools/verdicts.py "$RUN/dev.pairs.tsv" "$RUN/dev.sonnet.tsv" "$RUN/dev.gpt.tsv"`는 바뀐 줄,
   나아짐, 같음, 나빠짐과 두 모델이 모두 `worse`로 판정한 키를 출력합니다. 블라인드 판정은 최종 파일을 두 번 줍니다:
   `verdicts.py "$RUN/dev.pairs.tsv" "$RUN/dev.final.tsv" "$RUN/dev.final.tsv"`. 그 `worse`가 모두 두 판정자가
   나빠졌다고 본 줄이라 `both-worse`로도 나옵니다. 그런 줄이 있으면 원인 변경을 되돌리거나 조건을
   좁힙니다. 의견이 갈린 줄과 정상 줄의 변화도 직접 검토합니다. 나아짐:나빠짐이 2:1 이상이고 한 부류의 줄이 몰려
   나빠지지 않을 때 남깁니다.
6. held-out에도 3~5를 별도로 실행하고, 개발 세트와 held-out의 변경 줄, 개선, 동률, 악화 및 쌍방 악화 수를 **분리 보고**합니다.
   held-out을 보고 규칙을 맞췄으면 더는 held-out이 아니므로 새 평가 세트가 필요합니다.
7. `tools/measure.sh`는 변수가 있는 세트를 한꺼번에 번역하고 같은 형식의 판정 입력을 쓰므로 3~5를 똑같이 합니다.
   기준 run도 같은 변수로 잽니다. 참조 샘플의 chrF는 기준보다 낮지 않아야 합니다.

   | 세트 | 변수 | 번역 | 판정 입력, 키 |
   |---|---|---|---|
   | 줄 단위 회귀 문장 | `HDTRANS_CORPUS` | 줄마다 따로(`--each`) | `corpus.pairs.tsv`, `corpus:줄` |
   | 참조 샘플 | `HDTRANS_REF_SAMPLE` | 줄마다 따로 | `sample.pairs.tsv`, `sample:줄` |
   | 테스트 데이터 문서 | `HDTRANS_TESTDATA` | `e2e.sh`처럼 `#` 머리줄을 빼고 한 문서로, 출력에도 머리줄이 없음 | `docs.pairs.tsv`, `docs:폴더/이름:줄` |
   | 웹 예제 | `HDTRANS_EXAMPLES` | `web/examples.tsv`에 이름과 함께 적힌 파일을 `web/examples/`나 이 폴더에서 찾아 한 문서로 | `examples.pairs.tsv`, `examples:파일:줄` |

   줄 번호는 입력 파일에서 셉니다. 네 세트는 동시에 돌고, 문서는 `HDTRANS_JOBS`(기본 CPU 수)개씩 번역합니다.
   `docs:` 키의 폴더로 개발 세트와 held-out을 나눠 셉니다.

### 나쁜 구간을 고른 층 찾기 (attribute.py)

쌍 비교 판정은 구간을 주지 않습니다. 원인을 좁힐 줄을 골라 별도 구간 판정 TSV를 준비합니다.
열은 `line ja out verdict cause span fix`이고 머리줄은 선택입니다(`line`으로 시작). `verdict=ok`가 아닌 행을
분석합니다. `line`은 원본 파일의 줄 번호, `span`은 일본어의 잘못 해석한 구간입니다.

```sh
python3 tools/attribute.py --verdicts "$RUN/spans/sample.verdicts.tsv" \
  --chapters "$DEV/sample.ja.txt" --cli build/mac/hdtrans --dicts dicts \
  --output "$RUN/owners.tsv"
```

`--verdicts`와 `--chapters`는 여러 파일을 받으며, `sample.verdicts.tsv`와 `sample.ja.txt`처럼 이름으로 짝짓습니다.
현재 CLI로 챕터 전체를 `-why` 번역하고, 판정 당시와 원문, 번역이 같은 줄만 분석합니다. 출력은 머리줄 있는
`chapter line cause span fix ja out owners`입니다. `owners`는 겹친 단어의 `층:규칙:표기:한국어`를 `|`로 이으며,
어미 출처가 있으면 `+end=층:규칙`을 덧붙입니다. 같은 구간이 반복되면 모두 찾습니다. 표면형을 원문에서 순서대로
맞추므로 정규화, 합성된 단어는 빠질 수 있고, `-why`가 기록하지 않는 층은 [pipeline-layers.md](pipeline-layers.md#누가-정했는지--why)와
단계 기록으로 확인합니다. 표준 오류에는 분석, 출력 변화, 구간 누락 개수가 나옵니다.

## 오류율 감사 (audit.py)

변경 판정은 바뀐 줄만 봅니다. 감사는 릴리스마다 같은 줄을 뽑아 현재 출력에 등급을 매겨, 릴리스끼리 비교할 수 있는
오류율을 냅니다. 결과는 `$HDTRANS_WORK/audit/<버전>/`에 두고 커밋하지 않습니다.

```sh
V=0.4.8                                    # 릴리스 버전, develop이면 develop-<짧은 해시>
W=${HDTRANS_WORK:-build/work}
tools/measure.sh "audit-$V"                # 네 세트의 현재 출력
A=$W/audit/$V; mkdir -p "$A"
python3 tools/audit.py draw "$W/audit-$V" "$A/lines.tsv"
python3 tools/judge.py claude-sonnet-5-5 "$A/grades.tsv" "$A/lines.tsv" --grade
python3 tools/audit.py rate "$A/lines.tsv" "$A/grades.tsv" > "$A/rate.tsv"
```

- `draw RUN OUT.tsv [--seed S] [--per-set N]`: measure.sh run에서 변수가 있는 세트마다 N줄(기본 75, 네 세트면
  300줄)을 뽑습니다. 가나나 한자가 있는 줄만, 같은 일본어는 세트에 한 번만 셉니다. 씨앗(기본 1), 세트, 일본어의 해시로
  고르므로 같은 씨앗은 어느 run에서나 같은 줄을 뽑고, 세트에 줄이 늘어도 뽑힌 줄은 거의 그대로입니다. 출력은
  `judge.py` 입력 형식(`key ja 출력`, 넷째 열은 비움)이고 키는 measure.sh와 같습니다.
- 등급은 `correct`(그대로 받아들일 번역), `minor`(뜻은 맞지만 조사, 어미, 띄어쓰기, 낱말, 말투가 눈에 띄게 어긋남),
  `wrong`(뜻이 빠지거나 바뀌거나 더해짐, 일본어가 남음, 깨진 한국어)입니다. 줄은 문맥 없이 보며, 어떤 문맥에서 맞을 수
  있는 읽기는 오류로 치지 않습니다. `judge.py --grade`는 둘째 열과 셋째 열만 보고 `key 등급 model`을 씁니다. 직접 매긴
  등급도 같은 열(`key grade judge`, 넷째 열에 메모)로 씁니다.
- `rate LINES.tsv GRADES.tsv`: 세트마다 줄 수, 등급별 개수, `wrong %`, `correct`가 아닌 비율을 냅니다. 한 키의 마지막
  행이 쓰이고, 등급이 없는 줄이 있으면 1로 끝납니다.
- 씨앗, 세트 크기, 등급을 매긴 판정이 같은 감사끼리만 비교합니다.

## 주기 누적 확인 (cycle)

PR마다의 판정은 그 PR이 갈라져 나온 `develop`과 비교하므로, 같은 주기에 합친 PR끼리의 상호작용은 따로 봐야 합니다.

- **합치기 전.** 번역이 바뀌는 PR은 지금의 `develop`에 rebase하고, 지금의 `develop` 머리 빌드의 run을 기준으로
  다시 잽니다. 처음 판정한 쌍에 없는 줄만 판정합니다.
- **합친 뒤.** `develop`에 합칠 때마다 그 커밋을 마지막 릴리스 태그의 run과 네 세트 모두에서 비교하고, run을
  `$HDTRANS_WORK/cycle/<다음 버전>/`에 쌓습니다. 앞선 확인에서 판정한 쌍에 없는 줄만 판정합니다. 릴리스 리뷰는 마지막
  확인 뒤에 새로 생긴 줄만 판정하면 됩니다.

```sh
W=${HDTRANS_WORK:-build/work}; C=$W/cycle/0.4.9     # 다음 버전
tools/measure.sh -c "$TAG/build/mac/hdtrans" -d "$TAG/dicts" release-v0.4.8   # 태그마다 한 번
H=$(git -C "$DEV" rev-parse --short HEAD)
HDTRANS_WORK=$C tools/measure.sh -c "$DEV/build/mac/hdtrans" -d "$DEV/dicts" -b "$W/release-v0.4.8" "$H"
for s in corpus sample docs examples; do
  [ -f "$C/$H/$s.pairs.tsv" ] || continue
  python3 tools/new-pairs.py "$C/$H/$s.new.tsv" "$C/$H/$s.pairs.tsv" \
    $(find "$C" -name "$s.pairs.tsv" ! -path "$C/$H/*")
done
cat "$C/$H"/*.new.tsv > "$C/$H/new.tsv"
echo "$H" >> "$C/checks.txt"
```

- `new-pairs.py OUT.tsv NEW.tsv [OLD.tsv...]`는 NEW의 행 가운데 키, 이전, 이후 출력이 모두 같은 행이 OLD에 없는 것만
  씁니다. 판정한 뒤 이전이나 이후가 바뀐 줄은 다시 새 줄이 됩니다.
- `$DEV`는 그 커밋을 고정한 worktree이고 `$TAG`는 태그의 worktree입니다. `$C/$H/new.tsv`를 위의 변경 판정 3~5로
  판정해 최종 판정을 `$C/$H/final.tsv`에 둡니다. 새 줄이 없으면 빈 `final.tsv`를 둡니다.
- 마지막 릴리스 뒤 누적 개수는 마지막 확인의 쌍마다, 키, 이전, 이후 출력이 같은 줄을 판정한 마지막 확인의 판정으로
  셉니다. 한 주기 안에서 바뀌었다가 되돌아간 줄도 지금 출력에 맞는 판정을 씁니다:

  ```sh
  L=$(tail -n 1 "$C/checks.txt")
  cat "$C/$L"/*.pairs.tsv > "$C/total.pairs.tsv"
  python3 tools/new-pairs.py --verdicts "$C/total.tsv" "$C/total.pairs.tsv" \
    $(for H in $(cat "$C/checks.txt"); do echo "$C/$H/new.tsv" "$C/$H/final.tsv"; done)
  python3 tools/verdicts.py "$C/total.pairs.tsv" "$C/total.tsv"
  ```

## 잘못된 출력 찾기 (detect-bad.sh)

```sh
tools/detect-bad.sh build/e2e/*/*.ko.txt    # 걸린 곳마다 한 줄: 파일:줄, 패턴, 짧은 구간
tools/detect-bad.sh -c build/e2e/*/*.ko.txt # 패턴별 개수만
```

한국어 출력에서 올바른 번역에는 나올 수 없는 연쇄를 찾습니다. 첫 줄이 `#`로 시작하면 건너뜁니다(e2e, 예제
출력의 `#` 줄). 구간은 걸린 곳 앞뒤 몇 글자만 보여 줍니다. 종료 코드: 0 없음, 1 있음, 2 잘못된 인자.

| 패턴 | 찾는 것 |
|---|---|
| `ending-honorific` | 종결 어미 뒤에 붙은 군, 씨, 님, 짱 (`했다님`, `있어요씨`) |
| `doubled-ending` | 종결 어미가 두 번 (`그렇네요군요`, `합니다입니다`) |
| `stem-eung` | 동사 어간 바로 뒤의 감탄사 응 (`하응`, `않응`) |
| `particle-after-punct` | `, . ! ? … ~` 뒤에 홀로 선 조사 (`! 와`, `, 는`). `가 주`(가다)와 `와?`(오다)는 뺌 |
| `residue` | 출력에 남은 일본어 문자나 엔진 표지 (`승용き`, `&AV@`, `(이)가`) |
| `counter-no-number` | 수 없이 홀로 선 서수 `째`, 수가 아닌 말 뒤에 띄어 쓴 `원` (`괴물 째`, `이었던 원이군요`). `3번째`, `한 번 째`, `만 원`은 뺌 |

맞는 출력도 걸릴 수 있는 검사(정확도 100%가 아님)이므로, 걸린 곳을 직접 확인한 뒤 고칩니다. 확인한 결과 모두
실제 오류인 패턴만 엔진 규칙(`src/core/word/constraints.cpp`의 제약 규칙)으로 옮깁니다.

### 원문과 함께 보기 (--source)

```sh
tools/detect-bad.sh --source build/e2e/syosetu.038.ja.txt build/e2e/syosetu.038.ko.txt   # 원문, 번역 쌍
tools/detect-bad.sh -c --source A.ja.txt A.ko.txt B.ja.txt B.ko.txt                       # 패턴별 개수만
```

한국어만 보면 멀쩡해 보이는 오역(`かッハ……！` → `인가 하……!`)을 원문과 맞대어 찾습니다. 줄 번호가 맞는
일본어, 한국어 파일 쌍을 받고, 각 줄을 같은 문장 부호(「」『』（）、。！？…‥―)에서 잘라 조각 수가 같을 때만
조각끼리 비교합니다. 첫 줄이 `#`이면 건너뜁니다. 종료 코드는 위와 같습니다.

| 패턴 | 찾는 것 |
|---|---|
| `sound-function` | 소리로 읽히는 일본어 조각(가나 2~5자, ー, ～ 포함, 첫 글자가 아닌 っ, ッ가 있음, を로 시작하지 않음)이 기능어, 조사 낱말이 든 한국어가 됨 (`かッハ` → `인가 하`, `だっは` → `다 는`). 다음 조각의 첫 음절 하나로 쓴 말더듬(`こっ、この` → `이, 이`)은 뺌 |
| `stutter-mismatch` | 말더듬(가나 1~2자, 、나 … 뒤의 조각이 앞 조각으로 시작하고 늘인 소리가 아님: `ご、ごめ、ごめんなさい`)의 한국어 조각이 마지막 조각의 한국어로 시작하지 않음 (`째, 째, 미안해요`, `그래서, 그렇지만`). 마지막 음절은 받침 없이 비교하므로 `가, 감사합니다`는 뺌 |

## 환경 변수 요약

| 변수 | 쓰는 곳 | 설명 |
|---|---|---|
| `HDTRANS_DICTS` | ctest `examples` | 예제 번역에 쓸 사전 폴더 |
| `HDTRANS_REQUIRE_ALL` | 단위 테스트 | `1`이면 건너뛴 테스트를 실패로 |
| `HDTRANS_TEST_EXCLUDE` | 단위 테스트 | 건너뛸 테스트 이름 (쉼표 구분) |
| `HDTRANS_DATA`, `HDTRANS_INPUTS`, `HDTRANS_RUNS`, `HDTRANS_BIN` | `snapshot.sh`, `compare.sh` | 입력, 기록 위치와 CLI |
| `HDTRANS_TESTDATA` | `e2e.sh`, `fetch-syosetu.sh`, `measure.sh`, `audit.py`, `drops.py`, ctest `testdata`, `sets.drops` | 테스트 데이터 폴더 |
| `HDTRANS_EXAMPLES` | `measure.sh`, `audit.py`, `drops.py` | `web/examples/`에 없는 웹 예제 텍스트 폴더 (웹 빌드의 `HDTRANS_WEB_EXAMPLES`와 같은 폴더, 비공개) |
| `HDTRANS_JOBS` | `measure.sh` | 한꺼번에 번역할 문서 수 (기본 CPU 수) |
| `HDTRANS_DICTS_A` | `e2e.sh` | A 쪽 사전 |
| `HDTRANS_DRIVER_DUMP` | CLI | `-stages`와 같음 |
| `HDTRANS_CACHE_DIR`, `HDTRANS_SNAPSHOT`, `HDTRANS_VERIFY_DICTS` | 엔진 | 사전 캐시 ([dictionary-files.md](dictionary-files.md#캐시)) |
| `HDTRANS_WORK` | `gate.sh`, `measure.sh` | 작업 출력 폴더 (기본 `build/work`) |
| `HDTRANS_CORPUS`, `HDTRANS_REF_SAMPLE` | `measure.sh`, `audit.py`, `count-class.py`, `drops.py` | 줄 단위 회귀 문장 파일, `ja.txt`와 줄이 맞는 사람 번역 `ref.txt`가 든 폴더 (비공개) |
| `HDTRANS_PRESET` | `gate.sh`, `measure.sh`, `regen-rules.sh`, `shadow.py` | 프리셋 (기본 macOS `mac`, 그 밖 `linux`) |
| `HDTRANS_TIMEOUT` | `gate.sh`, `measure.sh`, `pick.sh` | 오래 걸릴 수 있는 단계의 시간 제한(초, 기본 1800) |
| `HDTRANS_PASS_SKIP`, `HDTRANS_PASS_SHUFFLE` | `HDTRANS_PASS_EXPERIMENTS` 빌드의 엔진 | 뺄 문장 패스, 패스 순서를 섞는 씨앗 ([패스 실험 빌드](#패스-실험-빌드)) |
| `HDTRANS_EXAMPLES_OUT` | `examples.sh` | 출력 폴더 (기본 `build/examples`) |
| `HDTRANS_HYGIENE_PATTERNS` | `hygiene.py` | 커밋하면 안 되는 말의 정규식 파일 (저장소 밖) |

작업 절차와 그 도구(`gate.sh`, `measure.sh`, `pick.sh` 등)는 [skills/README.md](../skills/README.md)에 있습니다.
