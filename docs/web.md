# 웹 데모

https://trans.kuri.kr 의 웹 데모는 `web/`에 있습니다. 번역은 모두 브라우저 안에서 하며, 서버는 정적
파일(페이지, WebAssembly 엔진과 기본 사전 전체를 담은 `dicts.hdsnap`)만 내려 줍니다.

## 구성

| 파일 | 내용 |
|---|---|
| `web/index.html`, `web/style.css` | 페이지 |
| `web/src/app.ts` | 엔진과 사전을 받아(Cache Storage에 보관) 워커에 넘기고, 입력이 바뀌면 번역을 요청 |
| `web/src/documents.ts` | 문서 번역 탭의 파일 읽기(인코딩 판별), 출력 방식, 결과 이름, ZIP 만들기 |
| `web/src/worker.ts` | 워커. WASM과 사전 파일을 준비하고 `HdTrans` 인스턴스로 번역 |
| `web/src/translator.ts` | 공용 [TypeScript API](typescript-api.md). 문자열 번역, 사용자 사전과 Ehnd 설정, JSON 선택 기록 |
| `web/src/dictionaries.ts` | 사전 목록(IndexedDB), 데스크톱 파일 가져오기와 내보내기, 적용할 사전 합치기 |
| `web/src/grid.ts` | 사용자 사전, 이름 표 편집기 (탭 구분 표, 붙여 넣기, 파일 열기, 저장) |
| `web/src/names.ts` | 이름 표를 이름 목록으로 (종류가 없으면 `full`), 내보낼 때는 사용자 사전 줄로 (종류는 `name`, `surname`, `n`으로, `hon`은 뺌) 바꿈 |
| `web/examples/`, `web/examples.tsv` | 예제 텍스트와 메뉴 이름 |
| `web/build.mjs` | 네이티브 CLI로 `dicts.hdsnap`을 컴파일하고 사이트를 `build/web/dist`에 만듦 |
| `web/check.mjs` | 헤드리스 Chrome으로 사이트를 확인 |
| `web/deploy.sh` | 서버에 올림, 되돌림 |

