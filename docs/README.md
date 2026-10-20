# 하드트랜스 문서

## 사용 안내

처음 쓰신다면 [다운로드와 기본 사용법](../README.md)을 먼저 보세요.

| 문서 | 내용 |
|---|---|
| [도구와 함께 쓰기](using-with-tools.md) | 배포판 설치, 번역 도구별 연결 설정 |
| [JavaScript와 TypeScript API](typescript-api.md) | npm 설치, 초기화, 버전 조회와 번역 |
| [사용자 사전](dictionary-files.md#사용자-사전) | 사용자 사전 경로, 파일 형식과 품사 |
| [CLI 옵션](api.md#명령줄-hdtrans) | 파일 입력과 출력, 줄별 번역, 명령줄 옵션 |
| [설정 파일](api.md#설정-파일) | 사용자 설정 경로와 옵션 |
| [Ehnd 호환 안내](ehnd-compat.md) | Ehnd 사전과 필터를 켜는 방법, 지원 형식 |
| [번역 품질 벤치마크](benchmarks.md) | 오역 개선 전후, 공개 테스트 결과와 재현 방법 |

## 개발 참고

빌드하거나 다른 프로그램에 엔진을 넣는 사람을 위한 문서입니다.
기여할 때는 [기여 안내](contributing.md)와 [코드 작성 규칙](../AGENTS.md)을 따릅니다.

| 문서 | 내용 |
|---|---|
| [빌드](build.md) | 요구 사항, CMake 프리셋, Windows와 WebAssembly 빌드 |
| [기여 안내](contributing.md) | 브랜치, 커밋과 PR, 버전과 릴리스 |
| [오역 고치기](fixing-translations.md) | 번역 진단, 사전과 규칙 수정, 회귀 테스트 |
| [테스트](testing.md) | 단위 테스트, 예제, 기준선과 엔드투엔드 테스트 |
| [사전 파일](dictionary-files.md) | 기본 사전 원본, 스냅샷, 사용자 사전과 캐시 |
| [번역 파이프라인](pipeline-layers.md) | 처리 단계, 사전과 규칙 적용 순서, 진단 출력 |
| [규칙 언어](rule-language.md) | 단어 선택 규칙의 형식과 연산자 |
| [사전 코드](dictionary-codes.md) | 품사, 의미와 문법 코드 일람 |
| [규칙 컴파일러](rule-compiler.md) | `hdrulec` 검사와 C++ 코드 생성 |
| [Ehnd 호환 참고](ehnd-compat.md) | 필터, SkipLayer와 사용자 사전 적용 순서 |
| [엔진 API](api.md) | C API, J2K 함수, WebAssembly와 C++ API |
| [TypeScript API](typescript-api.md) | npm, JavaScript와 TypeScript 사용법 |
| [도구 호환성](tool-compatibility.md) | 로더와 레지스트리, ABI와 인코딩, 호스트 테스트 범위, Python ctypes |
| [웹 번역기](web.md) | 웹 앱 빌드와 배포 |

## 저장소 구성

| 경로 | 내용 |
|---|---|
| `include/core/` | 공개 C++ 헤더와 C API 헤더 |
| `src/core/` | 번역 엔진과 처리 단계 |
| `src/cli/` | `hdtrans` 명령줄 프로그램 |
| `src/rulec/` | `hdrulec` 규칙 컴파일러 |
| `src/cshared/` | `hdTransEngine` C API |
| `src/j2kengine/` | `J2KEngine.dll`의 `J2K_*` 함수 |
| `src/wasm/` | WebAssembly 연결 코드 |
| `web/` | [웹 번역기](https://trans.kuri.kr) |
| `dicts/` | 기본 사전 원본 TSV. 배포판에서는 `dicts.hdsnap`으로 제공 |
| `tests/unit/` | 단위 테스트 |
| `tests/host/` | C API와 J2K DLL 호스트 테스트 |
| `examples/` | 번역 입력과 출력 예제 |
| `tools/` | 빌드, 배포, 번역 진단과 테스트 도구 |
