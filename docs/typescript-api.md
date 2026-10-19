# TypeScript API

WASM 엔진을 TypeScript와 JavaScript에서 쓰는 인터페이스입니다. 문자열과 객체를 주고받으며,
`ccall`, 포인터, `FreeMem`을 직접 다루지 않습니다. 번역 결과의 임시 메모리는 자동으로 정리합니다.

## 설치

Node.js 22 이상에서 사용합니다. 패키지에는 빌드한 WASM 엔진, 기본 사전(`dicts.hdsnap`), JavaScript와 `.d.ts`가 들어갑니다.
사용자가 C++이나 TypeScript를 직접 빌드할 필요는 없습니다.

npm에서 설치합니다.

```sh
npm install hdtrans
```

`.tgz` 파일을 직접 설치할 수도 있습니다.

```sh
npm install ./hdtrans-<version>.tgz
```

## 초기화와 종료

```ts
import { HdTrans } from "hdtrans";

const hdTrans = await HdTrans.init();
console.log(hdTrans.version);           // 엔진 빌드 버전
console.log(hdTrans.dictionaryVersion); // 기본 사전 버전
console.log(hdTrans.runtime);           // "wasm"
try {
  console.log(hdTrans.translate("今日はいい天気ですね。"));
} finally {
  hdTrans.close();
}
```

초기화는 비동기입니다. 별도 사전 경로를 주지 않으면 패키지에 포함된 기본 사전을 씁니다.
`version`은 실제 엔진의 빌드 버전이며, 임의로 지정하는 값이 아닙니다.

Ehnd 파일 묶음은 초기화할 때 `HdTrans.init({ ehnd: files })`로 넘기거나,
초기화 뒤 `setEhnd(files)`로 적용할 수 있습니다. 생략하면 Ehnd는 꺼져 있습니다.

인스턴스마다 세션, 사용자 사전, Ehnd 설정이 분리됩니다.
`close()`는 인스턴스 사용을 마칠 때 호출합니다. 번역 결과마다 호출할 필요는 없습니다.
WASM의 선형 메모리 크기는 해제 후에도 줄지 않을 수 있지만, 해제한 공간은 다음 호출에서 재사용합니다.

## 명령줄

`bin`으로 등록된 `hdtrans` 명령을 제공합니다. 전역 설치하거나 `npx`로 실행합니다.

```sh
npm install -g hdtrans
hdtrans "今日はいい天気ですね。"

npx hdtrans "今日はいい天気ですね。"
```

명령줄에서도 패키지에 포함된 엔진과 기본 사전을 씁니다.

```sh
hdtrans --version
hdtrans --json "競走で勝った。"
hdtrans --in input.txt --out output.txt
hdtrans --json --file input.txt --out result.json
hdtrans --why --userdict userdict.txt "蒼月は学生です。"
```

| 옵션 | 동작 |
|---|---|
| `--in FILE`, `--file FILE` | UTF-8 파일 입력. 생략하면 문장 인자나 표준 입력 사용 |
| `--out FILE` | UTF-8 파일 출력. 생략하면 표준 출력 |
| `--json` | API의 `{ text, choices, logs }`를 JSON으로 출력 |
| `--why` | 단어 선택과 Ehnd 실행 로그를 표준 오류로 출력 |
| `--userdict FILE` | 사용자 사전 파일. npm CLI는 한 파일을 받으며 중복 지정은 오류 |
| `--ehnd DIR` | Ehnd 폴더의 파일 묶음을 명시적으로 적용 |
| `--version` | 사전을 실제로 로드하고 엔진, 실행 방식, 사전 버전을 출력 |
| `--help` | 프로그램과 작성자 정보, 사용법 출력 |

`--version`의 형식은 양쪽 CLI에서 같습니다.

```text
hdTrans v0.1.0 (native) / dict 0.1.0+abc123
hdTrans v0.1.0 (wasm) / dict 0.1.0+abc123
```

