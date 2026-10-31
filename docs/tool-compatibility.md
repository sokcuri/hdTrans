# 도구 호환성 개발 참고

설치와 설정은 [도구와 함께 쓰기](using-with-tools.md)를 보세요.
이 문서는 도구의 로더, 호출 규약, 인코딩과 호스트 테스트 범위를 다룹니다.

## 배포 파일과 사전 탐색

Windows ZIP에는 x86 `J2KEngine.dll`, x64 `hdTransEngine.dll`, x86 `hdTransEngine_x86.dll`이 들어 있습니다.
네이티브 라이브러리 이름은 macOS에서 `libhdTransEngine.dylib`, Linux에서 `libhdTransEngine.so`입니다.

```text
C:\hdTrans\
  J2KEngine.dll
  hdTransEngine.dll
  hdTransEngine_x86.dll
  hdtrans-data\
    dicts.hdsnap
    dicts.txt
    user\
      userdict.example.txt
      hdtrans.config.example.yaml
    ehnd\
      README.txt
      PreFilter_example.txt
      PostFilter_example.txt
      UserDict_example.txt
```

배포판은 컴파일한 `dicts.hdsnap`과 버전 파일 `dicts.txt`를 담으며 `dicts/`나 원본 TSV를 담지 않습니다.
Linux와 macOS도 같은 `hdtrans-data/`를 실행 파일 옆에 둡니다.
구성은 [패키징 스크립트](../tools/package-release.py)에 있습니다.

J2K DLL은 초기화 인자의 `Dat`, 현재 폴더, 레지스트리와 무관하게 DLL 옆 `hdtrans-data/`를 데이터 루트로 씁니다.
기본 사전은 데이터 루트의 `dicts.hdsnap`입니다.
TwoControl 2024.08.30은 별도로 원본 사전 파일을 검사합니다. 다른 아래 로더는 `Dat` 폴더나 이지트랜스 실행 파일의 존재를 필수 검사하지 않습니다.
도구의 ANSI 변환 문제를 피하려면 영문 경로를 씁니다.

사용자 사전 파일 이름은 대소문자를 구별하지 않습니다. 소문자 이름의 바이트 순서로 읽고 같은 항목은 뒤 파일이 우선합니다.
설정의 `userdict: [first.tsv, second.txt, folder]`로 기본 탐색을 대신할 수도 있습니다.

원본 Ehnd DLL을 hdTrans 위에 겹쳐 넣지 않습니다. 원본 Ehnd는 `j2kengine.dlx`의 내부 코드를
패치하므로 export 호환만으로 그 조합을 보장할 수 없습니다([소스][ehnd]).
hdTrans 자체가 Unicode API를 제공하며 Ehnd 데이터는 DLL 옆 `Ehnd/`에서 읽습니다.
지원 범위는 [Ehnd 호환 안내](ehnd-compat.md)를 보세요.

## 설치 경로와 레지스트리

레지스트리를 편집하기 전에 기존 `FilePath` 값을 기록합니다.
표는 공개 소스와 배포 파일의 로더를 구분해 적었습니다. 미확인 도구의 키를 다른 도구에서 유추하지 않습니다.

