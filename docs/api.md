# API

hdTrans는 하나의 정적 라이브러리(`hdTransCore`)와 그 위의 얇은 프런트엔드로 되어 있습니다.

| 프런트엔드 | 소스 | 인터페이스 |
|---|---|---|
| CLI `hdtrans` | `src/cli/` | 표준 입출력, UTF-8 |
| `hdTransEngine` (dll/so/dylib) | `src/cshared/` | C API, UTF-8 (`include/core/hdtrans.h`) |
| `J2KEngine.dll` | `src/j2kengine/` | 이지트랜스 호환 `J2K_*`, Shift-JIS/CP949 또는 UTF-16 |
| WebAssembly, TypeScript | `src/wasm/`, `web/src/translator.ts` | Embind, `HdTrans` 클래스 ([TypeScript API](typescript-api.md)) |
| C++ | `include/core/engine.h` | `hdTrans::Engine`, `hdTrans::Session` |

## 명령줄 (hdtrans)

### 배포판으로 시작하기

[배포 파일](../README.md#다운로드와-설치)을 받아 압축을 푼 폴더에서 실행합니다. 입력과 출력은 UTF-8입니다.

Windows:

```powershell
.\hdtrans.exe --in script.ja.txt --out script.ko.txt
.\hdtrans.exe --each --in lines.ja.txt --out lines.ko.txt
```

Linux, macOS:

```sh
./hdtrans --in script.ja.txt --out script.ko.txt
./hdtrans --each --in lines.ja.txt --out lines.ko.txt
```

기본 사전 스냅샷을 자동으로 읽으므로 `--dicts`는 필요 없습니다.
Windows는 `hdTrans\dicts.hdsnap`, Linux와 macOS는 프로그램 옆 `dicts.hdsnap`을 씁니다.
기본은 파일 전체가 한 문서이며 `--each`는 줄마다 문맥을 새로 시작합니다.

### npm CLI

Node.js 22 이상에서 전역 설치하거나 `npx`로 실행합니다. 패키지의 WASM 엔진과 기본 사전을 씁니다.

```sh
npm install -g hdtrans
hdtrans "今日は本を読みます。"

npx hdtrans "今日は本を読みます。"
```

```sh
hdtrans --version
hdtrans --json "競走で勝った。"
hdtrans --in input.txt --out output.txt
hdtrans --json --file input.txt --out result.json
hdtrans --why --userdict userdict.txt "蒼月は学生です。"
```

지원 옵션은 `--in`(`--file`), `--out`, `--json`, `--why`, `--userdict`, `--ehnd`, `--version`, `--help`입니다.
입력은 UTF-8 파일, 문장 인자나 표준 입력입니다. `--json`은 `{ text, choices, logs }` 객체를 출력하고,
`--out`과 함께 쓰면 JSON을 파일에 저장합니다. 로그와 오류는 표준 오류로 보냅니다.
`--in`과 직접 문장을 함께 지정할 수 없으며 `--` 뒤의 인자는 옵션으로 해석하지 않습니다.

npm CLI의 `--userdict`는 파일 하나를 받으며 중복 지정은 오류입니다.
`--ehnd DIR`로 Ehnd 파일 묶음을 명시적으로 적용합니다. 현재 폴더의 사전이나 설정 파일은 자동으로 읽지 않습니다.
사전 컴파일, `--each` 등 아래 네이티브 전용 옵션은 npm CLI에서 지원하지 않습니다.

`--version`은 양쪽 CLI에서 사전을 실제로 로드한 뒤 `hdTrans v<엔진> (<실행 방식>) / dict <사전>`을 출력합니다.
실행 방식은 배포판이 `native`, npm판이 `wasm`입니다.
버전 정보가 없는 직접 편집한 사전은 `development (unversioned)`로 표시합니다.

### 네이티브 CLI 옵션

표준 입력(또는 `--in FILE`)의 UTF-8을 읽어 번역을 표준 출력에 씁니다. 기본으로 입력 전체가 한 세션,
한 문서이며, 출력 줄은 `\n`으로 잇고 끝에 줄바꿈을 붙이지 않습니다. `--each`와 `--raw`는 줄마다 번역하고
각 줄 뒤에 `\n`을 붙입니다.

```
hdtrans [--json] [--version] [--config FILE] [--dicts PATH,...] [--userdict PATH]... [--ehnd DIR] [--each | --raw] [--reference] [--verify-dicts] [--compile-dicts | --compile-snapshot FILE | --no-cache] [--why] [--select-shadow] [--misread] [--no-cg] [--cg-learned] [--soften MODE] [--stages DIR] [--rule-counts FILE] [--in FILE | --file FILE] [--out FILE] [--] [text]
```

| 옵션 | 설명 |
|---|---|
| `--config FILE` | 기본 `user/hdtrans.config.yaml` 대신 읽을 설정 파일 |
| `--dicts PATH[,PATH...]` | 사전 폴더 목록 또는 [컴파일한 사전](dictionary-files.md#컴파일한-사전-hdsnap) `.hdsnap` 파일. 스냅샷 하나면 원본 폴더 없이 읽음. 기본값은 데이터 루트의 `dicts.hdsnap`, `dicts/`, 현재 폴더의 `dicts` 순서 |
| `--userdict PATH` | 사용자 사전 파일이나 폴더. 여러 번 지정 가능. 기본값은 데이터 루트 `user/`의 `userdict*.txt`, `userdict*.tsv`. 빈 값이면 끔. 명시한 파일이 하나뿐이면 오류 때 실패 |
| `--in FILE`, `--file FILE` | 표준 입력 대신 UTF-8 파일을 읽음 |
| `--out FILE` | 결과를 UTF-8 파일로 저장. 생략하면 표준 출력. `--json`과 함께 쓰면 JSON 저장 |
| `--json` | `{ text, choices, logs }` 객체를 JSON으로 출력. WASM과 같은 선택 기록과 필터 로그 |
| `--version` | 선택한 사전을 로드하고 엔진 버전, native/WASM 구분, 사전 버전을 한 줄로 출력 |
| `--help` | 프로그램과 작성자 정보, 사용법 출력 |
| `--ehnd DIR` | Ehnd 폴더 DIR의 필터, SkipLayer, 사용자 사전을 씀. 기본으로 쓰지 않으며 설정 파일로도 켤 수 있음. 건너뛴 줄은 표준 오류에 씀 ([ehnd-compat.md](ehnd-compat.md)) |
| `--each` | 줄마다 새 세션 |
| `--raw` | 줄마다 단어 파이프라인만으로 번역 |
| `--reference` | 번역 개선 규칙을 끈 기준 모드 |
| `--verify-dicts` | 원본 해시와 모든 캐시 세그먼트를 검사. 표지 문자가 품사 코드의 부류와 맞지 않는 사전 줄이 있으면 나열하고 실패 ([rule-language.md](rule-language.md#번역-안의-표지-d-y)) |
| `--compile-dicts` | 사전 캐시를 컴파일, 검사하고 종료. 캐시 경로 또는 `up to date`를 출력 |
| `--compile-snapshot FILE` | 기본 사전 전체를 FILE(`.hdsnap`)에 컴파일하고 종료. FILE을 출력 |
| `--no-cache` | 사전 캐시를 끔 (`--compile-dicts`, `--compile-snapshot`과 함께 쓸 수 없음). `.hdsnap`은 그대로 읽음 |
| `--why` | 단어와 어미 선택, shadow 점수, 처리 단계와 Ehnd 필터 실행 로그를 표준 오류에 씀 |
| `--select-shadow` | 번역은 바꾸지 않고 가장 짧은 대상(`shortest-target`)으로 점수를 매김. `--why`에 나타남 |
| `--misread` | 오독 필터(`misreadFilter`)를 켬: 모델이 오독으로 점수를 매긴 선택 후보를 대안으로 바꿈 ([misread-classifier.md](misread-classifier.md)) |
| `--no-cg` | CG-lite 손 규칙(`cgRules`, 기본 켬)을 끔: 오독 결정의 후보를 지우거나 고르는 규칙표 |
| `--cg-learned` | 학습 규칙(`cgLearned`, 기본 끔)도 켬 |
| `--soften MODE` | 대사의 문장 끝 `-다`를 해체로 씀(간다 → 갈 거야, 다르다 → 달라, 학생이다 → 학생이야). `quotes`(기본): 「」 안, `marked`: 「」 안과 `이름: ` 뒤의 줄, 괄호 없이 말투(よ, ね, だろ, ちゃう)로 끝나는 줄(들여쓴 줄은 서술로 봄), `dialogue`: 모든 줄, `off`: 끔. 공손체(です, ます)와 고풍 말투(ておる, じゃ)의 대사, 『』 안, 인용한 말(「…」という)은 그대로 둠. `--reference`에서는 꺼짐 |
| `--stages DIR` | 단계 기록을 DIR에 씀 ([testing.md](testing.md#한-입력-살펴보기-단계-기록--stages)) |
| `--rule-counts FILE` | 선택 규칙마다 실행 횟수와 참이 된 횟수를 FILE에 TSV로 씀: `kind`(L, G), `index`(전역 규칙은 PRIO), `pumsa`, `number`, `ugan`, `runs`, `fires` ([rule-language.md](rule-language.md#사용-횟수)) |

기존 `-in`, `-why` 같은 단일 하이픈 옵션도 받습니다. 직접 문장을 인자로 넘길 수 있고, `--` 뒤는 옵션으로 해석하지 않습니다.
`--in`과 직접 문장을 함께 지정하면 오류입니다. npm CLI와 공통 옵션의 이름과 의미가 같으며, npm판의 실행 엔진은 WASM입니다.

종료 코드: 0 성공, 1 사전을 못 불러옴, 입력 읽기 실패, 캐시 오류, 명시한 설정 파일 없음, 2 알 수 없는 옵션(사용법 출력).

`--why`의 표준 오류 출력은 탭으로 구분한 줄입니다. `WHY-LINE <줄>`이 입력 줄마다 나오고, 단어마다
`WHY <줄> <청크> <문장> <토큰>:<시작> <표면형> <선택> <출처> <id> <번역> <직접 선택> <출처> <id> <층> <규칙> <어미 층> <어미 규칙>`,
그 아래에 `SHADOW` 줄이 붙습니다. Ehnd 필터 실행 로그도 함께 나오며, 번역문은 표준 출력에만 씁니다.
층과 규칙은 후보와 어미를 정한 실행 층과 규칙(`local`
`L925`, 전역 규칙은 `파일:GPRIO`)입니다([pipeline-layers.md](pipeline-layers.md#누가-정했는지--why)). 탭, 줄바꿈, 백슬래시는 `\t`, `\n`, `\\`로 이스케이프합니다.

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_CACHE_DIR` | 캐시 폴더 (절대 경로). 빈 값, 상대 경로(경고 출력), `off`(대소문자 무관)는 캐시를 끔 |
| `HDTRANS_SNAPSHOT=off` | 캐시를 끔 |
| `HDTRANS_VERIFY_DICTS=1` | `--verify-dicts`와 같음 |
| `HDTRANS_DRIVER_DUMP=DIR` | `--stages DIR`과 같음 |

### 설정 파일

실행 파일이나 DLL 옆에 `hdTrans/` 디렉터리가 있으면 그곳이 데이터 루트이고, 없으면 실행 파일이나 DLL이 있는 폴더입니다.
이름은 대소문자를 가리지 않습니다. 기본 사전은 `dicts.hdsnap`, 없으면 `dicts/`를 읽습니다.
선택한 스냅샷을 읽지 못하면 TSV로 바꾸지 않고 실패합니다. 사용자 파일은 `user/`에 두며,
옛 배치의 프로그램 옆 `userdict.tsv`와 `hdtrans.config.yaml`도 지원합니다.

데이터 루트의 `user/hdtrans.config.yaml`을 시작할 때 읽습니다. DLL은 `hdTrans_Reload`나
`J2K_ReloadUserDict`에서도 다시 읽습니다. 파일이 없으면 기본값을 쓰며 Ehnd는 자동으로 켜지지 않습니다.
배포본의 `user/`에는 견본만 있습니다. [`hdtrans.config.example.yaml`](../hdtrans.config.example.yaml)을
같은 폴더의 `hdtrans.config.yaml`로 복사해 고칩니다. 상대 경로는 설정 파일의 폴더 기준이며,
CLI 플래그가 파일보다 우선합니다.

| 키 | 값과 기존 옵션 |
|---|---|
| `dicts` | 경로 하나 또는 경로 목록, `--dicts`처럼 순서대로 읽음 |
| `userdict` | 사용자 사전 파일이나 폴더 경로 또는 경로 목록. 기본 탐색을 대신하며 목록 뒤쪽이 우선 |
| `ehnd` | 경로 또는 `true`(실행 파일이나 DLL 옆 `Ehnd`), `false`나 생략은 꺼짐. `--ehnd`와 같은 필터와 사용자 사전을 함께 적용 |
| `cache` | 경로 또는 `off`, `HDTRANS_CACHE_DIR`에 적용. `--no-cache`가 우선 |
| `cg` | bool, 기본 `true`. `false`는 `--no-cg` |
| `misread` | bool, 기본 `false`. `true`는 `--misread` |
| `reference` | bool, 기본 `false`. `true`는 `--reference` |
| `soften` | `off`, `quotes`(기본), `marked`, `dialogue`, `--soften`과 같음. `true`는 `quotes`, `false`는 `off` |

`cache`를 지우거나 DLL을 종료하면 적용 전 환경변수를 복원합니다. `HDTRANS_SNAPSHOT=off`는 그대로 유효합니다.
알 수 없는 키와 잘못된 값은 건너뛰고, 문법 오류 앞의 정상 항목은 유지합니다. 진단은 CLI의 표준 오류,
C API의 `hdTrans_UserDictError`, J2K의 디버거 출력으로 알립니다.
C API의 비어 있지 않은 `dirs`와 명시적인 `hdTrans_OpenEhnd`는 파일보다 우선합니다.
WebAssembly의 `HdTrans` 클래스는 YAML 대신 초기화 옵션과 사용자 사전, Ehnd 설정 메서드를 받습니다([TypeScript API](typescript-api.md)).

## 번역 창 (hdTransGUI.exe, Windows)

단문 번역 창과 문서 번역 창이 있습니다. 실행 파일 옆의 `hdTrans\`(없으면 실행 파일 폴더)에서 사전, 사용자 사전,
설정을 엔진을 처음 불러올 때 읽습니다. 문서 번역은 Shift-JIS, UTF-8(BOM 유무 무관), UTF-16LE(BOM)를 읽고
UTF-8 BOM으로 저장합니다. 문서 전체를 한 번에 번역하므로 문맥이 이어지고, **중단**은 지금 문서를 마친 뒤
결과를 저장하지 않습니다.

## C API (hdTransEngine)

헤더: `include/core/hdtrans.h`. Windows에서 DLL을 쓰는 쪽은 `HDTRANS_USE_DLL`을 정의합니다.

- 문자열은 모두 UTF-8입니다.
- 프로세스에 엔진 하나, 세션 하나입니다. 앞선 호출에서 찾은 이름과 문맥이 이후 번역에 반영됩니다.
- 호출은 스레드 안전하지만 재진입은 안 됩니다. 라이브러리를 내리기 전에 모든 호출을 끝내야 합니다.
- 초기화, 다시 읽기, 종료는 대기 중인 번역보다 먼저 처리됩니다.
- 데이터 루트 `user/`의 `userdict*.txt`, `userdict*.tsv`를 사전 폴더 다음에 읽고, 추가, 변경, 삭제를 자동 반영합니다
  ([dictionary-files.md](dictionary-files.md#사용자-사전-파일)). 불러오지 못하면 빼고 시작합니다(`hdTrans_UserDictError`).
- Ehnd는 설정 파일의 `ehnd`나 `hdTrans_OpenEhnd`로 켭니다. 켠 폴더가 바뀌면 함께 다시 읽습니다
  ([ehnd-compat.md](ehnd-compat.md)).
- Windows에서 프로세스 종료 시에는 엔진을 정리하지 않고 OS에 맡기며, `FreeLibrary`로 내릴 때는 진행 중인
  다시 읽기를 기다리지 않습니다. 깔끔하게 정리하려면 `hdTrans_Terminate`를 부릅니다.

| 함수 | 설명 |
|---|---|
| `int hdTrans_Initialize(const char* dirs)` | 사전 목록을 불러옴. `dirs`는 NUL로 구분하고 NUL 두 개로 끝나며 뒤 폴더가 앞을 덮어씀. `.hdsnap` 하나만 넘겨도 됨. NULL이나 빈 목록이면 설정의 `dicts`, 없으면 데이터 루트의 `dicts.hdsnap`, `dicts/` 순서로 찾음. 0 성공, -1은 불러오기 실패 또는 동시에 진행된 Terminate, Initialize, Reload가 이 불러오기를 취소함 |
| `void hdTrans_Terminate(void)` | 세션과 사전 해제. 여러 번 불러도 되고, 다시 Initialize할 수 있음 |
| `int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn)` | `text[0..n)`(`\n`으로 구분한 한 줄 이상)을 프로세스 세션에서 번역. `*out`에 NUL로 끝나는 버퍼, `*outn`에 길이(NUL 제외). 0 성공, -1 초기화 전, 종료 후 |
| `int hdTrans_Reset(void)` | 세션이 앞선 텍스트에서 배운 이름과 문맥을 잊어 다음 번역이 처음처럼 시작함. 0 성공, -1 초기화 전, 종료 후 |
| `int hdTrans_Reload(void)` | 설정 파일과 사전 폴더, 사용자 사전, 켜 둔 Ehnd 폴더를 다시 불러옴. 0 성공, -1 초기화 전, 종료 후, 실패(이전 사전 유지, 여러 사용자 파일 중 오류 파일은 빼고 나머지를 읽음) |
| `int hdTrans_UserDictError(char** out, size_t* outn)` | 설정 파일 진단과 마지막 사용자 사전 읽기 오류(오류마다 한 줄, 없으면 빈 문자열)를 `hdTrans_Translate`처럼 버퍼로 돌려줌. 0 성공, -1 초기화 전, 종료 후 |
| `int hdTrans_OpenEhnd(const char* dir)` | Ehnd 폴더 `dir`의 필터와 사용자 사전을 이후 번역에 씀(NULL이나 빈 값은 뗌). 사용자 사전처럼 자동으로 다시 읽음. 0 성공, -1 초기화 전, 종료 후, 폴더를 못 읽음 |
| `int hdTrans_FilterErrors(char** out, size_t* outn)` | 필터 파일과 Ehnd 폴더에서 건너뛴 줄(`파일:줄: 이유`, 줄마다 하나)을 버퍼로 돌려줌. 0 성공, -1 초기화 전, 종료 후 |
| `void hdTrans_FreeMem(void* p)` | `hdTrans_Translate`가 돌려준 버퍼 해제 |
| `const char* hdTrans_Version(void)` | 라이브러리 버전 문자열 |

```c
#include "core/hdtrans.h"

if (hdTrans_Initialize(NULL) != 0) return 1;
const char* text = "猫が好きです。";
char* out = NULL;
size_t outn = 0;
if (hdTrans_Translate(text, strlen(text), &out, &outn) == 0) {
    fwrite(out, 1, outn, stdout);
    hdTrans_FreeMem(out);
}
hdTrans_Terminate();
```

C API는 사전 캐시를 읽기만 하고 새로 저장하지 않습니다([dictionary-files.md](dictionary-files.md#캐시)).

## J2K 함수 (J2KEngine.dll)

이지트랜스의 `J2KEngine.dll`과 같은 이름의 함수를 `__stdcall`로 내보냅니다(`src/j2kengine/j2kengine.def`).
Windows 전용이며, 이지트랜스를 불러오는 도구는 대부분 32비트이므로 x86 빌드를 씁니다.

- 기본 사전은 설정의 `dicts`, 없으면 데이터 루트의 `dicts.hdsnap`, `dicts/` 순서로 찾습니다.
  사용자 사전은 `user/`의 `userdict*.txt`, `userdict*.tsv`에서 따로 읽습니다. 호스트가 넘기는 사전 경로는 무시합니다.
  사용자 사전의 추가, 변경, 삭제는 자동 반영됩니다([dictionary-files.md](dictionary-files.md#사용자-사전-파일)). 불러오지 못하면
  빼고 시작하며 이유를 디버거 출력(`OutputDebugString`)에 씁니다.
- Ehnd는 설정 파일의 `ehnd`로 켭니다. 켠 폴더가 바뀌면 함께 다시 읽으며 건너뛴 줄은
  디버거 출력에 씁니다 ([ehnd-compat.md](ehnd-compat.md)).
- 프로세스 종료 시에는 엔진을 정리하지 않고, `FreeLibrary`로 내릴 때는 진행 중인 다시 읽기를 기다리지 않습니다.
- Shift-JIS(932), CP949(949), UTF-16 변환은 OS 함수로 이 경계에서만 합니다. 엔진 안에는 코드 페이지
  표가 없습니다.
- 반환 버퍼는 프로세스 힙에 할당하며 `J2K_FreeMem`으로 해제합니다.
- 호출은 스레드 안전하지만 재진입은 안 됩니다.

| 함수 | 동작 |
|---|---|
| `J2K_Initialize(char* dicPath)` | 사전을 불러옴. 1 성공(이미 초기화되어 있어도 1), 0 실패, 취소 |
| `J2K_InitializeEx(char* dicPath, char* mainDir)` | `J2K_Initialize`와 같음 |
| `J2K_Terminate()` | 세션과 사전 해제, 1 |
| `J2K_TranslateMMNT(HWND, char* text)` | Shift-JIS 입력, CP949 출력 |
| `J2K_TranslateMMNTW(HWND, const wchar_t* text)` | UTF-16 입력, UTF-16 출력 |
| `J2K_TranslateMM`, `J2K_TranslateMMEx`, `J2K_TranslateChat` | `J2K_TranslateMMNT`와 같음 |
| `J2K_TranslateFM(HWND, char* fileName)` | 파일 번역 없음, null |
| `J2K_FreeMem(void*)` | 번역 결과 해제 |
| `J2K_ReloadUserDict()` | 설정 파일과 사전, 켜 둔 Ehnd 폴더를 바로 다시 불러옴 |
| `J2K_StopTranslation` | 아무것도 하지 않고 1 |
| `J2K_SetField`, `J2K_SetHnj2han`, `J2K_SetDelJPN`, `J2K_SetJWin`, `J2K_SetPriorDict`, `J2K_GetJ2KMainDir` | 호환용, 아무것도 하지 않음 |
| `J2K_GetPriorDict` | null |
| `J2K_SetProperty`, `J2K_GetProperty` | 0 |

`?GetJ2KMainDir@@YA?AVCString@@XZ`는 `J2K_GetJ2KMainDir`의 별칭입니다.

## WebAssembly

`emcmake cmake --preset wasm`으로 `hdtrans_engine.js`(ES6 모듈)와 `hdtrans_engine.wasm`을 만듭니다.
C++ 엔진은 Embind로 연결하고, TypeScript에서는 `HdTrans.init()`, `translate()`, 사용자 사전과
Ehnd 설정 메서드를 씁니다. C API 포인터나 `ccall`, `FreeMem`을 직접 다루지 않습니다.
네이티브 C API와 J2K 함수의 사용법은 바뀌지 않습니다.

초기화, 반환 타입, JSON/WHY, 실행 중 필터 교체 예제는 [TypeScript API](typescript-api.md)에 있습니다.
웹 데모도 같은 인터페이스를 사용하며, `tools/wasm_smoke.mjs`가 실제 WASM 엔진으로 검사합니다.

## C++ API

헤더는 `include/core/engine.h`입니다. `Engine`이 사전을 들고, `Session`이 그 사전으로 번역합니다.

```cpp
hdTrans::Engine engine;                      // 사전
engine.Open({"dicts"});                      // 사용자 사전은 OpenUserDicts로
hdTrans::Session session(engine);            // 문맥을 기억하는 번역
std::string ko = session.Translate("今日はいい天気ですね。");
```

### Engine

`Engine(bool reference = false, CacheWrite cacheWrite = CacheWrite::Sync, UserReload userReload = UserReload::Off)`.
여러 세션과 스레드가 함께 씁니다.

| 함수 | 하는 일 |
|---|---|
| `Open(dirs, verifyDicts = false, userFile = {})` | 사전 폴더(뒤 폴더가 앞을 덮음)와 사용자 사전 파일 하나를 불러옴. 사전을 하나도 못 불러오면 `false` |
| `OpenUserDicts(dirs, verifyDicts, userPaths)` | 사용자 사전을 파일이나 폴더 목록으로 받음. 폴더는 그 안의 `userdict*.txt`, `userdict*.tsv`. 뒤 파일이 우선 |
| `Reload()` | 폴더, 사용자 사전, Ehnd 폴더를 다시 불러와 바꿈. 진행 중인 세션은 하던 사전으로 계속 번역 |
| `OpenEhnd(dir)` | Ehnd 폴더의 필터와 사용자 사전을 씀. 빈 경로는 뗌. `Open` 전이면 폴더 이름만 정함 |
| `loaded()` | 사전을 불러왔는지 |
| `UserFileError()` | 사용자 사전을 못 불러온 이유. 못 불러온 파일은 빼고 엶 |
| `FilterErrors()` | 필터 파일과 Ehnd 폴더에서 건너뛴 줄(`파일:줄: 이유`) |
| `Dictionaries()`, `Filters()` | 지금 쓰는 사전과 필터 |
| `DetachReload()` | 라이브러리를 내릴 때. 엔진을 없애도 진행 중인 다시 읽기를 기다리지 않음 |

| 옵션 | 값 |
|---|---|
| `CacheWrite` | `Sync`: `Open`이 끝나기 전에 사전 캐시를 저장. `Off`: 저장하지 않음 |
| `UserReload` | `Auto`: 번역을 시작할 때 사용자 사전과 Ehnd 폴더를 1초에 한 번까지 보고, 바뀌면 백그라운드에서 다시 불러옴. `Off`: 하지 않음 |

엔진을 없애면 진행 중인 다시 읽기가 끝나기를 기다립니다(`DetachReload` 뒤에는 기다리지 않음).

### Session

`Session(const Engine&, bool reference = false, bool cg = true, bool misread = false)`. 한 번에 한 스레드에서 씁니다.

| 함수 | 하는 일 |
|---|---|
| `Translate(text)` | `\n`으로 나눈 줄을 번역(`\r`은 유지). 앞에서 번역한 문맥을 기억함. 64 KiB가 넘는 줄은 16 KiB 이하로 나눠 번역 |
| `Reset()` | 기억을 지우고 엔진의 지금 사전을 가져옴 |
| `SetSoften(Soften)` | 다음 번역부터 대사로 볼 범위(`Off`, `Quotes`(기본), `Marked`, `Dialogue`, `--soften`과 같음). 기준 모드에서는 효과 없음 |

`UserReload::Auto` 엔진의 세션은 엔진에 새 사전이 생기면 다음 번역에서 기억을 지우고 그 사전으로 바꿉니다.
필터와 Ehnd 형식은 [ehnd-compat.md](ehnd-compat.md)에 있습니다.