- **사전** 탭은 사용자 사전 목록입니다. 사전마다 사용자 사전 표와 이름 표(Ehnd 사전은 표 하나)가 있고, 체크한 사전만 씁니다.
  목록 순서가 우선순위입니다(위가 먼저, 끌어 놓기나 ↑↓로 바꿈). 체크한 hdTrans 사전의 사용자 사전 표를 순서대로 합쳐
  `setUserDictRaw()`로, Ehnd 사전은 합쳐 `UserDict.txt` 하나로 `setEhnd()`에 넣습니다. 한 텍스트 안에서는 같은 키의 앞 줄이
  먼저입니다. 체크한 hdTrans 사전의 이름 표는 합쳐 [이름 목록](dictionary-files.md#문서-이름-목록)으로 `setNames()`에 넣으며,
  같은 이름은 위 사전의 줄이 이깁니다.
- 이름 표의 열은 일본어, 한국어, 종류(`given`, `family`, `full`, `nick`, `place`, `org`, `title`, 비우면 `full`), 옵션(`hon`)입니다.
  이전 판의 품사는 종류로 읽습니다(`name` → `full`, `surname` → `family`, `n` → `org`). `hon` 밖의 옵션은 쓰지 않습니다. 텍스트 번역과 문서 번역이 같은 사전을 쓰고, 두 탭의 설정에 적용 중인 사전 이름과 그 사전을 사전 탭에서 여는 **사전 편집**이 있습니다.
  잘못된 줄은 사전 이름과 행으로 알리고, 잘못된 판이면 이전에 적용한 사전을 유지합니다.
- 사전은 IndexedDB(`hdtrans` 데이터베이스)에 둡니다. 처음 열 때 이전 판이 localStorage에 둔 사용자 사전과 이름 표를
  **기본 사용자 사전**으로 옮깁니다(localStorage 값은 지우지 않음).
- 가져오기와 내보내기는 데스크톱 파일 형식입니다. hdTrans 사전은 `userdict-이름.txt`(이름 표는 `name`, `surname`, `n` 줄로 붙임)로 `user/`에,
  Ehnd 사전은 `UserDict_이름.txt`로 Ehnd 폴더에 둡니다. 가져올 때는 셋째 열(Ehnd는 숫자, hdTrans는 품사)로, 정하지 못하면 파일 이름
  (`UserDict…`)으로 형식을 정합니다. 위 **내보내기**는 체크한 사전을 우선순위대로 합친 `userdict.txt`(Ehnd 사전이 있으면
  `UserDict.txt`와 함께 ZIP)입니다.
- 문서 방식은 입력 전체를 한 세션으로(`hdtrans`와 같음), 줄 단위는 줄마다 새 세션으로(`hdtrans -each`와
  같음) 번역합니다. 번역 전에 `reset()`으로 세션을 비웁니다.
- **텍스트 번역**과 **문서 번역** 탭이 있고 대사 말투와 출력(번역문만, 원문 병기, 원문 병기 + 빈 줄)은 두 탭이 함께 쓰며
  브라우저에 기억합니다. 텍스트 번역의 결과 칸은 출력 형식으로 보이고(원문 줄은 흐리게), **복사**는 문서 번역이 저장하는
  형식(`formatDocument`) 그대로 복사합니다. 문서 번역은 고르거나 끌어 놓은 텍스트 파일을
  브라우저에서 읽어 파일마다 문서 방식으로 번역합니다. 인코딩 판별, 출력 방식, 원문을 따르는 줄바꿈, `이름_번역.txt`(UTF-8 BOM) 이름은 Windows 번역 창과
  같습니다. 사전 탭에서 체크한 사전을 쓰고, 말투나 사전이 바뀌면 이미 만든 결과를 버립니다. **중단**은 지금 파일을 마친 뒤 멈추고
  그 결과를 버립니다. **ZIP으로 받기**는 끝난 파일을 한 ZIP(UTF-8 이름, 줄일 수 있으면 deflate)으로 묶습니다.
- 텍스트 번역은 예제 줄과 언어 줄(일본어 → 한국어) 아래 두 칸을 PC에서는 나란히, 좁은 화면에서는 원문을 위로 쌓습니다. 원문 칸에는
  글자 수와 줄 수, 지우기(×)가, 번역 칸에는 복사할 글(고른 출력 형식)의 글자 수와 줄 수, 걸린 시간, 진행 막대,
  복사가 있습니다. 칸 머리는 테두리 상자의 **원문**, **번역문**으로 시작하고 아래 글과 같은 왼쪽 선에 맞춥니다. 글자 수는 두 칸 모두 코드 포인트 수입니다. 두 칸은 위의 줄들 아래 창 높이를 채우고(좁은 화면에서는
  원문 45%, 번역 55%) 글은 칸 안에서 스크롤됩니다. 칸 머리의 펼치기(↕)는 그 칸만 남겨 설정, 예제, 언어 줄 아래 창을 채우고
  페이지는 움직이지 않으며, 언어 줄은 그 칸의 언어만 보입니다. 화상 키보드가 올라오면 보이는 높이에 맞춥니다. 숨은 칸도 계속 번역되고 복사됩니다. ↕나 Esc로
  두 칸으로 돌아가고, 펼친 칸을 기억합니다. 예제를 고르거나 ×로 지우면 두 칸을 첫 줄부터 보이게 합니다. 설정(방식, 대사 말투, 출력, 사전)은 고른 값을 요약한 **설정** 버튼이 여는 층에
  있습니다. PC에서는 버튼 아래 팝오버, 좁은 화면에서는 아래쪽 시트이고, 바깥을 누르거나 Esc나 ×로 닫으며, 연 동안 초점은 층
  안에서만 돕니다. 고른 값은 바로 적용됩니다. 좁은 화면의 문서 번역은 파일을 카드로 보이고 버튼을 아래에
  둡니다. 색은 시스템의 밝은, 어두운 모드를 따릅니다. 사전 오류는 상태 줄에 보입니다.
- 보안 컨텍스트가 아닌 곳(LAN 주소의 http)에서도 동작합니다. `crypto.subtle`이 없으면 받은 파일의 SHA-256을 JavaScript로 확인하고,
  Cache API가 없으면 매번 받으며, `navigator.clipboard`가 없거나 `writeText`가 거부되면 숨은 입력 칸으로 복사합니다.
- 제목 다음 줄에 엔진 버전과 빌드한 커밋의 짧은 해시, 날짜를 적습니다.

## 빌드

Node 22 이상과 Emscripten, 그리고 같은 소스로 빌드한 네이티브 CLI가 필요합니다. CLI가
`hdtrans -dicts dicts -compile-snapshot build/web/dicts.hdsnap`으로 기본 사전 전체를 컴파일하고, build.mjs는 그 파일을
WebAssembly 엔진이 불러오는지 확인한 뒤 올립니다. 엔진 데이터(`src/core/`)가 다른 빌드끼리는 맞지 않습니다.

```sh
cmake --preset mac && cmake --build --preset mac --parallel   # 또는 linux
emcmake cmake --preset wasm
cmake --build --preset wasm --parallel
npm --prefix web ci
node web/build.mjs
```

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_DICTS` | 올릴 사전 폴더 (기본 `dicts`) |
| `HDTRANS_WASM` | 엔진 빌드 폴더 (기본 `build/wasm`) |
| `HDTRANS_CLI` | 사전을 컴파일할 CLI (기본 `build/mac/hdtrans`, 없으면 `build/linux/hdtrans`) |
| `HDTRANS_WEB_EXAMPLES` | 저장소에 없는 예제 텍스트 폴더. `examples.tsv`에 적힌 파일 중 `web/examples/`에 없는 것을 여기서 찾음. 예제가 하나라도 없으면 빌드 실패 |

`build/web/dist`에는 `index.html`, `js/`, `examples/`, `manifest.json`과 내용 해시 폴더
`assets/<sha256 앞 16자>/`(`hdtrans_engine.js`, `hdtrans_engine.wasm`, `dicts.hdsnap`)가 생기고, 텍스트, wasm, `.hdsnap`
파일마다 `.br`, `.gz` 압축본이 붙습니다. 서버는 브라우저가 받는 인코딩의 압축본을 보내고, `assets/`는 오래 캐시하게 합니다.

## 확인

`npm --prefix web run compile` 뒤 `npm --prefix web test`가 문서 번역의 인코딩 판별, 출력 방식, ZIP과 사전 파일 형식 판별,
가져오기와 내보내기, 사전 합치기를 확인합니다(CI의 wasm 작업).

```sh
python3 -m http.server -d build/web/dist 8000 &   # 압축본을 쓰지 않는 정적 서버도 됨
head -20 INPUT > in.txt
build/mac/hdtrans -dicts dicts -userdict '' -in in.txt > want.txt
node web/check.mjs http://127.0.0.1:8000/ INPUT want.txt 20
```

`check.mjs`는 두 번 방문해 준비 시간, 받은 바이트, 엔진 메모리를 적고, 입력 앞 20줄의 번역이 CLI 출력과
같은지, 긴 입력의 진행 막대, 출력 형식마다 결과 칸과 복사한 글, 사용자 사전, 이름 표 편집과 붙여 넣기가 번역을 바꾸는지, 제목 아래 버전을
확인하고, 번역 API가 없는지(`POST /api/translate`가 404) 봅니다(`http.server`는 501로 답해 이 항목만
실패합니다). 이어서 localStorage의 사용자 사전과 이름 표가 기본 사용자 사전으로 옮겨지는지, PC(1200×1300)와 휴대폰(390×844)
화면에서 사전 탭의 배치(목록과 표가 나란히, 또는 위아래로, 휴대폰에서 표만 가로로 밀리는지, 누를 곳이 44px 이상인지), 사전과 항목
추가, 체크 해제, 순서에 따른 우선순위, 오류 줄 위치, 데스크톱 파일 내보내기와 가져오기의 바이트 일치, 이름 변경, 삭제, 다시 열었을 때
목록이 남는지 봅니다. 끝으로 텍스트 번역 탭이 PC(1366×800)에서 나란히, 휴대폰에서 첫 화면 안에 위아래로 놓이는지,
설정 층(열기, 초점 가두기, 바로 적용, Esc와 바깥과 ×로 닫기), 360px 폭에서 페이지가 가로로 밀리지 않고 누를 곳이 40px 이상인지,
×가 입력을 지우는지, 예제 고르기, 긴 입력에서 두 칸과 펼친 칸이 창을 채우고 페이지가 스크롤되지 않는지(휴대폰에서 창이
줄어든 경우 포함), ↕와 Esc로 돌아오는지, 예제와 ×가 스크롤을 처음으로 돌리는지 봅니다. 보안 컨텍스트가 아닌 경우는 정적 서버를 LAN이나 Tailscale 주소에 묶고 그 `http://` 주소로 한 번 더 돌려 확인합니다. `CHROME`으로 Chrome 경로를, `SHOT`으로 화면 저장 경로를 줍니다(사전 탭은 `-dictionaries`, `-mobile`을
붙인 이름).

