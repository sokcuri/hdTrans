# 하드트랜스 (hdTrans)

일본어를 한국어로 번역하는 오프라인 번역 프로그램입니다.
기본 사전과 번역 엔진이 함께 들어 있어 압축을 풀면 바로 쓸 수 있습니다.
Windows 번역 창, 명령줄 프로그램, 이지트랜스용 도구 연결을 제공합니다.

제작자: 소쿠릿 ([홈페이지](https://sokcuri.neko.kr/), [X](https://x.com/sokcuri))

## 웹에서 써 보기

설치 전에 [웹 번역기](https://trans.kuri.kr)에서 문장을 번역해 볼 수 있습니다.

## 번역 개선 예시

원본 ezTrans 열은 [공개 비교 글](https://x.com/sokcuri/status/2111443062698451143/photo/1)에서 인용했고, hdTrans 열은 아래 벤치마크의 고정 C++ 엔진으로 직접 번역한 결과입니다.

| 일본어 | 원본 ezTrans | hdTrans |
|---|---|---|
| 肉がジュージューと焼ける | 고기가 유태인 유태인과 탄다 | 고기가 지글지글 구워진다 |
| バイトに行く | 바이트에 간다 | 아르바이트에 간다 |
| おっと、すまんね | 남편, 미안 | 어이쿠, 미안 |
| 夢みたい | 꿈 보고 싶다 | 꿈같다 |
| くそっ！ | 똥! | 젠장! |
| やったー！ | 했다-! | 해냈다! |

## 벤치마크

공개 회귀 벤치마크 66문장에서 기대 출력 일치는 0.2.0 소스 빌드의 36/66에서 개선 C++ 엔진의 66/66으로 늘었습니다.
수정 대상 30문장을 고쳤고, 유지 대상 30문장과 위 6개 예시는 같은 출력을 유지했습니다.
수정 사례와 주변 표현을 골라 만든 테스트이며 일반 번역 정확도를 뜻하지 않습니다.

[전체 결과, 사전과 규칙 개선 내용, 재현 방법](docs/benchmarks.md)

## 다운로드와 설치

Windows 번역 창이나 네이티브 엔진을 쓰는 도구에 연결하려면 배포판을 사용합니다.

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

기본 사전은 Windows에서 `hdTrans\dicts.hdsnap`, Linux와 macOS에서 프로그램 옆 `dicts.hdsnap`입니다.
같은 폴더의 `dicts.txt`는 사전 버전입니다. 원본 TSV나 `dicts/` 폴더를 따로 받을 필요가 없습니다.

## 시작하기

### npm으로 시작하기

Node.js 22 이상에서 설치합니다. WASM 엔진과 기본 사전이 패키지에 포함되어 있습니다.

```sh
npm install hdtrans
```

JavaScript와 TypeScript에서 같은 API를 씁니다.

```js
import { HdTrans } from 'hdtrans';

const translator = await HdTrans.init();
try {
  console.log(translator.getInfo()); // 엔진 버전, 사전 버전, 실행 방식
  console.log(translator.translate('今日は本を読みます。'));
} finally {
  translator.close();
}
```

버전만 확인하려면 `await HdTrans.getInfo()`를 씁니다. 엔진과 사전을 실제로 로드해 확인하고 임시 인스턴스를 닫습니다.
[사용자 사전 등록](docs/typescript-api.md#사용자-사전), [JSON 결과](docs/typescript-api.md#번역과-json)와 자세한 사용법은 [JavaScript와 TypeScript API](docs/typescript-api.md)를 보세요.

### 명령줄로 시작하기

npm으로 명령을 설치할 수 있습니다.

```sh
npm install -g hdtrans
hdtrans --version
hdtrans --in input.ja.txt --out output.ko.txt
```

배포판을 받았다면 터미널에서 압축을 푼 폴더로 이동합니다.
입력 파일 `input.ja.txt`를 읽어 번역 결과를 `output.ko.txt`에 저장합니다. 입력과 출력은 **UTF-8**입니다.

Windows:

```powershell
.\hdtrans.exe --in input.ja.txt --out output.ko.txt
```

Linux, macOS:

```sh
./hdtrans --in input.ja.txt --out output.ko.txt
```

기본 사전은 자동으로 읽습니다. 배포판에서 줄마다 독립적으로 번역하려면 `--each`를 추가합니다.
[npm CLI와 배포판 CLI 사용법](docs/api.md#명령줄-hdtrans)

## 도구로 사용하기

### Windows 번역 창

`hdTransGUI.exe`를 실행하고 위 칸에 일본어를 넣은 뒤 **번역하기**를 누릅니다.
텍스트 파일은 **문서 번역**에서 파일을 골라 번역합니다.

### 이지트랜스용 도구에 연결

Windows 배포판의 네이티브 엔진을 LunaTranslator, 투컨트롤, 아네모네, XUnity.AutoTranslator, 아랄트랜스, VNR 등에 연결할 수 있습니다.
도구에 엔진 경로를 설정하면 원본 이지트랜스를 따로 설치할 필요가 없습니다.

- `J2KEngine.dll`과 `hdTrans` 폴더를 함께 둡니다.
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
[Ehnd 설정 안내](docs/ehnd-compat.md)

## 업데이트

실행 중인 번역 창과 연결 도구를 끄고 새 버전을 같은 폴더에 덮어 풉니다.
직접 만든 `userdict*.txt`와 `hdtrans.config.yaml`은 배포 파일에 없으므로 유지됩니다.
`*.example.*` 견본 파일은 업데이트 때 교체되니 견본을 직접 수정하지 말고 복사해서 쓰세요.

## 문제 해결

- **DLL을 불러오지 못함:** 도구에 지정한 경로에 `J2KEngine.dll`이 있는지 확인합니다.
- **사전을 찾지 못함:** Windows에서는 DLL이나 실행 파일 옆 `hdTrans\dicts.hdsnap`이 있는지 확인합니다. Linux와 macOS는 실행 파일 옆에 사전을 둡니다. DLL만 복사했다면 `hdTrans` 폴더도 복사하세요.
- **계속 예전 엔진으로 번역됨:** 도구의 엔진 경로를 확인합니다. XUnity는 `J2KEngineH.dll`을 먼저 읽으므로 기존 파일이 있으면 제거합니다.
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
