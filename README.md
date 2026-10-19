# 하드트랜스 (hdTrans)

일본어를 한국어로 번역하는 오프라인 번역 프로그램입니다.
기본 사전과 번역 엔진이 함께 들어 있어 압축을 풀면 바로 쓸 수 있습니다.
Windows 번역 창, 명령줄 프로그램, 이지트랜스용 도구 연결을 제공합니다.

제작자: 소쿠릿 ([홈페이지](https://sokcuri.neko.kr/), [X](https://x.com/sokcuri))

## 웹에서 써 보기

설치 전에 [웹 번역기](https://trans.kuri.kr)에서 문장을 번역해 볼 수 있습니다.

## 다운로드와 설치

[Releases](https://github.com/sokcuri/hdTrans/releases)에서 운영체제에 맞는 파일을 받습니다.
`Source code`가 아니라 아래 배포 파일을 고르세요.

| 운영체제 | 다운로드 파일 | 사용 방법 |
|---|---|---|
| Windows | `hdTrans-<버전>-windows.zip` | 번역 창, CLI, 이지트랜스 도구 연결 |
| Linux | `hdTrans-<버전>-linux-<CPU>.tar.gz` | CLI |
| macOS | `hdTrans-<버전>-macos-<CPU>.tar.gz` | CLI |

1. 원하는 폴더에 압축을 풉니다. 별도 설치 프로그램이나 소스 빌드는 필요 없습니다.
2. Windows에서는 `hdTransGUI.exe`를 실행합니다. Linux와 macOS에서는 아래 CLI 사용법을 따릅니다.
3. 다른 도구에 연결하려면 [도구별 설치 안내](docs/using-with-tools.md)를 보세요.

Windows ZIP 하나에 64비트 GUI와 CLI, **32비트 `J2KEngine.dll`**이 함께 들어 있습니다.
32비트 CLI는 `hdtrans_x86.exe`입니다. 개발자용 C API는 `hdTransEngine.dll`과 `hdTransEngine_x86.dll`입니다.
Linux와 macOS는 CPU에 맞는 배포 파일을 고릅니다. `x64`는 Intel/AMD 64비트, `arm64`는 Apple Silicon 등의 ARM 64비트입니다.

기본 사전은 Windows에서 `hdTrans\dicts.hdsnap`, Linux와 macOS에서 프로그램 옆 `dicts.hdsnap`입니다.
같은 폴더의 `dicts.txt`는 사전 버전입니다. 원본 TSV나 `dicts/` 폴더를 따로 받을 필요가 없습니다.

## 사용법

### Windows 번역 창

`hdTransGUI.exe`를 실행하고 위 칸에 일본어를 넣은 뒤 **번역하기**를 누릅니다.
텍스트 파일은 **문서 번역**에서 파일을 골라 번역합니다.

### CLI로 파일 번역

터미널에서 압축을 푼 폴더로 이동합니다. 입력 파일 `input.ja.txt`를 읽어 번역 결과를 `output.ko.txt`에 저장합니다.
입력과 출력은 **UTF-8**입니다.

Windows:

```powershell
.\hdtrans.exe --in input.ja.txt --out output.ko.txt
```

Linux, macOS:

```sh
./hdtrans --in input.ja.txt --out output.ko.txt
```

기본 사전은 자동으로 읽습니다. 줄마다 독립적으로 번역하려면 `--each`를 추가합니다.
[전체 CLI 옵션](docs/api.md#명령줄-hdtrans)

### 이지트랜스용 도구에 연결

LunaTranslator, 아네모네, XUnity.AutoTranslator, 아랄트랜스, VNR 등의 엔진 경로를 하드트랜스 폴더로 지정합니다.
도구에 엔진 경로를 설정하면 원본 이지트랜스를 따로 설치할 필요가 없습니다.

- `J2KEngine.dll`과 **`hdTrans` 폴더를 함께** 둡니다.
- 기존 이지트랜스 폴더에 복사한다면 원래 DLL부터 백업합니다.
- 도구에 따라 DLL 파일이나 폴더를 선택합니다. [도구별 설치 안내](docs/using-with-tools.md)의 설정을 따르세요.

## 사용자 사전

프로그램 폴더를 기준으로 아래 파일을 만듭니다.

| 운영체제 | 사용자 사전 경로 |
|---|---|
| Windows | `hdTrans\user\userdict.txt` |
| Linux, macOS | `user/userdict.txt` |

UTF-8로 저장하고 한 줄에 일본어와 한국어를 **탭**으로 구분해 적습니다.

```text
灯台	등대
山道	산길
```

`userdict-place.txt`처럼 `userdict`로 시작하는 `.txt` 파일로 나누어도 됩니다.
번역 창과 연결 도구에서는 저장한 내용이 다음 번역부터 반영됩니다.
품사와 자세한 형식은 [사용자 사전 안내](docs/dictionary-files.md#사용자-사전)를 보세요.

## 설정과 Ehnd 사전

Windows는 `hdTrans\user\hdtrans.config.example.yaml`, Linux와 macOS는 `user/hdtrans.config.example.yaml`을
같은 폴더의 `hdtrans.config.yaml`로 복사해 수정합니다.

꿀도르 같은 Ehnd 사전은 **기본으로 꺼져 있습니다**. 필요할 때 설정에서 켤 수 있지만 번역 품질이 낮아질 수 있습니다.
원본 Ehnd DLL을 덮어쓰는 방식은 쓰지 않습니다. [Ehnd 설정 안내](docs/ehnd-compat.md)

## 업데이트

실행 중인 번역 창과 연결 도구를 끄고 새 버전을 같은 폴더에 덮어 풉니다.
직접 만든 `userdict*.txt`와 `hdtrans.config.yaml`은 배포 파일에 없으므로 유지됩니다.
`*.example.*` 견본 파일은 업데이트 때 교체되니 견본을 직접 수정하지 말고 복사해서 쓰세요.

## 문제 해결

- **DLL을 불러오지 못함:** 이지트랜스 연결용 `J2KEngine.dll`은 32비트입니다. 도구 본체가 64비트여도 32비트 엔진을 사용하는 설정을 따르세요.
- **사전을 찾지 못함:** Windows에서는 DLL이나 실행 파일 옆 `hdTrans\dicts.hdsnap`이 있는지 확인합니다. Linux와 macOS는 실행 파일 옆에 사전을 둡니다. DLL만 복사했다면 `hdTrans` 폴더도 복사하세요.
- **계속 예전 엔진으로 번역됨:** 도구의 엔진 경로를 확인합니다. XUnity는 기존 `J2KEngineH.dll`을 먼저 읽으므로 백업 후 다른 이름으로 옮깁니다.
- **사용자 사전이 적용되지 않음:** 파일 경로와 `userdict`로 시작하는 이름, UTF-8 인코딩, 항목 사이의 탭을 확인합니다.

## 문의와 오역 신고

[이슈](https://github.com/sokcuri/hdTrans/issues/new/choose)에 사용한 버전, 도구 이름, 재현 방법을 적어 주세요.

## 개발과 기여

- [문서 목록](docs/README.md): 사용 안내, 빌드와 API
- [오역 고치기](docs/fixing-translations.md): 사전과 번역 규칙 수정
- [기여 안내](docs/contributing.md): PR은 `develop` 브랜치로 보냅니다.

## 라이선스

[AGPL-3.0-or-later](LICENSE)

외부 라이브러리의 라이선스는 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)에 있습니다.