## 배포

```sh
HDTRANS_WEB_HOST=user@host HDTRANS_WEB_SSH='ssh -i KEY' web/deploy.sh
```

올리기 전에 서버의 사이트 폴더를 `<폴더>.bak-<UTC 시각>`으로 복사하고, 올린 뒤 최신 백업만
`HDTRANS_WEB_KEEP`개(기본 10) 남기고 되돌리는 명령을 출력합니다.

```sh
HDTRANS_WEB_HOST=user@host HDTRANS_WEB_SSH='ssh -i KEY' web/deploy.sh rollback <폴더>.bak-<UTC 시각>
```

`web/deploy.sh dry-run`은 내용이 바뀔 파일만 보여 주고, `web/deploy.sh examples 폴더`는 `web/examples/`에 없는
예제 텍스트를 서버의 사이트에서 그 폴더로 받습니다(`HDTRANS_WEB_EXAMPLES`로 씀). 저장소에 없는 예제는 서버의
`examples/`에만 있으므로 배포는 서버의 `examples/*.txt`와 그 압축본을 지우지 않습니다. `HDTRANS_WEB_DIR`로 서버의 사이트
폴더를 바꿉니다(기본 `/srv/hdtrans/web`). 접속 정보와 키는 저장소에 두지 않습니다. 올린 뒤
`node web/check.mjs https://trans.kuri.kr/ ...`로 확인합니다.

