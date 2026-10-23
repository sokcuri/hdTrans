# 웹 데모

https://trans.kuri.kr 의 웹 데모는 `web/`에 있습니다. 번역은 모두 브라우저 안에서 하며, 서버는 정적
파일(페이지, WebAssembly 엔진과 기본 사전 전체를 담은 `dicts.hdsnap`)만 내려 줍니다.

## 구성

| 파일 | 내용 |
|---|---|
| `web/index.html`, `web/style.css` | 페이지 |
| `web/src/app.ts` | 엔진과 사전을 받아(Cache Storage에 보관) 워커에 넘기고, 입력이 바뀌면 번역을 요청 |
| `web/src/worker.ts` | 워커. WASM과 사전 파일을 준비하고 `HdTrans` 인스턴스로 번역 |
| `web/src/translator.ts` | 공용 [TypeScript API](typescript-api.md). 문자열 번역, 사용자 사전과 Ehnd 설정, JSON 선택 기록 |
| `web/src/grid.ts` | 사용자 사전, 이름 표 편집기 (탭 구분 표, 붙여 넣기, 파일 열기, 저장) |
| `web/src/names.ts` | 이름 표를 사용자 사전 줄로 바꿈 (품사가 없으면 `name`) |
| `web/examples/`, `web/examples.tsv` | 예제 텍스트와 메뉴 이름 |
| `web/build.mjs` | 네이티브 CLI로 `dicts.hdsnap`을 컴파일하고 사이트를 `build/web/dist`에 만듦 |
| `web/check.mjs` | 헤드리스 Chrome으로 사이트를 확인 |
| `web/deploy.sh` | 서버에 올림, 되돌림 |

- 사용자 사전 표와 이름 표를 합쳐 `setUserDictRaw()`로 넣습니다. 잘못된 내용은 오류를 보여 주고 이전에 적용한 사전을 유지합니다.
- 문서 방식은 입력 전체를 한 세션으로(`hdtrans`와 같음), 줄 단위는 줄마다 새 세션으로(`hdtrans -each`와
  같음) 번역합니다. 번역 전에 `reset()`으로 세션을 비웁니다.
- 페이지 아래 버전 줄은 엔진 버전(`release-version.txt`), 사전 버전(엔진 버전과 사전 TSV의 sha256 앞 12자), 빌드한 커밋의 날짜(Asia/Seoul, `yyyy-MM-dd HH:mm:ss`)와 짧은 해시입니다.

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

```sh
python3 -m http.server -d build/web/dist 8000 &   # 압축본을 쓰지 않는 정적 서버도 됨
head -20 INPUT > in.txt
build/mac/hdtrans -dicts dicts -userdict '' -in in.txt > want.txt
node web/check.mjs http://127.0.0.1:8000/ INPUT want.txt 20
```

`check.mjs`는 두 번 방문해 준비 시간, 받은 바이트, 엔진 메모리를 적고, 입력 앞 20줄의 번역이 CLI 출력과
같은지, 긴 입력의 진행 막대, 사용자 사전, 이름 표 편집과 붙여 넣기가 번역을 바꾸는지, 버전 줄을
확인하고, 번역 API가 없는지(`POST /api/translate`가 404) 봅니다(`http.server`는 501로 답해 이 항목만
실패합니다). `CHROME`으로 Chrome 경로를, `SHOT`으로 화면 저장 경로를 줍니다.

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
`hdtrans-<버전>.tgz`)를 받아 같은 릴리스의 `SHA256SUMS.txt`로 확인합니다. 다시 빌드하지 않습니다. 패키지의 엔진이
릴리스 버전을 보고하는지 확인하고, 태그 커밋의 소스와 예제 텍스트(서버에서 받음)로 사이트를 만든 뒤(CLI는 사전
스냅샷만 컴파일) `web/deploy.sh`로 올립니다. 이어서 공개 페이지가 방금 만든 `index.html`과 같은지(버전 줄의 엔진
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
