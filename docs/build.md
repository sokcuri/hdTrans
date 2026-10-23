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
| `linux` | Ninja | `build/linux` | Linux Release |
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

macOS에서 Command Line Tools만 쓰면 컴파일러 확인 단계에서 실패할 수 있습니다. 그때는 Xcode를 쓰게 합니다:
`export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`.

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

`cmake --preset linux -DHDTRANS_WERROR=OFF`처럼 끌 수 있지만, 저장소의 코드는 경고 없이 빌드되어야
합니다. 경고는 플래그가 아니라 코드를 고쳐서 없앱니다.

컴파일 플래그: C++20, 라이브러리와 DLL은 예외와 RTTI 없이 빌드합니다. MSVC 빌드는 CRT를 정적으로
링크하므로 각 DLL은 단일 파일로 배포됩니다.

## CI

`.github/workflows/`의 워크플로가 GitHub Actions에서 돌아갑니다.

- **build.yml**: `linux`, `windows-x64`, `windows-x86`, `mac`, `wasm` 프리셋을 각 OS 러너에서
  빌드합니다. 모든 플랫폼에서 Ninja를 쓰고, 컴파일 캐시는 네이티브 빌드가 sccache, `wasm`이 ccache입니다
  (sccache는 Emscripten의 `-Xclang` 인자를 캐시하지 못합니다). Windows는 프리셋에 `-G Ninja`를 덧붙이고
  `vcvarsall`로 MSVC 환경을 설정합니다. 네이티브 프리셋은 `HDTRANS_DICTS=dicts`로 `ctest`를 실행하고, `wasm`은
  Node로 `tools/wasm_smoke.mjs`를 실행해 작은 사전으로 한 번 번역해 봅니다(셋째 인자로 그 사전을
  컴파일한 `.hdsnap`을 주면 그것으로도 불러 봅니다). 결과물은
  `hdtrans-<프리셋>` 아티팩트로 올라갑니다.
- **release.yml**: `build.yml`의 결과를 받아 Windows ZIP과 Linux, macOS tar.gz, `SHA256SUMS.txt`를
  만듭니다. Windows는 x86 `J2KEngine.dll`, x64 CLI, GUI, C API와 별도의 x86 CLI, C API를 담습니다.
  WASM 아티팩트에는 엔진, TypeScript용 JavaScript와 `.d.ts`를 포함합니다. 빌드한 엔진, JavaScript 래퍼와 기본 사전을 담은 npm 패키지는 `hdTrans-<버전>-wasm.tgz`로 릴리스에 첨부합니다.
- **e2e.yml**: 풀 리퀘스트와 `main`, `develop` 푸시 때, 현재 커밋의 CLI로 비공개 테스트 데이터를 번역해 기대 출력과
  달라진 줄을 보고합니다. 자세한 내용은 [testing.md](testing.md#ci)에 있습니다.

로컬 빌드에는 sccache와 ccache 모두 필요 없습니다.

## 릴리스

`develop`을 `main`에 병합하면 release-please가 여러 변경을 릴리스 PR 하나에 모읍니다.
그 PR을 합칠 때만 버전, `CHANGELOG.md`, 태그와 GitHub Release를 만듭니다.
`fix`는 patch, `feat`는 minor, `!`나 `BREAKING CHANGE:`는 major를 올립니다. 첫 GitHub 릴리스 버전은 `0.1.1`입니다.
커밋과 PR 제목, 병합 방법은 [contributing.md](contributing.md)에 있습니다.

release-please가 태그를 만든 뒤 **Build**를 명시적으로 실행합니다. 전체 플랫폼 빌드와 테스트가 통과하면
패키지와 SHA-256 목록을 기존 GitHub Release에 첨부하며, release-please의 릴리스 노트는 유지합니다.
`develop` 푸시도 빌드와 테스트 뒤 패키지를 만들지만 `release-packages` CI 아티팩트로만 올리며,
GitHub Release를 만들거나 태그를 옮기지 않습니다.

Actions의 **Build → Run workflow**에서 `dry_run=true`로 실행하면 패키지를 `release-packages`
아티팩트로만 올립니다. 정식 태그를 선택하고 `dry_run=false`로 실행하면 첨부 파일을 다시 올릴 수 있습니다.
공개 배포는 `main`에 포함된 `vX.Y.Z` 태그에서만 허용하며, 브랜치에서 수동 실행하면 공개하지 않습니다.
사용자 파일 견본의 스테이징 목록은 `build.yml`의 `HDTRANS_USER_FILES`에서 관리합니다.

버전은 `git describe --tags`로 찾은 가장 가까운 `v` 태그에서 앞의 `v`를 뗀 값입니다.
태그가 없으면 `0.0.0-<짧은 SHA>`입니다. Git 정보가 없는 소스 압축본은 release-please가 관리하는
`release-version.txt`를 쓰며, 그 파일도 없으면 `0.0.0-unknown`입니다.
C API의 `hdTrans_Version`, TypeScript의 `version`, Windows EXE와 DLL의 버전 정보에 같은 엔진 버전 문자열을 씁니다.
CLI의 `--version`은 선택한 사전도 로드해서 `hdTrans v<엔진> (native 또는 wasm) / dict <사전>`을 한 줄로 출력합니다.
`J2KEngine.config.yaml`은 아직 읽지 않으므로 배포하지 않습니다. 설정은 `user/hdtrans.config.yaml`을 씁니다.
