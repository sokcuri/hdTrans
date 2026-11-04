# 하드트랜스 (hdTrans)

일본어를 한국어로 옮기는 오프라인 번역기입니다. 인터넷 없이 내 컴퓨터에서 돌아가고,
이지트랜스를 쓰던 게임과 번역 도구에 그대로 연결해 쓸 수 있습니다.

제작자: 소쿠릿 ([홈페이지](https://sokcuri.neko.kr/), [X](https://x.com/sokcuri))

설치하기 전에 [웹 번역기](https://trans.kuri.kr)에서 바로 써 볼 수 있습니다.

| 일본어 | 이지트랜스 | 하드트랜스 |
|---|---|---|
| 肉がジュージューと焼ける | 고기가 유태인 유태인과 탄다 | 고기가 지글지글 구워진다 |
| バイトに行く | 바이트에 간다 | 아르바이트에 간다 |
| おっと、すまんね | 남편, 미안 | 어이쿠, 미안 |
| 夢みたい | 꿈 보고 싶다 | 꿈 같다 |
| くそっ！ | 똥! | 젠장! |
| やったー！ | 했다-! | 앗싸! |

이지트랜스 결과는 [공개 비교 글](https://x.com/sokcuri/status/2111443062698451143/photo/1)에서 가져왔고, 하드트랜스 결과는 0.8.4로 번역했습니다.
더 많은 비교는 [번역 품질 벤치마크](docs/benchmarks.md)에 있습니다.

## 특징

사전과 문법 규칙으로 번역합니다. 틀리는 문장을 하나씩 찾아 사전과 규칙을 사람이 직접 고쳐 왔고,
고칠 때마다 소설, 게임, 자막 십만여 줄을 다시 번역해 다른 문장이 망가지지 않았는지 확인합니다.

- 대사의 말투를 문맥에 맞춰 반말과 존댓말 가운데에서 고릅니다.
- 작품의 인물과 지명을 목록에 적어 두면 이름으로 읽고 경칭, 조사, 띄어쓰기까지 맞춥니다.
- 기계학습을 쓰지 않습니다. 같은 문장은 언제 넣어도 같은 결과가 나오고, 틀린 곳은 원인이 된 사전 행이나 규칙을 찾아 고칩니다.

## 내려받기

[최신 릴리스](https://github.com/sokcuri/hdTrans/releases/latest)에서 운영체제에 맞는 파일을 받습니다.
목록 맨 아래의 `Source code`는 받지 않아도 됩니다.

| 운영체제 | 파일 | 들어 있는 것 |
|---|---|---|
| Windows | `hdTrans-<버전>-windows.zip` | 번역 창, 도구 연결용 DLL, 명령줄 |
| Linux | `hdTrans-<버전>-linux-<CPU>.tar.gz` | 명령줄 |
| macOS | `hdTrans-<버전>-macos-<CPU>.tar.gz` | 명령줄 |

설치 과정은 없습니다. 원하는 폴더에 압축을 풀면 끝입니다.

새 버전이 나오면 번역 창과 연결한 도구를 끄고 같은 폴더에 덮어 풉니다. 직접 만든 사용자 사전과 설정 파일은
배포 파일에 없으므로 그대로 남습니다.

## 쓰는 방법

### 번역 창 (Windows)

`hdTransGUI.exe`를 실행하고 위 칸에 일본어를 넣은 뒤 **번역하기**를 누릅니다.
텍스트 파일은 **문서 번역** 탭에 끌어다 놓으면 원본 옆에 `이름_번역.txt`로 저장됩니다.
**사전** 탭에서는 사용자 사전을 켜고 끄거나 고칠 수 있습니다.

### 게임과 번역 도구에 연결 (Windows)

LunaTranslator, 투컨트롤, 아네모네, XUnity.AutoTranslator, 아랄트랜스, VNR처럼 이지트랜스를 쓰는 도구에서
엔진 경로를 하드트랜스로 바꾸면 됩니다. 이지트랜스를 따로 설치할 필요는 없습니다.

`J2KEngine.dll`과 `hdtrans-data` 폴더는 항상 같은 곳에 함께 둡니다. 도구마다 고르는 파일이 조금씩 달라서
[도구별 연결 방법](docs/using-with-tools.md)에 따로 정리해 두었습니다.

### 명령줄

입력과 출력은 UTF-8 텍스트 파일입니다.

```sh
./hdtrans --in input.ja.txt --out output.ko.txt     # Windows는 .\hdtrans.exe
```

기본은 파일 전체를 한 문서로 번역합니다. 줄마다 따로 번역하려면 `--each`를 붙입니다.
옵션 전체는 [명령줄 안내](docs/api.md#명령줄-hdtrans)에 있습니다.

### JavaScript, TypeScript

Node.js 22 이상에서 쓸 수 있습니다. 엔진(WASM)과 사전이 패키지에 들어 있습니다.

```sh
npm install hdtrans
```

```js
import { HdTrans } from 'hdtrans';

const translator = await HdTrans.init();
console.log(translator.translate('今日は本を読みます。'));  // 오늘은 책을 읽습니다.
translator.close();
```

`npm install -g hdtrans`로 설치하면 `hdtrans` 명령도 함께 생깁니다. 자세한 내용은 [API 안내](docs/typescript-api.md)를 보세요.

## 설정

설정과 사용자 파일은 모두 프로그램 옆 `hdtrans-data/user/` 폴더에 둡니다. 견본 파일(`*.example.*`)은
업데이트 때 바뀌므로 복사해서 이름을 바꾼 뒤 고칩니다.

| 하고 싶은 것 | 방법 |
|---|---|
| 단어의 번역을 정하기 | `userdict.txt`에 일본어와 한국어를 탭으로 나눠 적기 |
| 인물과 지명을 이름으로 읽히기 | `names.txt`에 같은 형식으로 적기 |
| 대사를 반말로 | 설정 파일에 `speech: soft` |
| 대사의 말투 다듬기를 끄기 | 설정 파일에 `speech_levels: keep` |
| 줄마다 따로 번역 | 번역 창에서 **한꺼번에 묶어서 번역** 끄기, 명령줄은 `--each` |
| Ehnd 사전(꿀도르 등) 쓰기 | 설정 파일에서 켜기. 기본은 꺼져 있고, 켜면 품질이 떨어질 수 있음 |

```text
灯台	등대
衣更	이사라
```

파일은 UTF-8로 저장합니다. 번역 창과 연결한 도구는 저장한 사전과 이름을 다음 번역부터 바로 씁니다.
설정 파일은 `hdtrans.config.example.yaml`을 같은 폴더에 `hdtrans.config.yaml`로 복사해서 만듭니다.

더 자세한 내용은 아래 문서에 있습니다.

- [사용자 사전과 이름 목록](docs/dictionary-files.md#사용자-사전): 품사, 이름 종류, 작품마다 사전 나누기
- [말투와 번역 방식](docs/translation-modes.md): `strict`와 `soft`, 대사 말투, 한 줄씩 번역과 문서 번역의 차이
- [설정 파일 항목](docs/api.md#설정-파일)과 [Ehnd 설정](docs/ehnd-compat.md)

## 잘 안 될 때

- **도구에서 DLL을 못 불러온다:** 도구에 적은 경로에 `J2KEngine.dll`이 있는지 확인합니다.
- **사전을 찾지 못한다:** DLL 옆에 `hdtrans-data/dicts.hdsnap`이 있어야 합니다. DLL만 옮겼다면 폴더도 함께 옮깁니다.
- **예전 엔진으로 번역된다:** 도구의 엔진 경로를 다시 확인합니다. XUnity는 `J2KEngineH.dll`을 먼저 읽으니 남아 있으면 지웁니다.
- **사용자 사전이 안 먹는다:** 파일 이름이 `userdict`로 시작하는지, UTF-8로 저장했는지, 사이에 탭을 썼는지 봅니다.

## 오역 신고

[이슈](https://github.com/sokcuri/hdTrans/issues/new/choose)에 원문 한두 줄, 지금 나온 번역, 기대하는 번역을 적어 주세요.
버전, 쓰는 도구, 한 줄씩 번역했는지 여러 줄을 한꺼번에 번역했는지도 알려 주시면 원인을 찾기 쉽습니다.

## 개발

- [문서 목록](docs/README.md): 빌드, 엔진 API, 번역 파이프라인
- [오역 고치기](docs/fixing-translations.md): 사전과 규칙을 고치는 방법
- [기여 안내](docs/contributing.md): PR은 `develop` 브랜치로 보냅니다.
- [변경 내역](CHANGELOG.md)

## 라이선스

[MPL-2.0](LICENSE). 외부 라이브러리의 라이선스는 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)에 있습니다.

Copyright (c) Sokcuri (소쿠릿, https://github.com/sokcuri)