네이티브 CLI는 `native`, npm CLI는 `wasm`입니다. 버전 정보가 없는 직접 편집한 사전은 `development (unversioned)`로 표시합니다.
`--file`은 `--in`의 별칭입니다. `--json --out`은 JSON만 파일에 저장하고, 로그와 오류는 표준 오류로 보냅니다.
`--in`과 직접 문장을 함께 지정할 수 없습니다. `--` 뒤의 인자는 옵션으로 해석하지 않습니다.

네이티브 CLI의 사전 컴파일 옵션 등은 npm판에 없으며, 네이티브 `--userdict`는 여러 파일과 폴더를 지원합니다.
현재 npm판은 기본 사전이 동봉된 Node.js용 패키지입니다. 웹 데모는 공용 WASM 인터페이스를 직접 연결합니다.

## 번역과 JSON

```ts
const text = hdTrans.translate("競走で勝った。");
// string

const result = hdTrans.translate("競走で勝った。", { json: true });
console.log(result.text);
console.log(result.choices);
```

`json: true`는 JSON 문자열이 아니라 타입이 있는 객체를 반환합니다.
직렬화할 때는 `JSON.stringify(result)`를 씁니다. 생략하거나 `false`이면 문자열을 반환합니다.

`choices`는 단어와 어미의 후보를 고른 기록입니다. 원문 표면형, 선택한 번역과 후보 번호,
처리 단계, 규칙 파일과 번호를 확인할 수 있습니다. 자연어 설명을 생성하는 기능은 아닙니다.
일반 번역에서는 이 기록을 수집하지 않습니다.

| 필드 | 의미 |
|---|---|
| `text` | 최종 번역문 |
| `choices[].surface` | 처리 중인 단어의 표면형 |
| `choices[].line`, `chunk`, `sentence`, `token` | 호출 안의 파이프라인 위치. 0부터 시작 |
| `choices[].start` | 정규화된 토큰 안의 UTF-8 바이트 위치 |
| `choices[].choice` | 본문 후보 선택: `index`, `text`, `provenance`, `layer`, `id`, `source` |
| `choices[].ending` | 어미 선택과 표면형. 없으면 `null` |

선택의 `text`는 활용 표지 등을 조립하기 전 엔진 내부 번역입니다. 최종 문자열의 일부와 일치하지 않을 수 있습니다.
`source`는 규칙 파일의 행이나 의미 표의 행이며, 기록이 없으면 빈 문자열입니다.
위치는 전처리와 정규화 뒤의 값이므로 원문 문자열의 인덱스로 바로 쓰면 안 됩니다.

한 인스턴스는 앞선 번역의 이름과 문맥을 기억합니다. 관계없는 문서를 시작할 때는 `reset()`을 부릅니다.
일반 번역과 JSON 번역을 비교할 때도 각각 `reset()`한 뒤 같은 입력을 넣어야 합니다.

## 사용자 사전

```ts
hdTrans.setUserDict([
  { ja: "魔導具", ko: "마도구" },
  { ja: "蒼月", ko: "아오츠키", pos: "surname" },
]);

const entries = hdTrans.getUserDict();
entries.push({ ja: "ユキ", ko: "유키", pos: "name" });
hdTrans.setUserDict(entries);
```

- `setUserDict(entries)`는 사용자 사전 전체를 교체합니다. 기본 사전은 바꾸지 않습니다.
- `getUserDict()`는 등록된 사용자 사전 항목의 복사본을 반환합니다. 반환한 배열이나 항목을 수정해도 적용 전에는 사전이 바뀌지 않습니다.
- `clearUserDict()`는 사용자 사전만 제거합니다.
- 변경은 같은 인스턴스의 다음 번역부터 반영되며 문맥 기억은 초기화됩니다.

TSV 문자열을 그대로 넣을 수도 있습니다.

