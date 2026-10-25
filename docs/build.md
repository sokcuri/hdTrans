# 빌드

hdTrans는 CMake 프리셋(`CMakePresets.json`)으로 빌드합니다. 외부 라이브러리는 필요 없습니다. Ehnd 필터의
정규식에 쓰는 PCRE2 10.49는 소스째 `third_party/pcre2`에 들어 있어(라이선스 `LICENCE.md`) 함께
빌드되며, 16비트 라이브러리만 만듭니다.

## 요구 사항

| 플랫폼 | 필요한 것 |
|---|---|
| macOS, Linux | CMake 3.25 이상, C++20 컴파일러(clang 또는 gcc), Ninja |
| Windows | Visual Studio 2026 (C++ 도구 포함), CMake 4.2 이상 |
| WebAssembly | Emscripten, Ninja |

## 프리셋

| 프리셋 | 생성기 | 출력 폴더 | 내용 |
|---|---|---|---|
| `mac` | Ninja | `build/mac` | macOS clang Release |
| `mac-debug` | Ninja | `build/mac-debug` | macOS Debug |
| `mac-asan` | Ninja | `build/mac-asan` | macOS, AddressSanitizer와 UndefinedBehaviorSanitizer |
| `linux` | Ninja | `build/linux` | Linux Release |
| `linux-asan` | Ninja | `build/linux-asan` | Linux, AddressSanitizer와 UndefinedBehaviorSanitizer |
| `windows-x64` | Visual Studio 18 2026 | `build/windows-x64/Release` | MSVC x64 |
| `windows-x86` | Visual Studio 18 2026 | `build/windows-x86/Release` | MSVC x86 (32비트 호스트용 DLL) |
| `windows-x64-ninja` | Ninja | `build/windows-x64-ninja` | MSVC x64, x64 개발자 환경 필요 |
| `windows-x86-ninja` | Ninja | `build/windows-x86-ninja` | MSVC x86, x86 개발자 환경 필요 |
| `wasm` | Ninja | `build/wasm` | Emscripten |

네이티브 프리셋은 모두 정적 라이브러리(`hdTransCore`), 공유 라이브러리(`hdTransEngine`), CLI
(`hdtrans`), 단위 테스트(`hdtrans_tests`)를 빌드합니다. `J2KEngine.dll`은 Windows에서만 만들어집니다.

| 산출물 | Windows | Linux | macOS |
|---|---|---|---|
| C API 라이브러리 | `hdTransEngine.dll` | `libhdTransEngine.so` | `libhdTransEngine.dylib` |
| 이지트랜스 호환 DLL | `J2KEngine.dll`(배포본은 x86만) | - | - |
| CLI | `hdtrans.exe` | `hdtrans` | `hdtrans` |
| 단문, 문서 번역 GUI | `hdTransGUI.exe` | - | - |

## macOS, Linux

```sh
cmake --preset mac          # 또는 linux
cmake --build --preset mac --parallel
ctest --preset mac
```

이것이 기본 개발 루프입니다. 다른 네이티브 플랫폼에서는 `mac` 대신 해당 프리셋 이름을 씁니다.

`mac-asan`, `linux-asan`은 `HDTRANS_SANITIZE=address,undefined`로 모든 타깃을 계측하고, 보고가 하나라도
나오면 프로그램을 멈춥니다. 다른 GCC, Clang 빌드에도 `-DHDTRANS_SANITIZE=...`로 같은 계측을 켤 수 있습니다.
CI의 `linux-asan` 작업은 Clang으로 이 프리셋을 빌드하고 단위 테스트, 예문, 규칙 사례를 돌립니다. GCC 13으로
빌드했다면 `ASAN_OPTIONS=detect_stack_use_after_return=0`을 주어야 사전 읽기가 몇 분씩 걸리지 않습니다.