| 도구 | 확인된 레지스트리 키와 값 | 경로 선택 우선순위 |
|---|---|---|
| [LunaTranslator][luna-python] | 사용하지 않음 | 설정에서 선택한 DLL 파일의 부모 폴더 |
| [XUnity ezTransXP][xunity-endpoint] | `HKCU\SOFTWARE\ChangShin\ezTrans` / `FilePath` | `[ezTrans] InstallationPath`, 없으면 레지스트리 기본값. 폴더의 H DLL을 일반 DLL보다 우선 |
| [아네모네][anemone-path] | `HKCU\Software\ChangShin\ezTrans` / `FilePath` | `anemone.ini`의 `[CONFIG] EZTRANS_PATH`에 DLL이 있으면 사용, 아니면 레지스트리 |
| Textractor + 아네모네 | 아네모네와 같음 | Textractor는 DLL 경로를 찾지 않음 |
| [아랄트랜스 0.2/0.3][aral02] | `HKCU\Software\ChangShin\ezTrans` / `FilePath` | 레지스트리를 못 읽으면 폴더 선택 창. 명시 WOW64 플래그 없음 |
| [VNR][vnr-registry] | `HKCU\Software\ChangShin\ezTrans` / `FilePath`, 명시 WOW64 플래그 없음 | [위치 설정][vnr-location]은 유효한 `ezTransLocation` → 유효한 레지스트리 경로. 실제 DLL 로드는 기존 검색 경로 뒤에 이 폴더들을 PATH로 추가하므로 설정 우선이 보장되지는 않음 |
| [nanikit 서버][server-interop] | `HKCU\Software\ChangShin\ezTrans` / `FilePath`, 명시 `Registry32` | 명시 DLL 경로 → 레지스트리 → 기본 설치 경로 → 서버 폴더와 상위 폴더 |
| [HelloKS 서버][eztransweb] | 사용하지 않음 | 소스에 하드코딩한 H DLL 전체 경로 |
| [에그헤드 0.73new, 0.73rev.E_BETA2][egghead-release] | `HKCU\Software\ChangShin\ezTrans` / `FilePath`, `KEY_QUERY_VALUE`, 명시 WOW64 플래그 없음 | 레지스트리 조회에 성공해야 로더 진행. 실행 파일 옆 `J2KEngine.dll` → 레지스트리 경로의 `J2KEngine.dll` |
| [TwoControl 2024.08.30][twocontrol-release] | `HKCU\Software\ChangShin\ezTrans` / `FilePath` | 자체 `Ehnd/Dat` 사전이 없으면 레지스트리와 폴더 선택을 통해 원본 사전을 복사. 실제 DLL은 자체 `Ehnd/Ehnd.dll` 또는 `Ehnd/Ehnd.dlx` |
| Translator++, MTool | 직접 확인 못 함 | 연결 방법 미정 |