```ts
hdTrans.setUserDictRaw("魔導具\t마도구\n蒼月\t아오츠키\tsurname\n");
const raw = hdTrans.getUserDictRaw();
hdTrans.clearUserDict();
```

항목은 `{ ja: string, ko: string, pos?: UserDictPos, options?: string }`입니다.
`options`에는 `"prio=2 force"`처럼 기존 TSV 옵션을 적습니다.
형식은 기존 [사용자 사전](dictionary-files.md#사용자-사전)과 같고, 품사와 옵션도 같은 파서를 씁니다.

`getUserDict()`는 유효한 원본 항목을 반환합니다. 기본 사전 전체, 동사 활용으로 생성된 항목,
중복 항목을 병합한 최종 사전이 아닙니다. 중복 항목의 순서도 보존됩니다.

잘못된 TSV로 교체를 시도하면 `userDictError`에 진단을 남기고 이전에 적용한 사용자 사전을 유지합니다.
이때 `getUserDict()`는 여전히 적용 중인 항목을 반환하고, `getUserDictRaw()`는 수정 작업을 위해 마지막으로 제출한 원문을 반환합니다.
두 조회 결과가 다를 수 있으므로 원문 편집기에서는 `userDictError`도 함께 확인합니다.

## Ehnd 필터를 실행 중에 바꾸기

Ehnd는 기본으로 꺼져 있습니다. 파일 이름과 내용을 넘겨 명시적으로 적용합니다.
파일 형식과 적용 순서는 [ehnd-compat.md](ehnd-compat.md)에 있습니다.

아래는 번역 결과 전체를 정해진 문자열로 바꾸는 후처리 필터입니다.
필터 교체가 반영되는지 확인하기 위한 예이며 실제 번역용 규칙은 아닙니다.

```ts
hdTrans.setEhnd({
  "PreFilter_A.txt": "ねこ\t猫\t100\t0\n",
  "PostFilter_A.txt": "^.+$\t필터 A\t100\t1\n",
});
console.log(hdTrans.translate("今日はいい天気ですね。")); // 필터 A

hdTrans.setEhnd({
  "PostFilter_B.txt": "^.+$\t필터 B\t100\t1\n",
});
// PreFilter_A.txt와 PostFilter_A.txt는 이제 적용되지 않습니다.
console.log(hdTrans.translate("今日はいい天気ですね。")); // 필터 B

hdTrans.clearEhnd();
console.log(hdTrans.translate("今日はいい天気ですね。")); // 필터 없는 번역
```

### 교체와 제거 범위

`setEhnd()`는 추가가 아니라 **전달한 파일 묶음 전체로 교체**합니다.
기존 파일 중 새 호출에서 생략한 파일은 남지 않습니다. 위 예에서 두 번째 호출 뒤에는 `PostFilter_B.txt`만 적용됩니다.

| 대상 | `setEhnd(newFiles)` 뒤 |
|---|---|
| 이전 호출의 `PreFilter*.txt`, `PostFilter*.txt` | 제거하고 새 묶음의 파일로 교체 |
| 이전 호출의 `SkipLayer*.txt`, `UserDict*.txt` | 마찬가지로 교체. 새 묶음에 없으면 제거 |
| `setUserDict()`나 `setUserDictRaw()`로 넣은 일반 사용자 사전 | 유지 |
| 기본 사전과 기본 사전 폴더의 필터 | 유지 |
| 세션이 기억한 이름과 문맥 | 초기화 |
| 다른 `HdTrans` 인스턴스 | 영향 없음 |

`clearEhnd()`는 `setEhnd({})`와 같습니다. 외부 Ehnd 파일 묶음만 제거하며, 기본 사전 폴더의 필터까지 끄는 것은 아닙니다.
반대로 `clearUserDict()`는 일반 사용자 사전만 제거하고 Ehnd 묶음의 `UserDict*.txt`는 남깁니다.
`getUserDict()`도 일반 사용자 사전만 조회합니다.

설정 메서드는 동기적으로 적용합니다. 반환된 뒤의 다음 번역부터 새 설정을 쓰며, 새 인스턴스를 만들거나 WASM을 다시 불러올 필요는 없습니다.
파일이나 전달한 JS 객체를 나중에 수정하는 것만으로는 자동 반영되지 않습니다. 수정한 내용으로 `setEhnd()`를 다시 호출합니다.
외부 필터를 적용하면 번역 품질이 떨어질 수 있습니다.

### 파일과 행의 오류

파일 이름은 `PreFilter*.txt`, `PostFilter*.txt`, `SkipLayer*.txt`, `UserDict*.txt`만 받으며 대소문자는 구별하지 않습니다.
경로 구분자가 든 이름이나 문자열이 아닌 내용은 예외를 발생시킵니다. 이 검사는 교체 전에 하므로 기존 설정은 유지됩니다.

파일 안의 잘못된 정규식 등은 네이티브 파서의 규칙대로 건너뛰고 `filterErrors`로 알립니다.
이 경우 이전 묶음으로 자동 복구하는 것은 아닙니다. 새 묶음에서 유효한 규칙을 적용합니다.

```ts
hdTrans.setEhnd(files);
for (const error of hdTrans.filterErrors) console.error(error);
```

## 필터 실행 로그와 WHY

```ts
const result = hdTrans.translate("今日はいい天気ですね。", { json: true });
console.log(result.text);
console.log(result.choices); // 단어와 어미를 고른 기록
console.log(result.logs);    // 처리 단계와 실제 적용한 필터 로그
```

`filterErrors`는 파일을 읽을 때의 진단이고, `logs`는 이번 번역의 실행 기록입니다.
Ehnd 형식의 규칙 로그에는 전처리와 후처리 구분, 파일 이름, 0부터 시작하는 원본 행 번호,
검색식, 치환식, 차수, 정규식 여부를 담습니다. 처리 단계의 요청, 전처리 결과, 번역 결과, 후처리 결과도 기록합니다.
규칙마다 치환 전후 전문을 따로 저장하는 방식은 아닙니다.

CLI의 `-why`도 같은 필터 실행 로그를 표준 오류에 씁니다. 일반 번역에서는 수집하지 않습니다.

## 테스트

저장소 최상위에서 실행합니다. WASM 빌드에는 [Emscripten](build.md#webassembly), TypeScript 빌드에는 Node.js 22 이상이 필요합니다.

```sh
npm --prefix web ci
npm --prefix web run compile
npm --prefix web run test:types
emcmake cmake --preset wasm
cmake --build --preset wasm --parallel
HDTRANS_WASM_WRAPPER=build/web/translator.js node --experimental-default-type=module tools/wasm_smoke.mjs \
  build/wasm/hdtrans_engine.js tests/unit/dict/testdata/folder/base.tsv
```

PowerShell의 마지막 명령은 다음과 같습니다.

```powershell
$env:HDTRANS_WASM_WRAPPER = "build/web/translator.js"
node --experimental-default-type=module tools/wasm_smoke.mjs build/wasm/hdtrans_engine.js tests/unit/dict/testdata/folder/base.tsv
```
컴파일한 `translator.js`와 `translator.d.ts`는 `build/web/`에 생깁니다. CI의 WASM 아티팩트에도 둘을 포함합니다.

`tools/wasm_smoke.mjs`가 실제 WASM 엔진과 TypeScript 인터페이스를 함께 검사합니다.
사용자 사전의 등록, 교체, 조회와 삭제, JSON 선택 기록, 인스턴스 격리, 필터의 실행 중 교체와 제거를 확인합니다.
테스트는 직접 만든 예문과 필터만 사용하며 외부 Ehnd 사전을 기본 데이터에 합치지 않습니다.