macOS에서 Command Line Tools만 쓰면 컴파일러 확인 단계에서 실패할 수 있습니다. 그때는 Xcode를 쓰게 합니다:
`export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.

처음 구성할 때 컴파일러가 아직 지원하지 않는 최신 SDK를 골라 실패하면, 설치된 SDK 가운데 하나를 `SDKROOT`로
정하고 구성부터 다시 합니다. 이후 빌드도 같은 `SDKROOT`로 합니다.

```sh
ls /Library/Developer/CommandLineTools/SDKs                              # Command Line Tools
ls "$(xcode-select -p)"/Platforms/MacOSX.platform/Developer/SDKs          # Xcode
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX<버전>.sdk   # 고른 SDK
rm -rf build/mac && cmake --preset mac
```

## Windows

Visual Studio 프리셋은 개발자 명령 프롬프트 없이 일반 터미널에서 동작합니다.

```powershell
cmake --preset windows-x64  # 또는 windows-x86
cmake --build --preset windows-x64 --parallel
ctest --preset windows-x64
```

Release 바이너리는 `build\windows-x64\Release`(또는 `build\windows-x86\Release`)에 있습니다.
이지트랜스를 쓰던 도구에 넣을 `J2KEngine.dll`은 32비트라 `windows-x86`으로 빌드합니다. `windows-x64`도 빌드는 되지만 배포본에는 넣지 않습니다.

Ninja로 빌드하려면 아키텍처에 맞는 MSVC 개발자 명령 프롬프트나 `vcvarsall` 환경에서
`windows-x64-ninja` 또는 `windows-x86-ninja`를 씁니다. 바이너리는 `build\<프리셋>`에 바로 생깁니다.

### tools/build.ps1

```powershell
powershell -File tools\build.ps1 -arch all
```

설치된 Visual Studio(2022 또는 2026)를 찾아 그 안의 CMake로 `windows-<arch>` 프리셋을 구성, 빌드하고
`ctest`를 실행한 뒤, 각 DLL의 SHA-256과 크기를 출력합니다.

| 인자 | 설명 |
|---|---|
| `-arch x86\|x64\|all` | 빌드할 아키텍처 (기본 `all`) |
| `-ref REF` | `-bare`가 있을 때 체크아웃할 ref (기본 `main`) |
| `-root DIR` | 작업 트리 (기본 `HDTRANS_CPP_ROOT`, 없으면 스크립트가 있는 저장소) |
| `-bare DIR` | 가져올 bare 저장소 (기본 `HDTRANS_CPP_BARE`, 없으면 가져오지 않음). 주면 작업 트리를 `origin/<ref>`로 강제로 맞춥니다 |

성공하면 빌드한 커밋을 `build\SOURCE`에 적습니다.

## WebAssembly

```sh
emcmake cmake --preset wasm
cmake --build --preset wasm --parallel
```

`build/wasm/hdtrans_engine.js`와 `hdtrans_engine.wasm`이 생깁니다(ES6 모듈). 사용법은
[api.md](api.md#webassembly)에, 이것을 쓰는 웹 데모는 [web.md](web.md)에 있습니다.

## 빌드 옵션

| 옵션 | 기본값 | 설명 |
|---|---|---|
| `HDTRANS_WERROR` | `ON` | 경고를 오류로 처리합니다 (MSVC `/WX`, clang, gcc `-Werror`). `third_party/`의 코드는 경고 없이 빌드합니다 |
| `HDTRANS_PCRE2_JIT` | `OFF` | PCRE2 JIT 컴파일러를 넣습니다. WebAssembly에서는 쓰지 않습니다 |
| `HDTRANS_FUZZ` | `OFF` | libFuzzer 대상 `hdtrans_fuzz`를 만들고 모든 타깃에 커버리지 계측을 넣습니다. Clang만 됩니다([퍼징](#퍼징)) |

`cmake --preset linux -DHDTRANS_WERROR=OFF`처럼 끌 수 있지만, 저장소의 코드는 경고 없이 빌드되어야
합니다. 경고는 플래그가 아니라 코드를 고쳐서 없앱니다.

clang과 gcc 빌드는 지역 변수가 다른 지역 변수나 매개변수를 가리는 것(MSVC `/W4`의 C4456, C4457)도 경고합니다.
gcc는 `-Wshadow=local`, clang은 `-Wshadow -Wshadow-uncaptured-local`이며, clang의 `-Wshadow`는 멤버와 전역을
가리는 것(C4458, C4459)도 잡습니다.

컴파일 플래그: C++20, 라이브러리와 DLL은 예외와 RTTI 없이 빌드합니다. MSVC 빌드는 CRT를 정적으로
링크하므로 각 DLL은 단일 파일로 배포됩니다.

## CI

`.github/workflows/`의 워크플로가 GitHub Actions에서 돌아갑니다.

- **build.yml**(Build): 풀 리퀘스트, `main`과 `develop` push, 수동 실행에서 돌고, 태그에서는 **Publish npm**이
  불러 씁니다. `linux`, `windows-x64`, `windows-x86`, `mac`, `wasm` 프리셋을 각 OS 러너에서
  빌드합니다. 모든 플랫폼에서 Ninja를 쓰고, 컴파일 캐시는 네이티브 빌드가 sccache, `wasm`이 ccache입니다
  (sccache는 Emscripten의 `-Xclang` 인자를 캐시하지 못합니다). Windows는 프리셋에 `-G Ninja`를 덧붙이고
  `vcvarsall`로 MSVC 환경을 설정합니다. 네이티브 프리셋은 `HDTRANS_DICTS=dicts`로 `ctest`를 실행하고, `wasm`은
  Node로 `tools/wasm_smoke.mjs`를 실행해 작은 사전으로 한 번 번역해 봅니다(셋째 인자로 그 사전을
  컴파일한 `.hdsnap`을 주면 그것으로도 불러 봅니다). `linux`는 비공개 테스트 데이터의 기대 출력과도 비교합니다
  ([testing.md](testing.md#ci)). `linux`는 [성능 예산](#성능-예산)도 잽니다. 결과물은 `hdtrans-<프리셋>`
  아티팩트로 올라갑니다.
- Build의 `linux-asan` 작업은 sanitizer 빌드로 단위 테스트, 예문, 규칙 사례를 돌리고, `linux-fuzz` 작업은
  [퍼저](#퍼징)를 돌립니다. 두 작업은 풀 리퀘스트와 push에서 matrix와 함께 돕니다.
- Build의 `ci-ok` 작업은 풀 리퀘스트와 push에서 항상 돌고, `gate`가 성공하지 않았거나 테스트 작업 하나가
  실패, 취소되면 실패합니다. 건너뛴 작업은 통과로 칩니다. 테스트 작업을 새로 만들면 `ci-ok`의 `needs`에 넣습니다
  (`tools/release/ci-workflow.test.mjs`가 검사합니다).
- 성능 예산과 퍼징이 실패로 치는 것과 알리기만 하는 것:

  | 단계 | 실패 | 경고만(작업은 통과) |
  |---|---|---|
  | `linux`의 Performance budget | - | 기준 커밋의 빌드보다 1% 넘게 많은 행, valgrind를 설치하지 못하거나 측정이 실패하거나 끝나지 않을 때, 기준 빌드를 찾지도 빌드하지도 못했을 때 |
  | `linux-fuzz`의 Replay | 시드나 `tests/fuzz/regressions/`의 입력 하나가 멈추거나 sanitizer 보고, 메모리 초과를 낼 때 | - |
  | `linux-fuzz`의 Fuzz | `develop` push에서 무작위 퍼징이 찾은 입력이 있을 때 | PR, `main` push, 태그 빌드(**Publish npm**)에서 찾은 입력(요약과 `fuzz-findings` 아티팩트), 퍼저 빌드 실패(같은 소스는 다른 작업이 컴파일합니다) |

  경고는 작업 요약과 주석(annotation)에 남습니다. 인프라 문제로 보이면 그 작업만 다시 실행합니다(**Re-run
  failed jobs** 또는 `gh run rerun <run> --failed`).
- Build의 `gate` job이 풀 리퀘스트와 브랜치 push에서 matrix(릴리스 도구 테스트 포함)를 돌릴지 정합니다. 아래
  경우에만 건너뛰고, 그 밖의 경우와 확인 중 오류는 전체를 빌드합니다. 태그와 수동 실행에서는 판단하지 않고 늘
  전체를 빌드합니다. 릴리스 파일은 `CHANGELOG.md`, `.release-please-manifest.json`, `release-version.txt`,
  `npm/package.json`, `npm/package-lock.json`입니다.
  - `main` push가 릴리스 봇의 버전 커밋 하나입니다. 작성자와 커미터가 봇이고, 메시지가 릴리스 커밋 형식이며,
    부모가 `Release-Source`의 커밋이고, 릴리스 파일만 고치거나 더합니다. `Release-Build`의 run은 그 부모를
    빌드해 성공한 `main` push Build입니다. 같은 커밋을 태그 빌드가 테스트합니다.
  - `main` push의 트리를 기록한 Build가 있습니다.
  - `develop`→`main` PR의 병합 트리가 `develop` head 트리와 릴리스 파일만 다르고(`npm/package.json`과 lock은
    버전만, 삭제 없음), `develop` 트리를 기록한 Build가 있습니다.

  기록은 `tested-tree-<트리 해시>` 아티팩트(30일)입니다. matrix가 통과한 `develop` push의 Build와 이 저장소의
  `develop`→`main` PR Build(셋째 경우 포함)만 남기고, gate도 이 저장소의 성공한 그 두 종류 run의 기록만 씁니다.
- 컴파일 캐시는 `develop` push와 풀 리퀘스트, `develop`에서의 수동 실행만 저장합니다. `main` push, 태그, 그 밖의
  수동 실행은 읽기만 하므로 `develop`의 최신 캐시를 씁니다. Emscripten(`EMSDK_VERSION`)은 버전과 러너마다 캐시
  한 벌을 모든 워크플로가 함께 쓰고, ccache는 컴파일러를 그 버전으로 식별합니다.
- **release.yml**: `build.yml`의 결과를 받아 Windows ZIP과 Linux, macOS tar.gz, `SHA256SUMS.txt`를
  만듭니다. Windows는 x86 `J2KEngine.dll`, x64 CLI, GUI, C API와 별도의 x86 CLI, C API를 담습니다.
  WASM 아티팩트에는 엔진, TypeScript용 JavaScript와 `.d.ts`를 포함합니다. 빌드한 엔진, JavaScript 래퍼와 기본 사전을 담은 npm 패키지는 `hdTrans-<버전>-wasm.tgz`로 릴리스에 첨부합니다.
- **release-please.yml**(Release main): `main` push의 Build가 성공하면 버전 커밋, 태그, GitHub Release 초안을 만듭니다.
- **publish-npm.yml**(Publish npm): `v*` 태그 push에서 Build 전체를 실행하고, 그 run의 패키지로 GitHub Release와
  npm(OIDC, provenance)에 게시합니다.
- **deploy-web.yml**(Deploy web): Publish npm이 성공하면 그 릴리스의 CLI와 npm 패키지로 웹 데모를 배포합니다
  ([web.md](web.md#자동-배포)).
- **pr-title.yml**: PR 제목을 검사합니다.

로컬 빌드에는 sccache와 ccache 모두 필요 없습니다.

## 성능 예산

`tools/perf-budget.py`는 고정 입력을 번역하는 CLI의 명령어 수를 valgrind(`cachegrind --cache-sim=no`)로 셉니다.
벽시계 시간이 아니라서 같은 빌드는 어느 러너에서 재도 수가 거의 같습니다.

| 행 | 입력 |
|---|---|
| `load` | 빈 입력. 사전 읽기 |
| `each` | 규칙 사례(`tests/rules/cases`, `tests/rules/speech-strict`)의 입력을 `--each`로 |
| `document` | `examples/*.ja.txt`(첫 줄 제외)를 한 문서로 |

- 입력은 기준 커밋에서 읽으므로 두 빌드가 같은 글을 번역합니다. 사전은 각 빌드의 것을 쓰므로 사전을 바꾸면 세 행이
  모두 움직일 수 있습니다. `each`와 `document`는 `load`를 뺀 수입니다.
- 수는 컴파일러, C 라이브러리, 아키텍처, valgrind(도구 체인)에 따라 다르므로 두 빌드를 한 러너에서 함께 잽니다.
- `--report CLI DICTS --against CLI DICTS --base REF`는 두 빌드를 함께 재고 행마다 바뀐 비율을 적습니다. 1% 넘게
  많은 행은 `over:`, 1% 넘게 적은 행은 `under:`로 시작하는 줄을 더합니다. 기준 빌드를 재지 못하면 이 빌드의 수만
  적습니다. 종료 코드는 잰 경우 늘 0이고, 이 빌드를 재지 못하면(멈춤 포함) 2입니다. `--measure CLI DICTS`는 한
  빌드의 수만 출력합니다. `--base`를 주지 않으면 develop과의 병합 기준 커밋을 씁니다.
- 성능 예산은 재고 알리기만 하고 빌드를 실패시키지 않습니다. CI의 `linux` 작업은 PR에서 기준 커밋(PR 빌드가
  병합한 기준 브랜치의 커밋, 곧 병합 커밋의 첫 부모)의 linux 빌드를 같은 러너에서 함께 재고, 표를 작업 요약에
  적습니다. 1% 넘게 많은 행과 인프라 문제는 경고가 됩니다. 단계가 시간을 넘겨도 작업은 통과합니다.
  `tools/perf-base.py`가 기준 빌드를 찾습니다.
  - 그 커밋의 push Build가 올린 `hdtrans-linux` 아티팩트(`linux` 작업의 테스트가 통과하면 run이 끝나기 전에도
    올라갑니다), 없으면 같은 트리를 기록한 `tested-tree-<트리>` run의 아티팩트를 씁니다.
  - push Build가 아직 돌고 있으면 30초마다 다시 보며 10분까지 기다립니다.
  - 그래도 없거나 컴파일러가 다르면 그 커밋을 작업 안에서 빌드합니다. 빌드도 실패하면 이 빌드의 수만 적고 경고합니다.
- push에서는 재지 않습니다. develop → main 승격 PR의 표가 릴리스 주기 전체의 변화이고, 크게 늘었으면 릴리스
  리뷰에서 살펴봅니다.
- 로컬에서 따로 잴 필요는 없습니다. 경고를 다시 보려면 `linux` 작업만 다시 실행합니다. 같은 빌드의 수는 다시
  재도 같습니다.

## 퍼징

`tests/fuzz/translate.cpp`는 libFuzzer 대상입니다. 입력 하나를 새 세션에서 문서로 번역하고, 입력의 해시로
말투(soft, strict)와 같은 세션에서 한 번 더 번역할지를 고릅니다. 변이기는 글자 단위로 가나, 한자, 반각 가나,
문장부호, 루비 표기(`｜漢字《かんじ》`, `<ruby>`), 한 글자의 긴 반복, 공백 덩어리, 줄바꿈, 잘못된 UTF-8을
넣거나 지우고 구간을 반복, 교환합니다. 가끔은 libFuzzer의 바이트 변이를 씁니다. `HDTRANS_FUZZ`를 켜지 않은
빌드도 이 소스를 컴파일합니다.

```sh
CC=clang CXX=clang++ cmake --preset linux-asan -B build/linux-fuzz -DHDTRANS_FUZZ=ON
cmake --build build/linux-fuzz --target hdtrans_fuzz hdtrans_cli
HOME=$(mktemp -d) HDTRANS_CACHE_DIR=off build/linux-fuzz/hdtrans --dicts "$PWD/dicts" \
  --compile-snapshot "$PWD/build/linux-fuzz/dicts.hdsnap"
python3 -I tools/fuzz.py build/linux-fuzz/hdtrans_fuzz build/linux-fuzz/dicts.hdsnap --replay
python3 -I tools/fuzz.py build/linux-fuzz/hdtrans_fuzz build/linux-fuzz/dicts.hdsnap --seconds 150
```

- 시드는 예문 본문, 규칙 사례 입력 25개마다 하나, `tests/fuzz/regressions/`의 파일, 긴 반복, 공백 덩어리,
  루비, 짝 없는 괄호로 만든 짧은 글입니다.
- `--replay`는 시드를 하나씩 60초 제한으로 번역하고 가장 느린 시드의 시간을 보입니다. 결과는 트리에만
  달렸습니다.
- 무작위 퍼징은 먼저 같은 재생을 하고, 프로세서 수만큼 퍼저를 돌립니다(`--jobs`, 난수 시드는 `--seed`부터
  하나씩). 입력 하나가 가장 느린 시드의 10배(적어도 10초, `--timeout`으로 바꿉니다)를 넘으면 멈춤으로 봅니다.
  ASan, UBSan 보고와 메모리 초과도 실패입니다. CI는 실행 번호를 난수 시드로 쓰므로(다시 실행해도 같습니다)
  `--seed <실행 번호> --jobs <작업 수>`(요약의 `fuzz:` 줄)로 같은 변이를 되풀이합니다. 찾는 입력은 기계 속도에 따라 다를 수 있습니다.
- 실패한 입력은 `--out`(기본 `build/fuzz`)에 남고 이스케이프한 글과 16진수로 출력됩니다. CI에서는 작업 요약과
  `fuzz-findings` 아티팩트에 있습니다. 고친 뒤 그 입력을 `tests/fuzz/regressions/`에 넣고 번역을 규칙 사례로
  더합니다. 그러면 PR마다 재생 단계와 sanitizer 작업의 규칙 사례가 같은 오류를 막습니다.
- `HDTRANS_FUZZ`는 Clang과 `HDTRANS_SANITIZE`가 있어야 합니다. 커버리지 계측의 기본 함수가 sanitizer 런타임에
  있어서 같은 빌드의 CLI도 링크됩니다.

## 릴리스

`develop`을 `main`에 병합하는 PR의 제목이 다음 버전을 정합니다. 커밋과 PR 제목, 병합 방법은
[contributing.md](contributing.md)에 있습니다. 병합 커밋의 Build가 성공하면 **Release main**이 버전 파일과
`CHANGELOG.md`를 커밋해 `main`에 push하고, `vX.Y.Z` 태그와 GitHub Release 초안을 만듭니다. 태그 push의
**Publish npm**이 그 커밋을 전체 플랫폼에서 빌드, 테스트한 뒤 패키지와 SHA-256 목록을 Release에 올려 게시하고
같은 패키지를 npm에 게시합니다.
`develop` 푸시도 빌드와 테스트 뒤 패키지를 만들지만 `release-packages` CI 아티팩트로만 올리며,
GitHub Release를 만들거나 태그를 옮기지 않습니다.

Actions의 **Build → Run workflow**에서 `dry_run=true`로 실행하면 패키지를 `release-packages`
아티팩트로만 올립니다. 정식 태그를 선택하고 `dry_run=false`로 실행하면 첨부 파일을 다시 올릴 수 있습니다.
공개 배포는 `main`에 포함된 `vX.Y.Z` 태그에서만 허용하며, 브랜치에서 수동 실행하면 공개하지 않습니다.
사용자 파일 견본의 스테이징 목록은 `build.yml`의 `HDTRANS_USER_FILES`에서 관리합니다.

버전은 빌드한 커밋에 붙은 `v` 태그에서 앞의 `v`를 뗀 값입니다(`cmake/detect_version.cmake`).
릴리스 태그가 붙지 않은 커밋은 `dev`이고, 짧은 커밋 해시를 따로 둡니다.
Git 정보가 없는 소스 압축본은 릴리스 봇이 관리하는 `release-version.txt`를 쓰며, 그 파일도 없으면 `dev`입니다.
C API의 `hdTrans_Version`, TypeScript의 `version`에는 엔진 버전만 쓰고, Windows EXE와 DLL의 버전 문자열과
CLI에는 해시를 붙여 `dev (2757ed12)`처럼 씁니다. Windows의 숫자 버전은 `dev`일 때 `0.0.0.0`입니다.
패키지 이름에 쓰는 `hdtrans-version.txt`는 `0.5.1` 또는 `dev-2757ed12`입니다.
CLI의 `--version`은 선택한 사전도 로드해서 `hdTrans v<엔진> (native 또는 wasm) / dict <사전>`을 한 줄로 출력합니다.
`J2KEngine.config.yaml`은 아직 읽지 않으므로 배포하지 않습니다. 설정은 `user/hdtrans.config.yaml`을 씁니다.