`HKCU\Software`의 이 키는 [32비트와 64비트가 공유][registry-view]합니다. `Wow6432Node`에 복제하거나 HKLM 키로 바꾸지 않습니다.
`regedit` 설정은 [사용 안내](using-with-tools.md#아랄트랜스-araltrans)를 보세요. 도구의 탐색 순서와 hdTrans DLL 자체의 사전 탐색은 별개입니다.

## 도구별 연결

아래의 **CI 검증은 도구 소스의 DLL 호출을 별도 호스트로 재현한 것**입니다. 실제 프로그램을 실행한 수동 검증은 아닙니다.
경로 선택 UI, IPC, 후킹, 클립보드, 도구 자체 전후처리는 검증에 포함하지 않습니다.

### LunaTranslator

- 이지트랜스 번역 설정에서 위 배포 구성의 `J2KEngine.dll` 파일을 선택합니다. 설치 폴더 선택이 아닙니다.
- [Python 경로 처리][luna-python]는 선택 파일의 부모를 32비트 `LunaSubProcess32.exe`에 넘깁니다. 메인 앱은 64비트여도 됩니다.
- [네이티브 호출][luna-native]: DLL 로드 → 필수 export 19개 검사 → `InitializeEx("CSUSER123455", "<폴더>\\Dat")`
  → `TranslateMMNTW(0, text)` → 복사 → `FreeMem(pointer)`. `Dat`는 시스템 ANSI, 번역은 UTF-16입니다.
  W export 존재를 Ehnd 지원으로 판단하며, 별도 Ehnd DLL을 찾지는 않습니다. 요청은 직렬 처리하고 Terminate와 unload는 하지 않습니다.
- **CI:** `host.luna`, `host.luna-ansi`. hdTrans에서는 W 경로를 선택합니다. ANSI 테스트는 W가 없다는 조건을 강제로 선택하여
  CP932 입력, CP949 출력의 fallback도 확인합니다. 실프로그램에서는 초기화 오류 표시와 연속 번역을 확인해야 합니다.

### XUnity.AutoTranslator의 ezTransXP

- 위 배포 구성으로 준비한 뒤 `Config.ini`에 `[Service] Endpoint=ezTransXP`, `[ezTrans] InstallationPath=C:\hdTrans`를 설정합니다.
  언어는 `[General] Language=ko`, `FromLanguage=ja`입니다. 기존 `J2KEngineH.dll`이 있으면 제거합니다.
  이 도구는 **H 파일을 우선** 읽습니다.
- [설정 소스][xunity-endpoint]: InstallationPath 기본값은 `HKCU\SOFTWARE\ChangShin\ezTrans`의 `FilePath`입니다.
  경로를 명시하면 레지스트리를 만들 필요가 없습니다. 별도 `FullNET\ezTransXP.ExtProtocol.exe`가 엔진을 부릅니다.
- [호출 소스][xunity-native]: Stop, TranslateW, InitializeEx 주소 조회 → 같은 키와 `Dat`로 초기화
  → `TranslateMMNTW(0, UTF-16)` → `PtrToStringAuto`. 직렬 처리하며 종료는 인자 없는 `StopTranslation()` → FreeLibrary, Terminate는 없습니다.
- **CI:** `host.xunity`는 원래의 **Cdecl C# delegate**를 사용합니다. DLL의 stdcall과 선언이 달라 원본 net35 helper의
  스택 복구 동작을 .NET Framework 4의 `NetFx40_PInvokeStackResilience` 설정으로 재현합니다. 최신 .NET이나 Mono까지 검증한 것은 아닙니다.
- **제한:** 원본 호출자는 `FreeMem`을 전혀 부르지 않아 **매 번역마다 결과 버퍼가 누수**됩니다. 원본 Ehnd도 매번 새 버퍼를
  할당합니다([소스][ehnd]). CI 통과를 무누수 보장으로 해석하면 안 됩니다. 실제 배포 helper의 비트수, CLR, 게임 연결은 수동 확인이 필요합니다.

### 아네모네, Anemone

- 위 배포 구성으로 준비한 뒤 아네모네를 끄고, 실행 파일 옆 `anemone.ini`를 편집합니다.

  ```ini
  [CONFIG]
  EZTRANS_PATH=C:\hdTrans
  ```

- 값은 DLL 파일이 아닌 폴더입니다. [경로 탐색][anemone-path]은 이 설정의 `J2KEngine.dll`을 열어 보고,
  실패하면 `HKCU\Software\ChangShin\ezTrans`의 `FilePath`를 읽습니다.
- [엔진 로더][anemone-native]와 [번역 호출][anemone-text]은 Luna와 같은 19개 필수 export, 같은 키와 `Dat`,
  W 우선 및 CP932/CP949 fallback, 결과 복사 후 `FreeMem`입니다. 번역과 reload는 공통 잠금으로 직렬화하며 Terminate와 unload는 하지 않습니다.
  Win32 전용입니다. `Dat` 경로는 문자열을 바이트로 단순 축소하므로 비ASCII 경로를 보장하지 않습니다.
- **사용자 사전:** W export가 있는 엔진에서 아네모네는 `AneDic.txt`를 `Dat\UserDict.jk`에 쓰지 않고, 파일이 바뀔 때
  `J2K_ReloadUserDict`만 부릅니다([소스][anemone-text]). 그래서 `J2KEngine.dll`(Windows)이 아네모네 안에서만 실행 파일 옆
  `AneDic.txt`(UTF-8, 이름의 대소문자 무관)를 읽습니다. 아네모네 창(`AneParentClass`, `AnemoneParentWndClass`)이 같은 프로세스에
  있는지를 첫 번역과 `J2K_ReloadUserDict` 때 확인합니다. 형식과 처리는 [Ehnd UserDict](ehnd-compat.md)와 같고(`0`, `2`는 상용어구),
  `user/`의 `userdict*` 항목이 우선합니다. 잘못된 줄은 건너뛰고 디버거 출력에 남습니다. 다른 도구, C API, CLI, 번역 창은 읽지 않습니다.
- **CI:** `host.anemone`, `host.anemone-ansi`, `host.anemone-dict`(창이 있을 때 `anedic.txt` 적용과 다시 읽기),
  `host.anemone-nowindow`(창이 없으면 적용하지 않음). 클립보드 감시, 창 출력과 실제 사전 편집은 수동 확인이 필요합니다.

### Textractor + 아네모네

- Textractor의 **Copy to Clipboard** 확장을 켜고, 위와 같이 설정한 아네모네에서 클립보드 번역을 켭니다.
- [공식 확장 목록][textractor-extensions]에는 ezTrans 로더가 없습니다. [클립보드 확장][textractor-clipboard]이
  UTF-16 `CF_UNICODETEXT`를 넘기고 **아네모네가 DLL을 호출**하는 구성입니다. Textractor와 아네모네의 비트수는 같지 않아도 됩니다.
- **CI:** DLL 부분은 `host.anemone`과 같습니다. Textractor 자체의 직접 로드 검증이 아닙니다.
  게임 후킹, 선택한 텍스트 스레드, 클립보드 전달과 중복 번역은 실제 Windows에서 확인해야 합니다.

### 아랄트랜스, AralTrans의 EzTransXP 플러그인

- 위 배포 구성으로 준비한 뒤 EzTransXP 번역 플러그인을 선택합니다. [0.2 소스][aral02]는
  `HKCU\Software\ChangShin\ezTrans`의 `FilePath`에서 엔진 폴더를 읽습니다. 값을 얻지 못하면 폴더 선택 창에서 `C:\hdTrans`를 지정할 수 있습니다.
  한글 경로에는 경고를 띄우므로 영문 경로를 씁니다. ATCode는 엔진 로더가 아니라 별도 후킹 플러그인입니다.
- `regedit`에서 `FilePath`를 바꾸는 순서는 [사용 안내](using-with-tools.md#아랄트랜스-araltrans)에 있습니다.

- 로드 경로는 `<폴더>\Dat\..\J2KEngine.dll`입니다. InitializeEx, TranslateMMNT, FreeMem, StopTranslation, Terminate 순으로
  5개 export를 모두 확인한 뒤 `InitializeEx("CSUSER123455", "<폴더>\\Dat")`를 부릅니다. 경로는 시스템 ANSI입니다.
  [ATCode 경계][aral-encoding]의 CP932 입력을 `TranslateMMNT(0, text)`에 넘기고 CP949 출력을 처리한 뒤 `FreeMem(pointer)`으로 해제합니다.
  종료는 `StopTranslation(0)`이며 Terminate와 FreeLibrary는 호출하지 않습니다. W export를 탐지하거나 별도 Ehnd를 읽지는 않습니다.
- **CI:** `host.aral02`는 로드부터 Stop까지 같은 작업 스레드에서 재현합니다. [0.3 소스][aral03]에 맞춘 `host.aral03`은
  호출 측에서 로드와 export 조회, 작업 스레드에서 초기화와 번역, 다시 호출 측에서 Stop을 수행합니다.
  없는 `Dat`를 거치는 로드 경로도 검사합니다. 레지스트리 UI, 실제 게임 주입, 필터와 ATCode 적용은 수동 검증 대상입니다.

### VNR, Visual Novel Reader

- 위 배포 구성으로 준비한 뒤 `Preferences > Locations > Translators > ChangShin Soft - ezTrans XP > Browse`에서 폴더를 선택합니다.
  [선택창][vnr-prefs]은 `ezTrans.exe` 폴더라고 표시하지만 실제로는 `J2KEngine.dll` 존재만 검사합니다. 설정 키는 `ezTransLocation`입니다.
- `Preferences > Translation > Translators`의 **Enable built-in Ehnd translation script**를 끄고 **VNR을 재시작**합니다.
  [기본값이 True인 `EhndEnabled` 설정][vnr-ehnd]을 그대로 두면 별도 `ehnd.dll`을 호출하므로 이번 검증 대상과 다릅니다.
- [바인딩][vnr-native]은 실제 번역용 DLL을 `J2KEngine.dll` → `.dlx` 순서로 찾고, 초기화할 `Dat`의 기준 위치용 DLL은
  `.dlx` → `.dll` 순서로 찾습니다. 모두 bare filename 로드입니다. 설정과 레지스트리 폴더를 기존 PATH 뒤에 추가하므로,
  VNR 폴더나 앞선 PATH의 다른 엔진이 먼저 선택될 수 있습니다. 같은 폴더의 기존 `.dlx`도 별도로 확인해야 합니다.
- 호출은 `InitializeEx("CSUSER123455", "<로드된 원본 DLL 폴더>\\Dat")`, 성공값은 정확히 1입니다.
  내장 Ehnd를 끈 상태에서 W export가 있으면 `TranslateMMNTW(0, UTF-16)` → 복사 → `FreeMem(originalPointer)`입니다.
  A fallback은 Python `sjis`, 출력 `uhc`(CP949)이며 변환 오류를 무시합니다. `sjis`는 CP932 확장 전체와 같지 않습니다.
- **CI:** x86 전용 `host.vnr`, `host.vnr-ansi`. 원본처럼 WinDLL 기본 반환형인 32비트 `c_int` 포인터를 유지하고,
  main 초기화 → main의 `あ` warmup → worker 번역 → 명시적 Loader.destroy의 Terminate를 재현합니다.
  ANSI 경로는 W 미지원 조건을 강제로 선택합니다. Stop은 호출하지 않으며 정상 destroy에서는 unload도 하지 않습니다.
  VNR 전체는 종료 시 프로세스를 강제 종료할 수 있으므로 Terminate가 항상 불린다는 뜻은 아닙니다.
- 실제 Python 2 ctypes, Qt UI와 스레드풀, 기존 DLL 검색 충돌은 수동 확인 대상입니다. VNR의 [`QSettings` 저장][vnr-settings]은
  NativeFormat/UserScope의 `sakuradite.org`, `reader`, 값 `ezTransLocation`이며, 설치 위치 조회용 ChangShin 키와 구분합니다.

### HTTP 중개: nanikit/eztrans-server

- 위 배포 구성으로 준비한 뒤 terminal 버전에서 `EztransServer.exe --dll-path "C:\hdTrans\J2KEngine.dll"`로 지정합니다.
  x86 .NET 6 런타임이 필요합니다. GUI 버전은 명시 경로 설정 없이 자동 탐색합니다.
- [서버][server]가 참조하는 Nanikit.Ehnd 2.4.0의 [로더][server-interop]는 명시 경로 → 32비트 HKCU `FilePath`
  → 기본 이지트랜스 폴더 → 서버 실행 폴더와 상위 폴더 순으로 탐색합니다.
- [호출][server-translate]: 기본 Winapi 규약으로 InitializeEx 조회와 초기화 → FreeMem, TranslateW 조회
  → `TranslateMMNTW(0, UTF-16)` → 복사 → `FreeMem`. 키와 `Dat` 인자는 위와 같습니다. 서버 큐와 semaphore로 직렬 처리하고 Terminate와 unload는 하지 않습니다.
- **CI:** `host.eztransserver`. 2.4.0을 표시한 소스의 네이티브 호출을 재현합니다. NuGet 바이너리 동일성, HTTP 요청과 응답,
  GUI 자동 탐색은 수동 검증 대상입니다. 기본 서버 주소는 `http://localhost:8000/`입니다.

### HTTP 중개: HelloKS/ezTransWeb

- 위 배포 구성의 파일을 소스에 지정된 `C:\Program Files (x86)\ChangShinSoft\ezTrans XP`에 두고,
  hdTrans의 `J2KEngine.dll`을 `J2KEngineH.dll` 이름으로도 복사해 기존 파일을 교체합니다.
  다른 폴더라면 [eztrans.py][eztransweb]의 DLL과 `Dat` 경로를 함께 수정합니다. x86 Python과 Flask를 사용합니다.
- 소스는 WinDLL로 로드 → InitializeEx, TranslateW 조회 → 같은 키와 하드코딩한 ANSI `Dat`로 초기화
  → `TranslateMMNTW(0, UTF-16)`입니다. `c_wchar_p`가 결과를 복사하며 **FreeMem, Terminate, unload는 없습니다**.
  Flask의 `threaded=True` 요청들이 외부 잠금 없이 엔진을 부릅니다.
- **CI:** `host.eztransweb`는 hdTrans의 H 이름 복사본을 로드하고, 두 스레드의 동시 호출과 원본처럼 해제하지 않는 순서를 재현합니다.
  XUnity와 마찬가지로 **결과 버퍼 누수는 남습니다**. Python ctypes, Flask HTTP와 자체 전후처리는 수동 검증 대상입니다.

### 에그헤드, EggHead

- [공개 배포 글][egghead-release]의 `egghead073_new.rar`, `egghead073_rev.E_BETA2.rar`를 기준으로 합니다. 두 실행 파일은 x86 PE입니다. 아래 내용은 실행 결과가 아닌 배포 파일의 정적 분석입니다.
- `RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ChangShin\\ezTrans", 0, KEY_QUERY_VALUE, ...)`와 `RegQueryValueExA(..., "FilePath", ...)`를 호출합니다. 접근 플래그는 `1`이며 별도 WOW64 플래그는 없습니다. 레지스트리 조회가 실패하면 DLL 로더로 진행하지 않습니다.
- 실행 파일 옆 `J2KEngine.dll`을 먼저 로드하고, 실패하면 `FilePath`에서 얻은 경로의 `Dat\\..\\J2KEngine.dll`을 로드합니다. `J2KEngineH.dll`을 우선하는 분기는 없습니다.
- 필수 export는 `J2K_InitializeEx`, `J2K_TranslateMMNT`, `J2K_FreeMem`, `J2K_StopTranslation`, `J2K_Terminate`, `J2K_ReloadUserDict`입니다. 이 두 배포본의 로더는 W 함수를 조회하지 않습니다.
- 원본 0.73new의 레지스트리 함수는 RVA `0x1e100`, DLL 로더는 RVA `0x1dfa0`입니다. 원본 실행 파일 SHA-256은 `97a0a5b31e6a6bf60db3c36ef15211fe9da32ac61015df5574a080d6c83aa366`입니다.

### 투컨트롤, TwoControl

- [공식 GitHub][twocontrol-repo]는 배포 저장소이며 엔진 로더 소스는 공개되어 있지 않습니다. 아래 내용은 [2024.08.30 릴리스][twocontrol-release]의 `TwoControl.rar`에 한정한 정적 분석입니다. 다른 버전의 동작으로 일반화하지 않습니다.
- `TwoControl.exe`와 `dll/CustomData/ezTransXP.dll`은 x86 PE입니다. 엔진은 자체 `Ehnd/Ehnd.dll`을 로드하며, `[Option] EhndUnload`가 켜져 있거나 NTLEA 모듈이 로드된 조건에서는 `Ehnd/Ehnd.dlx`를 선택합니다. 이 로더의 선택 대상은 `J2KEngineH.dll`이 아닙니다.
- `Ehnd/Dat/JisJK.Inf`, `Ehnd/Dat/JisJK.da`의 존재와 내용을 검사합니다. 사전이 없으면 `HKCU\Software\ChangShin\ezTrans`의 `FilePath` 또는 폴더 선택 창에서 원본 사전을 가져옵니다. 공개 RAR에는 이 파일들과 `Ehnd.dll`, `Ehnd.dlx`가 포함되어 있습니다.
- 플러그인은 `J2K_InitializeEx`, `J2K_TranslateMMNT`, `J2K_FreeMem`, `J2K_StopTranslation`, `J2K_Terminate`, `J2K_ReloadUserDict`를 조회합니다. hdTrans는 이 export와 호출 규약을 제공합니다. 이 버전의 플러그인은 `J2K_TranslateMMNTW`를 조회하지 않으므로 hdTrans의 W 함수 지원과 별개로 클라이언트의 번역 경로는 ANSI입니다.
- 번역 호출은 `char* __stdcall J2K_TranslateMMNT(int, const char*)` 형태로 두 인자를 전달하고 원래 결과 포인터를 `J2K_FreeMem`에 넘깁니다. export 조회는 플러그인 RVA `0x17ce8a`, 번역 호출은 `0x17dfd7`, 결과 해제는 `0x17e3cb`에서 시작합니다.
- `Ehnd/Ehnd.dll`과 `Ehnd/Ehnd.dlx`를 모두 x86 hdTrans DLL의 복사본으로 교체합니다. 원본 Ehnd 래퍼 자체를 교체하므로 원본 엔진에 대한 내부 코드 패치는 거치지 않습니다. 나머지 동봉 파일은 유지해 로더의 사전 검사를 통과시킵니다.
- hdTrans는 파일명과 무관하게 로드된 DLL 옆에서 데이터를 찾으므로 사전과 사용자 파일은 `Ehnd/hdtrans-data/`에 둡니다. 두 파일을 교체하면 `EhndUnload`와 NTLEA 조건의 어느 분기에서도 hdTrans를 읽습니다. [설치 순서](using-with-tools.md#투컨트롤-twocontrol)
- 이 교체 방식의 근거는 정적 로더와 호출 계약입니다. 실제 TwoControl GUI는 실행하지 않았고 이 배포본에 대응하는 hdTrans 호스트 테스트는 없습니다.
- 공개 RAR SHA-256은 `7faf4f6c7698da1008e0fe67b9e293e55fbc97117b9e54018301f0cb683dc232`입니다.

### 연결 방법이 정리되지 않은 도구

| 도구 | 자료와 한계 |
|---|---|
| [Translator++ transEz][translatorez] | ezTransXP 지원 공지는 있으나 플러그인 구현과 hdTrans 연결 방법은 정리되지 않았습니다. |
| [MTool][mtool] | 공개 ezTrans 연동 구현과 hdTrans 연결 방법은 확인되지 않았습니다. |

이 도구들의 레지스트리 키나 설치 경로를 다른 도구에서 유추하지 않습니다.

## C API와 Python ctypes

직접 연결할 때는 `J2KEngine.dll`이 아닌 C API DLL을 씁니다. Windows Python이 64비트면
`hdTransEngine.dll`, 32비트면 `hdTransEngine_x86.dll`을 고릅니다.
macOS는 `libhdTransEngine.dylib`, Linux는 `libhdTransEngine.so`이며 문자열은 모두 UTF-8, 호출 규약은 Cdecl입니다.

아래를 `translate.py`로 저장하고 `python translate.py C:\hdTrans\hdTransEngine.dll C:\hdTrans\hdtrans-data\dicts.hdsnap`로 실행합니다.
둘째 인자는 배포판의 사전 스냅샷 경로입니다. DLL과 Python의 비트수를 맞춥니다.

```python
import ctypes as C
from pathlib import Path
import sys

api = C.CDLL(str(Path(sys.argv[1]).resolve()))
api.hdTrans_Initialize.argtypes = [C.c_char_p]
api.hdTrans_Initialize.restype = C.c_int
api.hdTrans_Translate.argtypes = [C.c_char_p, C.c_size_t,
                                 C.POINTER(C.c_void_p), C.POINTER(C.c_size_t)]
api.hdTrans_Translate.restype = C.c_int
api.hdTrans_FreeMem.argtypes = [C.c_void_p]
api.hdTrans_FreeMem.restype = None
api.hdTrans_Terminate.argtypes = []
api.hdTrans_Terminate.restype = None

dirs = str(Path(sys.argv[2]).resolve()).encode("utf-8") + b"\0\0"
if api.hdTrans_Initialize(dirs) != 0:
    raise RuntimeError("사전을 불러오지 못했습니다")
try:
    text = "こんにちは".encode("utf-8")
    out, size = C.c_void_p(), C.c_size_t()
    if api.hdTrans_Translate(text, len(text), C.byref(out), C.byref(size)) != 0:
        raise RuntimeError("번역 실패")
    try:
        print(C.string_at(out, size.value).decode("utf-8"))
    finally:
        api.hdTrans_FreeMem(out)
finally:
    api.hdTrans_Terminate()
```

반환형을 `c_char_p`로 바꾸면 원래 포인터를 잃으므로 `c_void_p`를 유지합니다. [C API 계약](api.md#c-api-hdtransengine).

## 웹과 WASM

브라우저 앱에는 `hdtrans-wasm` 아티팩트의 `hdtrans_engine.js`, `hdtrans_engine.wasm`과 사전을 함께 배포합니다.
배포판 사전은 `dicts.hdsnap`이며 Windows DLL을 WASM에 넣는 방식은 아닙니다.
직접 빌드는 `emcmake cmake --preset wasm && cmake --build --preset wasm`입니다.
[WASM API](api.md#webassembly), [웹 빌드](web.md), [npm과 TypeScript API](typescript-api.md)를 참고하세요.

## CI가 확인하는 것

구현은 [네이티브 호스트](../tests/host/tool_compat.cpp)와 [C# 호스트](../tests/host/xunity_host.cs)에 있습니다.
모든 모드는 없는 `Dat` 경로, 사전 없는 작업 폴더, 잘못된 `HDTRANS_DICTS`를 사용하고 DLL 옆 사전으로 초기화합니다.
`こんにちは → 안녕하세요`, `日本語 → 일본어`를 반복하여 정확한 인코딩과 결과를 비교하고, 도구의 해제 및 종료 순서를 그대로 따릅니다.
직렬 처리하는 도구는 순차 호출하며, `host.eztransweb`는 두 스레드가 잠금 없이 부릅니다. 기존 `host.j2k`도 동시 번역을 별도로 검사합니다.
x86 CI가 실제 엔진 ABI 검증이며 x64 실행은 같은 인터페이스의 추가 검사일 뿐, x64 이지트랜스 배포 지원을 뜻하지 않습니다.

[luna-native]: https://github.com/HIllya51/LunaTranslator/blob/168272ad5088b4a235fd37e1c822d2cb5047c449/src/NativeImpl/LunaSubprocess/eztrans.cpp
[luna-python]: https://github.com/HIllya51/LunaTranslator/blob/168272ad5088b4a235fd37e1c822d2cb5047c449/src/LunaTranslator/translator/eztrans.py
[xunity-endpoint]: https://github.com/bbepis/XUnity.AutoTranslator/blob/08324a58dcb26b76fc08e0fb2121098164579e73/src/Translators/ezTransXP/ezTransXPEndpoint.cs
[xunity-native]: https://github.com/bbepis/XUnity.AutoTranslator/blob/08324a58dcb26b76fc08e0fb2121098164579e73/src/Translators/ezTransXP.ExtProtocol/ezTransTranslationLibrary.cs
[anemone-native]: https://github.com/sokcuri/anemone/blob/9fa75743af5b433253036676bcd962928f120cbf/Anemone/TransEngine.cpp
[anemone-text]: https://github.com/sokcuri/anemone/blob/9fa75743af5b433253036676bcd962928f120cbf/Anemone/TextProcess.cpp
[anemone-path]: https://github.com/sokcuri/anemone/blob/9fa75743af5b433253036676bcd962928f120cbf/Anemone/Library.cpp
[textractor-extensions]: https://github.com/Artikash/Textractor/blob/df5b830d1757a66aa162f8081e99d42c9bd50488/extensions/CMakeLists.txt
[textractor-clipboard]: https://github.com/Artikash/Textractor/blob/df5b830d1757a66aa162f8081e99d42c9bd50488/extensions/copyclipboard.cpp
[server]: https://github.com/nanikit/eztrans-server/blob/8b398d67fcdba6ab9a97fc0016488c65469e16f4/EztransServer.Core/EztransServer.Core.csproj
[server-interop]: https://github.com/nanikit/Nanikit.Ehnd/blob/7599abd6efe45e834f0dfee268201cefeea3a05a/Nanikit.Ehnd/EhndInterop.cs
[server-translate]: https://github.com/nanikit/Nanikit.Ehnd/blob/7599abd6efe45e834f0dfee268201cefeea3a05a/Nanikit.Ehnd/Ehnd.cs
[eztransweb]: https://github.com/HelloKS/ezTransWeb/blob/7296b9acd71e91eb28079af65dc9af83e73c028c/eztrans.py
[ehnd]: https://github.com/sokcuri/ehnd/tree/a2c6e0fbb309690b440a2e65027076b67e1cd3a2/ehnd
[translatorez]: https://dreamsavior.net/update-info-translator-ver-5-11-15/
[mtool]: https://trstest.mtool.app/release.php?lang=en
[twocontrol-guide]: https://umthink.tistory.com/117
[twocontrol-repo]: https://github.com/twocomet/TwoControl
[twocontrol-release]: https://github.com/twocomet/TwoControl/releases/tag/Releases
[egghead-release]: https://blog.naver.com/foolmaker/30164894153
[aral02]: https://github.com/sokcuri/araltrans02/blob/d5128e3c112e234f2bef532fbd2e51c4e3e73b6d/Translator/ezTransXP/ezTransXP.cpp
[aral03]: https://github.com/sokcuri/araltrans03/blob/6aa944d1829006a59d0f7e4cf2fef83e3f256481/Translator/ezTransXP/ezTransXP.cpp
[aral-encoding]: https://github.com/sokcuri/araltrans02/blob/d5128e3c112e234f2bef532fbd2e51c4e3e73b6d/Algorithm/ATCode/HookPoint.cpp#L1558-L1593
[registry-view]: https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys
[egghead-guide]: https://github.com/forkwikiman/enha/blob/4742b600191923863638e57877136dcd7e799948/mirror/%EC%97%90%EA%B7%B8%ED%97%A4%EB%93%9C.md
[vnr-native]: https://github.com/Artikash/VNR-Core/blob/6ed038bda9dcd35696040bd45d31afa6a30e8978/py/libs/eztrans/j2kengine.py
[vnr-registry]: https://github.com/Artikash/VNR-Core/blob/6ed038bda9dcd35696040bd45d31afa6a30e8978/py/libs/eztrans/eztrans.py#L34-L59
[vnr-location]: https://github.com/Artikash/VNR-Core/blob/6ed038bda9dcd35696040bd45d31afa6a30e8978/py/apps/reader/managers/libman.py#L174-L197
[vnr-prefs]: https://github.com/Artikash/VNR-Core/blob/6ed038bda9dcd35696040bd45d31afa6a30e8978/py/apps/reader/dialogs/_prefs.py#L7453-L7515
[vnr-ehnd]: https://github.com/Artikash/VNR-Core/blob/6ed038bda9dcd35696040bd45d31afa6a30e8978/py/apps/reader/dialogs/_prefs.py#L3780-L3791
[vnr-settings]: https://github.com/Artikash/VNR-Core/blob/6ed038bda9dcd35696040bd45d31afa6a30e8978/py/apps/reader/settings.py