### 자동 배포

`.github/workflows/deploy-web.yml`은 `Publish npm`이 태그 push에서 성공하고 그 태그가 최신 안정 릴리스이면
그 릴리스의 `hdTrans-<버전>-linux-x64.tar.gz`(CLI)와 npm 패키지 `hdTrans-<버전>-wasm.tgz`(0.4.5 전에는
`hdtrans-<버전>.tgz`)를 받아 같은 릴리스의 `SHA256SUMS.txt`로 확인하고, 그 버전이 npm에 있으면 패키지를 npm의
`dist.integrity`와도 비교합니다. 다시 빌드하지 않습니다. CLI(`--version`)와 패키지의 엔진이 릴리스 버전을 보고하는지
확인하고, 태그 커밋의 소스와 예제 텍스트(서버에서 받음)로 사이트를 만든 뒤(CLI는 사전
스냅샷만 컴파일) `web/deploy.sh`로 올립니다. 이어서 공개 페이지가 방금 만든 `index.html`과 같은지(제목 아래 버전의 엔진
버전과 커밋), `manifest.json`, 예제 목록을 확인하고, 공개된 엔진 파일을 릴리스 엔진과 바이트 단위로 비교합니다. 서버에도 없는 예제가 있으면 실패하고, 받은
예제 텍스트는 아티팩트로 올리지 않습니다.

Actions에서 develop 브랜치로 직접 실행할 때는 게시된 릴리스 태그와 `dry_run`(기본 켬)을 고릅니다. `dry_run`은
사이트를 만든 뒤 바뀔 파일 수만 보고합니다. 최신이 아닌 태그를 고르면 사이트가 그 릴리스로 바뀝니다.

비밀값은 GitHub 환경 `web`(develop 브랜치만 허용)에 둡니다.

| 이름 | 내용 |
|---|---|
| `HDTRANS_WEB_HOST` | 배포 계정의 ssh 대상, `hdtrans-deploy@<호스트>` |
| `HDTRANS_WEB_DEPLOY_KEY` | 배포 계정의 개인 키 |
| `HDTRANS_WEB_KNOWN_HOSTS` | 서버의 호스트 키(`known_hosts` 형식) |

서버의 배포 계정 `hdtrans-deploy`는 사이트 폴더와 백업 폴더의 주인이고, 그 키는 `authorized_keys`의 강제
명령 `web/deploy-command.sh`로 `web/deploy.sh`의 deploy, dry-run, examples가 보내는 명령만 실행합니다(받은
심볼릭 링크는 무효로 바꾸고, 예제 읽기는 심볼릭 링크를 거부). 두 파일은 함께 고치고, 바꾼
`deploy-command.sh`는 서버에 다시 설치합니다. 손으로 올릴 때도 이 계정을 써야 파일 주인이 바뀌지 않습니다.

저장소에 없는 예제를 새로 넣을 때는 먼저 그 `.txt`를 서버의 사이트 `examples/`에 `hdtrans-deploy` 소유, 644로
올린 뒤 `web/examples.tsv`에 적습니다. 순서가 바뀌면 다음 배포가 예제를 찾지 못해 실패합니다.

되돌릴 때는 관리자 키로 `web/deploy.sh rollback <폴더>.bak-<UTC 시각>`을 실행합니다. 배포 로그의 `rollback:`
줄이 그 명령이며, 배포 계정으로는 되돌릴 수 없습니다.

## 메모리

WebAssembly의 선형 메모리는 해제해도 크기가 줄지 않으며, 빈 공간을 이후 호출에서 재사용합니다.
페이지는 TSV를 직접 컴파일하지 않고 `dicts.hdsnap`을 읽어 초기화 중 메모리 사용을 줄입니다.
사용자 사전과 Ehnd 필터는 같은 인스턴스에서 교체하며, 번역 결과의 임시 메모리는 자동으로 정리합니다.
