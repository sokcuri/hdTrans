# hdTrans

**이지트랜스와 호환되는 일한 기계번역 프로그램 hdTrans (하드도란스)**

\- by 소쿠릿 ([홈페이지](https://sokcuri.neko.kr/), [X](https://x.com/sokcuri))

하드도란스, 하드트랜스, 어떻게 읽어도 무관합니다.

이지트랜스용 번역 도구에 `J2KEngine.dll`만 바꿔 넣으면 그대로 동작합니다.
비주얼 노벨, 게임, 웹소설 번역을 목표로 사전과 규칙을 다듬고 있습니다.

- 설치 없이 압축만 풀어서 사용
- LunaTranslator, XUnity.AutoTranslator, 아랄트랜스, VNR, 아네모네 지원
- 인명, 지명, 작품명, 관용구, 속어 사전 기본 포함
- 앞 문장의 이름과 문맥을 다음 문장 번역에 반영

## 웹에서 써 보기

https://trans.kuri.kr

## 내려받기

[Releases](https://github.com/sokcuri/hdTrans/releases)에서 받습니다. Windows는 `hdTrans-0.1.0-windows.zip`처럼
버전과 `windows`가 붙은 파일입니다. 원하는 폴더에 압축을 풀면 됩니다.

macOS, Linux 판에는 명령줄 프로그램만 있습니다([명령줄 사용법](docs/api.md#명령줄-hdtrans)).

## 사용법

### 문장, 파일 번역

`hdTransGUI.exe`를 실행하고 위 칸에 일본어를 넣은 뒤 **번역하기**를 누릅니다.
텍스트 파일은 **문서 번역**에서 파일을 골라 번역합니다.

### 이지트랜스 도구에서 쓰기

1. 도구가 쓰는 이지트랜스 폴더의 `J2KEngine.dll`을 다른 이름으로 백업합니다.
2. 압축에서 나온 `J2KEngine.dll`과 `hdTrans` 폴더를 그 폴더에 복사합니다.
3. 도구를 다시 실행합니다.

아랄트랜스와 VNR은 이지트랜스가 설치되어 있어야 폴더를 찾습니다. 이지트랜스 없이 쓰는 방법과
도구별 설명은 [도구와 함께 쓰기](docs/using-with-tools.md)를 참고하세요.

## 사용자 사전

`hdTrans\user\`에 `userdict.txt`를 만들고, 한 줄에 일본어와 한국어를 탭으로 구분해 적습니다.

```
魔導具	마도구
蒼月	아오츠키	surname
```

- 저장하면 실행 중에도 다음 번역부터 반영됩니다.
- `userdict`로 시작하는 `.txt` 파일을 모두 읽습니다. 게임별로 파일을 나눠도 됩니다.
- 새 버전을 같은 폴더에 덮어써도 `user` 폴더의 파일은 남습니다.

품사와 자세한 형식: [사용자 사전](docs/dictionary-files.md#사용자-사전)

## 사전

배포판의 기본 사전은 `hdTrans\dicts.hdsnap` 파일이고, 버전은 같은 폴더의 `dicts.txt`에 있습니다.
Linux와 macOS에서는 두 파일을 프로그램 옆에 둡니다.
저장소의 `dicts/` 폴더(TSV)를 쓰려면 `dicts.hdsnap`과 `dicts.txt`를 지우고 그 자리에 `dicts` 폴더를 넣습니다([사전](docs/dictionary-files.md#배포판과-git의-사전)).

## 설정

설정 파일은 `hdTrans\user\hdtrans.config.yaml`입니다. 같은 폴더의 `hdtrans.config.example.yaml`을 복사해서 고치면 됩니다.

꿀도르 같은 Ehnd 사전은 기본으로 꺼져 있습니다. 설정에서 켤 수 있지만 번역 품질이 떨어질 수 있습니다.

## 오류, 오역 신고

[이슈](https://github.com/sokcuri/hdTrans/issues/new/choose)

## 기여

풀 리퀘스트는 `develop` 브랜치로 보내 주세요. [커밋과 PR 제목 규칙](docs/contributing.md#커밋과-pr-제목)을 따릅니다.
사전과 규칙을 고치는 방법: [오역 고치기](docs/fixing-translations.md)

## 라이선스

[AGPL-3.0-or-later](LICENSE)

## 서드파티 라이선스

[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)

## 개발자용

빌드, 명령줄 옵션, 다른 프로그램에 넣어 쓰기, 사전과 규칙 고치기: [docs/](docs/README.md)
