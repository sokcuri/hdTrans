# 도구와 함께 쓰기

## 다운로드와 설치

1. [Releases](https://github.com/sokcuri/hdTrans/releases)에서 `hdTrans-<버전>-windows.zip`을 받습니다. `Source code`는 필요 없습니다.
2. `C:\hdTrans`처럼 영문 경로에 압축을 풉니다. 별도 설치 프로그램은 없습니다.
3. 연결할 도구를 끄고 아래 도구별 설정을 적용합니다.

연결에 필요한 파일은 다음과 같습니다.

```text
C:\hdTrans\
  J2KEngine.dll
  hdTrans\
    dicts.hdsnap
    dicts.txt
    user\
      userdict.example.txt
      hdtrans.config.example.yaml
```

- `dicts.hdsnap`은 기본 사전, `dicts.txt`는 사전 버전입니다. `dicts/` 폴더나 원본 TSV는 배포하지 않으며 따로 받을 필요도 없습니다.
- DLL을 다른 폴더로 옮기면 `hdTrans` 폴더도 함께 옮깁니다.

## LunaTranslator

1. 이지트랜스 번역 설정에서 DLL 경로를 지정합니다.
2. `C:\hdTrans\J2KEngine.dll` 파일을 선택합니다.
3. 이지트랜스 번역을 켜고 일본어를 번역합니다.

## 아네모네, Anemone

1. 아네모네를 끕니다.
2. 실행 파일 옆 `anemone.ini`의 `[CONFIG]`에 아래 값을 넣습니다. 같은 항목이 있으면 값을 바꿉니다.

   ```ini
   [CONFIG]
   EZTRANS_PATH=C:\hdTrans
   ```

3. 아네모네를 다시 실행합니다.

사용자 사전은 `C:\hdTrans\hdTrans\user\userdict.txt`에 작성합니다. 아네모네의 구형 `AneDic.txt` 대신 이 파일을 씁니다.
형식은 [사용자 사전](dictionary-files.md#사용자-사전)을 보세요.

## Textractor에서 아네모네로 보내기

1. 위 방법으로 아네모네를 설정합니다.
2. Textractor에서 **Copy to Clipboard** 확장을 켭니다.
3. 아네모네에서 클립보드 번역을 켭니다.
4. Textractor에서 게임에 연결하고 번역할 텍스트 스레드를 선택합니다.

Textractor는 텍스트를 클립보드로 보내고 아네모네가 번역합니다. Textractor에 DLL 경로를 지정하지 않습니다.

## XUnity.AutoTranslator의 ezTransXP

1. 게임을 끄고 XUnity.AutoTranslator의 `Config.ini`에서 아래 값을 설정합니다. 기존 섹션에 항목이 있으면 값을 바꿉니다.

   ```ini
   [Service]
   Endpoint=ezTransXP

   [General]
   Language=ko
   FromLanguage=ja

   [ezTrans]
   InstallationPath=C:\hdTrans
   ```

2. 지정한 폴더에 `J2KEngineH.dll`이 있으면 제거합니다. 이 파일이 `J2KEngine.dll`보다 먼저 선택됩니다.
3. 게임을 다시 실행합니다. `InstallationPath`를 지정하면 레지스트리 등록은 필요 없습니다.

장시간 사용하면 중개 프로그램의 결과 버퍼 미해제로 메모리 사용량이 늘 수 있습니다.

## 아랄트랜스, AralTrans

먼저 레지스트리를 설정합니다.

1. 아랄트랜스를 끕니다.
2. `Win+R`을 누르고 `regedit`을 실행합니다.
3. `HKEY_CURRENT_USER\Software\ChangShin\ezTrans`로 이동합니다.
4. `FilePath` 값을 `C:\hdTrans`로 바꿉니다. 키나 값이 없으면 키와 문자열 값 `FilePath`를 만듭니다.
5. 아랄트랜스를 실행하고 **EzTransXP** 번역 플러그인을 선택합니다.

게임 후킹과 ATCode는 아랄트랜스에서 별도로 설정합니다.

## 에그헤드, EggHead

먼저 레지스트리를 설정합니다.

1. 에그헤드를 끕니다.
2. `Win+R`을 누르고 `regedit`을 실행합니다.
3. `HKEY_CURRENT_USER\Software\ChangShin\ezTrans`로 이동합니다.
4. `FilePath` 값을 `C:\hdTrans`로 바꿉니다. 키나 값이 없으면 키와 문자열 값 `FilePath`를 만듭니다.
5. 에그헤드 실행 파일 옆에 기존 `J2KEngine.dll`이 있으면 제거합니다.
6. 에그헤드를 다시 실행합니다.

에그헤드 0.73new와 0.73rev.E_BETA2 기준입니다. 레지스트리 경로보다 실행 파일 옆 DLL을 먼저 읽습니다.
같은 레지스트리를 쓰는 다른 도구의 엔진 경로도 바뀝니다.

## 투컨트롤, TwoControl

공식 2024.08.30 배포판 기준입니다.

1. 투컨트롤을 끕니다.
2. 하드트랜스 배포본의 `J2KEngine.dll`을 두 개 복사하고, 각각 `Ehnd.dll`과 `Ehnd.dlx`로 이름을 바꿔 투컨트롤의 `Ehnd` 폴더에 덮어씁니다.
3. 하드트랜스 배포본의 `hdTrans` 폴더를 투컨트롤의 `Ehnd` 폴더 안에 복사합니다.
4. 나머지 투컨트롤 동봉 파일은 그대로 둡니다.
5. 투컨트롤을 다시 실행하고 이지트랜스 번역을 사용합니다.

```text
TwoControl\
  TwoControl.exe
  Ehnd\
    Ehnd.dll
    Ehnd.dlx
    hdTrans\
      dicts.hdsnap
      dicts.txt
      user\
```

`Ehnd.dll`과 `Ehnd.dlx`는 둘 다 하드트랜스 DLL의 복사본입니다.

## VNR, Visual Novel Reader

1. `Preferences > Locations > Translators > ChangShin Soft - ezTrans XP > Browse`에서 `C:\hdTrans` 폴더를 선택합니다.
2. `Preferences > Translation > Translators`의 **Enable built-in Ehnd translation script**를 끕니다.
3. **VNR을 재시작**합니다.

다른 엔진이 계속 사용되면 VNR 폴더나 PATH에 남아 있는 기존 `J2KEngine.dll`, `J2KEngine.dlx`를 제거합니다.

## HTTP 서버, nanikit/eztrans-server

terminal 버전과 .NET 6 런타임을 설치한 뒤 서버 폴더에서 실행합니다.

```powershell
.\EztransServer.exe --dll-path "C:\hdTrans\J2KEngine.dll"
```

기본 주소는 `http://localhost:8000/`입니다. DLL 경로를 직접 지정하려면 terminal 버전을 씁니다.

## 그 밖의 도구

Translator++와 MTool은 하드트랜스 연결 방법이 아직 정리되지 않았습니다.

## 다른 사용 방법

- [CLI 안내](api.md#명령줄-hdtrans): 명령줄 설치, 파일 번역과 옵션
- [JavaScript와 TypeScript API](typescript-api.md): npm 라이브러리 사용법
- 설치 없이 번역: https://trans.kuri.kr

직접 프로그램에 연결할 때의 호출 규약과 제한은 [도구 호환성 개발 참고](tool-compatibility.md)를 보세요.
