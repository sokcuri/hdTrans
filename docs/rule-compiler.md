# 규칙 컴파일러 hdrulec

`src/rulec/`의 `hdrulec`는 단어 선택 규칙([rule-language.md](rule-language.md))을 읽어 진단하고,
읽기 쉬운 문법으로 바꾸고, C++ 코드를 만드는 빌드 호스트 도구입니다. 지역 규칙은 사전 행의 `ctx` 열에 있으므로
`hdtrans --dump-rules FILE`이 엔진이 조립한 프로그램을 쓰고, `hdrulec`는 그 파일을 읽습니다. 규칙은 계속 데이터입니다.
도구가 읽고 쓰고 세고(학습 규칙도) 고칠 수 있어야 하고, 엔진은 같은 데이터를 인터프리터로도 돌릴 수 있습니다.

```sh
hdtrans -dicts dicts --dump-rules build/rules.txt -in tests/empty.in   # 조립한 프로그램 (rules.dump)
hdrulec lint build/rules.txt -dicts dicts [-tsv]   # 진단 (file:line:col, 또는 kind, index로 이을 수 있는 TSV)
hdrulec convert build/rules.txt -o build/rules.hdr # 읽기 쉬운 문법으로 (설명 주석 포함)
hdrulec flat build/rules.hdr -o build/rules.txt    # 되돌리기
hdrulec verify build/rules.txt [-paths N]          # 앞단과 변환을 rules::Load, 인터프리터에 대어 봄
hdrulec dump build/rules.txt [-hash]               # 불러온 프로그램을 실행 순서로 (rules.identity)
hdrulec cpp build/rules.txt -local 0-499 -globals -o src/core/rules/compiled_gen.cpp
```

## 사용법

규칙을 고칠 때 쓰는 명령입니다. 저장소 루트에서 `build/mac/hdrulec`로 돌리고, `build/rules.txt`는 먼저
`build/mac/hdtrans -dicts dicts --dump-rules build/rules.txt -in tests/empty.in`으로 씁니다.

| 언제 | 명령 | 같은 것을 보는 ctest |
|---|---|---|
| 고친 직후 | `hdrulec lint build/rules.txt -dicts dicts 2>&1 \| tail -1` | `rules.lint` |
| 동작을 바꿨을 때 | `hdrulec dump build/rules.txt -hash > src/core/rules/program.hash` | `rules.identity` |
| 늘 | `hdrulec cpp build/rules.txt -local 0-499 -globals -o src/core/rules/compiled_gen.cpp` | `rules.generated` |
| 큰 변환 뒤 | `hdrulec verify build/rules.txt -paths 4096` | `rules.verify` |

해시는 주석을 보지 않습니다. 동작을 바꿀 생각이
없었는데 `rules.identity`가 깨지면 `hdrulec dump build/rules.txt` 출력을 이전 커밋과 비교합니다.

| 진단 | 고치기 |
|---|---|
| `trailing tokens after the rule body` | `&&N`, `\|\|N`의 N이 문장 수보다 작음. 대입도 하나로 셈 |
| `[-Wlone-test]` | 옛 규칙에 많음. 고친 규칙에서 새로 났으면 N을 확인 |
| `is not the code of …` | `cpp` 명령으로 다시 만듦 |
| `rules::Load rejects the program at line N` | 그 줄의 따옴표, 연산자 |

## 제약

- 결정적이고, 32비트 Windows DLL, x64, Linux, macOS, WebAssembly에서 같은 결과.
- 빌드에 Python, Go 없음. CMake 스크립트와 C++ 호스트 도구만. 크로스 빌드(WebAssembly 등)는 호스트 도구를
  돌릴 수 없으므로 생성물을 커밋하고, 네이티브 빌드의 ctest가 다시 만들어 비교함.
- GCC 13 `-Werror`, MSVC `/W4 /WX /utf-8`에서 경고 없음. 라이브러리 코드는 예외, RTTI 없음.
- 옛 인터프리터와 의미가 정확히 같아야 함: 대입(`D=`, `Y=`)은 규칙이 거짓이 되어도 남고, 이웃 단어를 찾는
  `GetObject`는 그 단어의 시험 중 후보 표시를 지우는 부수 효과가 있음.

## 참고한 것

| 출처 | 가져온 것 | 가져오지 않은 것 |
|---|---|---|
| GCC 3.4/4.1(손으로 쓴 재귀 하강 파서로 교체), Clang(Parser/Sema 분리, Expressive Diagnostics), rustc(진단, lint 체계), Roslyn(누락, 건너뛴 토큰, 원문 보존 트리) | 손으로 쓴 재귀 하강 파서, 줄 단위 복구로 한 번에 모든 오류, `file:line:col` + 캐럿 + `[-W이름]`, 경고는 이름 붙은 lint, 주석을 트리에 붙여 변환이 원문을 잃지 않음 | 파서 생성기, 다단계 IR, 증분 재파싱 |
| Maranget 2008, Augustsson 1985, Le Fessant, Maranget 2001 | 규칙=행, (위치, 필드)=열로 보는 검사: 쓸모없는 대안(가림), 모순, 중복 | 규칙 사이의 결정 트리. 규칙은 사전 행의 번호로 이미 골라 부르므로 이득이 없음 |
| Forgy 1982(Rete), Drools(Phreak, executable model) | 빌드할 때 코드로 만드는 방식 | Rete. 바뀌는 작업 메모리에 대한 증분 다대다 매칭이 목적인데, 여기서는 단어마다 작은 창을 한 번 봄 |
| VISL CG-3, Apertium lrx-comp(Tyers 외 2012) | 사람이 쓴 규칙과 학습 규칙이 같은 형식, 규칙 추적, 쓰이지 않는 정의 경고 | 구간 반복 수렴 의미론, FST 합성 |
| re2c, Ragel, tree-sitter | 생성물에 날짜, 경로를 넣지 않고 커밋, 생성기로 다시 만들어 비교 | 문법을 JS로 정의 |
| Csmith(Yang 외 2011), Translation Validation(Pnueli 외 1998, Necula 2000), EMI(Le 외 2014) | 옛 인터프리터를 기준으로 한 차분 검사, 컴파일한 규칙마다 등가 검증 | 형식 증명 |

출처: GCC <https://gcc.gnu.org/gcc-3.4/changes.html>, <https://gcc.gnu.org/gcc-4.1/changes.html>;
Clang <https://clang.llvm.org/diagnostics.html>, <https://clang.llvm.org/docs/InternalsManual.html>;
rustc <https://rustc-dev-guide.rust-lang.org/diagnostics.html>; Roslyn
<https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/work-with-syntax>; Maranget
<https://doi.org/10.1145/1411304.1411311>; Augustsson <https://link.springer.com/chapter/10.1007/3-540-15975-4_48>;
Le Fessant, Maranget <https://dl.acm.org/doi/10.1145/507669.507641>; Forgy
<https://doi.org/10.1016/0004-3702(82)90020-0>; Drools <https://docs.drools.org/latest/drools-docs/drools/rule-engine/index.html>;
CG-3 <https://edu.visl.dk/cg3/single/>; Tyers 외 <https://aclanthology.org/2012.eamt-1.54/>; re2c
<https://re2c.org/manual/manual_c.html>; tree-sitter <https://tree-sitter.github.io/tree-sitter/>; Csmith
<https://users.cs.utah.edu/~regehr/papers/pldi11-preprint.pdf>; Pnueli 외 <https://link.springer.com/chapter/10.1007/BFb0054170>;
Necula <https://doi.org/10.1145/349299.349314>; EMI <https://dl.acm.org/doi/10.1145/2594291.2594334>.

## 읽기 쉬운 문법

지금 형식(`L 3 ||1 &&3 S+1== "あそば" …`)의 문제는 앞에 놓는 묶음 수입니다. 수가 하나 틀려도 다른 뜻으로
해석될 뿐 오류가 나지 않습니다. 예: `L 3093 ||3 G+0== "Hz" &&2 D-1== "," P-2== "3**" P+1== "7**"`의 마지막
`P+1== "7**"`는 문장 하나로 떨어져, 뒤가 형용사이기만 하면 なんか를 `어쩐지`로 고릅니다. 새 문법에서는
문장이 한 줄이고 `and`, `or`, 괄호로 묶으므로 이런 실수가 보입니다.

```
# なんか를 문장 첫머리나 부사 뒤 쉼표 다음 또는 い형용사 앞에서 '어쩐지'로 고른다.
local 3093
    gram has Hz
    ko[-1] == "," and pos[-2] is 3**
    pos[+1] is 7**

local 5
    src[+1] == "で" and pos[+1] is 2** and src[+2] == "買" and pos[+2] is 4**
    (src[+1] == "掛か" or src[+1] == "掛" or src[+1] == "かか") and pos[+1] is 4**

local 7
    src[-1] == "、" and pos[-2] is 4** and end.src[-2] == "と" and end.pos[-2] is 50* and end.ko[-2] := "2면"

global 1 stem pos 4 prio 210
    src == "する" and src[-1] starts "お" and end.pos[-1] is 5:0 and ko := "" and end.ko[-1] := end.ko

local 3
    src[+1] == "あそば" and ko[+1] == "하&AE@" and pos[+1] is 4**
    test "いかがあそばしますか。" -> "어떻게 하십니까."
    test "いかがですか。" silent
```

- 규칙 머리 `local N`, `global 번호 stem|ending pos 품사키 prio PRIO`. 그 아래 들여 쓴 줄 하나가 문장이고,
  위에서부터 처음 참인 문장에서 규칙이 참입니다(옛 형식의 맨 바깥 `||N`). 괄호 안에서는 줄을 넘길 수 있습니다.
- 필드: `src`(S), `ko`(D), `pos`(P), `sem`(A), `gram`(G), `end.src`(U), `end.ko`(Y), `end.pos`(M), `end.sem`(T),
  `end.gram`(W). 위치는 `[+1]`, `[-2]`이고 `+0`은 쓰지 않습니다.
- 표기 비교의 숨은 접두어가 낱말이 됩니다: `"L…"`→`starts`, `"R…"`→`ends`, `"00…"`→`inside`,
  그 밖→`==`(`!=`는 `not starts` 등).
- 코드 비교: `pos is 4**|58*`(P==), `is not`(P!=), `sem has`(A==), `lacks`(!=), `under`(A-= 의미 계열),
  `gram has`, `lacks`, `within`(G-=), `end.sem isa`(T==). 코드는 `|`로 나눈 대안이고 따옴표가 없습니다.
- 대입은 `:=`, 다른 단어의 값은 `end.ko[-1] := end.ko`처럼 같은 필드를 씁니다. `&N`(모두 평가하는 그리고)은
  `all(a, b)`, 빈 목록은 `true`/`false`.
- 옛 문법과 새 문법은 같은 트리로 읽히고, 서로 기계적으로 바뀝니다(`convert`, `flat`). 설명 주석도 옮깁니다.

## 파이프라인

```
파일들 ──ReadRuleDir──> Source(파일별 위치) ──ParseFlat / ParseReadable──> AST(File, Rule, Expr)
   ──Normalize──> 검사(Check) ──> 진단
   ──Lower──> rules::Program(노드 배열, 인터프리터)     ──EmitCpp──> 규칙마다 C++ 함수
```

- **어휘, 구문**: 두 문법 모두 손으로 쓴 재귀 하강 파서입니다(`flat.cpp`, `syntax.cpp`). 옛 형식은 레코드가 한 줄이므로
  줄 단위로 복구하고, 새 문법은 문장 끝(줄 끝)까지 건너뛴 뒤 다시 읽습니다. 한 번 돌리면 모든 오류가 나옵니다.
  코드 대안은 문맥에 따라 다르게 읽습니다(`is` 뒤의 `4**`).
- **AST**: 원자에는 엔진이 보는 리터럴 바이트를 그대로 둡니다. 그래서 `Lower`가 만드는 프로그램은 `rules::Load`가
  같은 텍스트에서 만드는 것과 같습니다(`verify`가 덤프로 확인). `Normalize`는 원소 하나짜리 목록을 풀고 같은 종류의
  목록을 펼칩니다. 호출 순서가 같으므로 의미가 같습니다.
- **진단**: `파일:줄:열: warning: 메시지 [-W이름]`, 원문 줄과 `^~~~` 밑줄, 관련 위치는 `note`. 열은 코드 포인트로
  세고 밑줄은 전각 문자 너비를 맞춥니다. `-tsv`는 `kind index statement file line column severity flag message`로
  내보내므로 규칙 검토 표와 `(kind, index)`로 이을 수 있습니다.
- **검사**(`check.cpp`): 대입이 남는 의미를 고려합니다. 예를 들어 앞 문장 때문에 참이 될 수 없는 문장이라도 실패하기
  전의 대입은 일어나므로 "참이 되지 않음"과 "대입만 남음"을 나눠 알립니다.

| lint | 뜻 |
|---|---|
| `unknown-code` | 어느 사전 행에도, 의미 계열 표에도 없는 코드(엔진이 붙이는 `Ow Hz Td Pi Sa Ka`는 앎). 필드를 잘못 쓴 경우가 많음 |
| `lone-test` | 그리고-묶음 뒤에 코드 검사 하나만 있는 문장. 묶음 수가 하나 모자란 경우 |
| `contradiction` | 같은 단어에서 함께 참일 수 없는 검사(표기끼리, 같은 값의 `==`와 `!=`, `+0`의 품사끼리) |
| `neighbour-contradiction` | 이웃의 품사 검사 두 개가 고른 후보가 없을 때만 함께 참 |
| `shadowed` | 앞 문장(또는 앞 전역 규칙)이 늘 먼저 참이라 참이 될 수 없는 문장, 규칙 |
| `duplicate-statement`, `duplicate-test`, `duplicate-rule` | 같은 문장, 검사, 본문 |
| `key-mismatch`, `never-fires` | `+0` 표기 검사가 그 규칙을 가리키는 사전 행의 키와 맞지 않아 참이 될 수 없음 |
| `unused-rule`, `missing-rule` | 어느 사전 행도 가리키지 않는 규칙, 없는 규칙을 가리키는 사전 행 |
| `always-false`, `unreachable-test` | 엔진에 구현이 없는 `D= @d`, 그 뒤라서 돌지 않는 검사 |
| `unreachable-global` | 범위(0<n<1000, 1000<n<5000) 밖이라 돌지 않는 어간 전역 규칙 |
| `self-compare`, `delta-range`, `empty-pattern`, `literal-space` | 자기와 비교, 3칸 넘는 위치, 빈 패턴, 값 끝의 공백, 채움 |
| `assign-then-test` | 대입 뒤에 검사가 오는 문장(지금 규칙에는 없음) |
| `prio-tie` | PRIO가 같은 전역 규칙 |

## 실행: 인터프리터와 생성 코드

규칙은 세 가지로 돌릴 수 있습니다. (1) 지금의 노드 배열(사실상 바이트코드) 인터프리터, (2) 규칙마다 C++ 함수,
(3) 규칙들 사이 또는 안의 결정 트리. 잰 결과는 다음과 같습니다(macOS arm64, `-O3`, 웹소설 51장을 이어 붙인
10,950줄, 사용자 시간 3회).

| 빌드 | 시간(초) | 실행 파일 |
|---|---|---|
| 인터프리터만(`-DHDTRANS_INTERPRET_RULES=ON`) | 3.92, 3.97, 3.93 | 3.39 MB |
| L 0–499 + 전역 36개 생성 코드 | 3.93, 3.84, 3.85 | 3.66 MB |
| 5,125개 모두 생성 코드 | 3.91, 3.87(첫 실행 5.54) | 5.62 MB |

- 장 단위로 돌린 같은 말뭉치에서 지역 규칙 실행 128만 번, 전역 규칙 449만 번, 원자 1,229만 번이었습니다. 원자마다
  가상 호출(`GetObject`, `Check…`)과 문자열 비교가 비용의 대부분이고, 해석 부담(노드 디스패치)은 측정 오차 안입니다.
- 모든 규칙의 생성 코드는 1.8 MB C++, 오브젝트 2.7 MB, 컴파일 4.5초(clang)였습니다.
- 결정 트리는 규칙 사이에서는 쓸모가 없고(사전 행이 규칙 번호로 고름), 규칙 안에서 공통 검사를 묶으려면
  `GetObject`, `Check…`가 순수하다는 증명이 필요합니다. `GetObject`는 이웃의 후보 표시를 지우고, 이웃 찾기는 `D=`가
  바꾼 번역(`(`, `)`)을 읽으므로 대입이 없는 구간에서만 가능합니다. 얻을 수 있는 시간이 위와 같이 작아 하지 않습니다.

결정: **데이터와 인터프리터가 기준**입니다. 생성 코드는 속도가 아니라 *규칙마다 이름 있는 함수*(`RuleL3`,
`RuleG210`)가 필요해서 둡니다. 디버거에서 규칙 하나에 중단점을 걸 수 있고, 손으로 쓴 C++ 조건(아래)을 같은 자리에
넣을 수 있습니다. 생성 코드는 그 레코드 줄의 해시가 맞을 때만 쓰이므로(`compiled.h`의 `RecordHash`), 규칙을
고치고 다시 생성하지 않아도 그 규칙만 인터프리터로 돌고 결과는 같습니다. 크기가 중요한 빌드(WebAssembly)는
`HDTRANS_INTERPRET_RULES=ON`으로 끌 수 있습니다.

생성 코드의 모양(규칙 하나):

```cpp
// local 3
//     src[+1] == "あそば" and ko[+1] == "하&AE@" and pos[+1] is 4**
bool RuleL3([[maybe_unused]] Item* p) {
    return (SrcEq(p, 1, "あそば"sv) && DstEq(p, 1, "하&AE@"sv) && PumsaEq(p, 1, "4**"sv));
}
```

원자 함수(`src/core/rules/compiled.h`)는 `eval.cpp`의 원자 하나를 같은 순서로 부릅니다: 왼쪽 단어, (단어 피연산자면)
오른쪽 단어, 없는 단어 확인, 후보 번호(`GetCurrentCompDI`), 검사. `||`, `&&`는 C++의 단락 평가와 같고, `&N`은 각
피연산자를 순서대로 문장으로 평가하는 람다입니다(함수 인자는 평가 순서가 정해지지 않으므로 쓰지 않음). 대입은 같은
호출을 같은 순서로 하므로 "거짓이어도 남는" 부수 효과도 그대로입니다. UTF-8이 아닌 리터럴 바이트는 8진 이스케이프로,
주석의 그런 바이트는 `\xHH`로 써서 MSVC `/utf-8`에서 경고가 나지 않습니다.

## 빌드 통합

| 단계 | 네이티브(macOS, Linux, Windows x86/x64) | 크로스(WebAssembly) |
|---|---|---|
| 전역 규칙 텍스트 → 라이브러리 | `cmake/builtin_tables.cmake`(CMake 스크립트) | 같음 |
| 생성 코드 `compiled_gen.cpp` | 커밋된 파일을 컴파일, ctest `rules.generated`가 다시 만들어 비교 | 커밋된 파일 |
| 검사 | ctest `rules.dump`가 쓴 프로그램에 `rules.lint`(오류만 실패), `rules.verify`, `rules.identity` | 없음 |

`hdrulec`는 라이브러리에 링크하지 않고 규칙, 코드 소스(`load.cpp`, `eval.cpp`, `match.cpp`, `codes.cpp` …)만으로
빌드합니다. 그래서 생성 코드가 깨져도 생성기는 빌드됩니다. Windows의 x86 빌드도 x64 호스트에서 그대로 돌릴 수 있지만,
생성물을 커밋하므로 어느 플랫폼도 빌드 중에 생성기를 돌리지 않습니다.

## 검증

| 검사 | 결과 |
|---|---|
| 앞단 대 `rules::Load` | 덤프 동일(노드 35,052개, 리터럴 107,284바이트) |
| 옛 → 새 → 옛 변환 | 5,125개 모두 정규화한 트리가 같음. 모든 응답 대본에 대한 호출 순서 비교(번역 검증): 5,594만 경로, 0개 다름(33개는 2^20 경로에서 멈춤). 1개(`L 1211`의 `"22주의"`→`"R주의"`)는 같은 뜻의 다른 철자 |
| 생성 코드 대 인터프리터 | 5,125개 모두 생성한 빌드에서 5,594만 경로, 0개 다름. 커밋한 536개는 996만 경로(ctest는 규칙당 2^14 경로). 원자 순서를 바꾼 돌연변이를 잡음 |
| 번역 동일성 | 인터프리터, 536개, 5,125개 빌드가 웹소설 51장(`tools/e2e.sh` 0줄)과 `examples/` 18개에서 같은 출력 |
| 규칙 테스트 | `tests/rules/cases/*.tsv` |

호출 순서 비교(`src/rulec/oracle.h`)는 `Item`을 대본으로 대신합니다. `GetObject`의 단어 유무와 모든 `Check…`, `Assign…`의
참거짓을 대본에서 꺼내고, 모든 호출과 인자를 기록하며, 깊이 우선으로 모든 경로를 엽니다. 두 구현이 모든 대본에서 같은
호출을 같은 인자로 같은 순서로 하고 같은 값을 내면, 실제 단어에서도 같습니다.

## 규칙마다 테스트와 디버깅

규칙 하나가 클래스 하나처럼 따로 고치고 따로 시험할 수 있어야 합니다. 클래스를 손으로 5천 개 쓰는 대신:

- **테스트**: `tests/rules/cases/`의 행이 규칙 하나를 이름합니다. `rule` 열에 그 규칙을 이름한 사전 행의 id를 적고,
  번역 전체는 `expected`에, 그 규칙이 고른 번역은 `rule:dict:<id>=<번역>`에, 규칙이 손대지 않아야 하는 줄은
  `!rule:dict:<id>`에 적습니다. 실패는 케이스 행과 기대값, 실제값을 보입니다([rule-language.md](rule-language.md#규칙-테스트)).
- **디버깅**: 생성 코드의 함수 `RuleL3`에 중단점을 겁니다. `-why`는 결정한 층과 규칙(`L3`, `globals.txt:G10`)을 적습니다([pipeline-layers.md](pipeline-layers.md)).
- **손으로 쓴 조건(설계만)**: 문법으로 쓰기 어려운 드문 경우를 위해 원자 `call 이름[위치]`를 둡니다(옛 형식
  `N+1== "이름"`). 이름은 `src/core/word/` 안의 표(`이름 → bool (*)(const Item&)`)에서 찾고, 없는 이름은 `hdrulec`와
  `rules::Load`가 모두 오류로 냅니다. 인터프리터는 표로, 생성 코드는 그 함수를 바로 부릅니다. 순수 함수(검사만)여야
  하고 `hdrulec`의 모순, 가림 검사는 이 원자를 알 수 없는 검사로 다룹니다.

## 층과 하나의 규칙 언어

실행 층의 순서는 [pipeline-layers.md](pipeline-layers.md)에 있고, 이번 단계에서는 바꾸지 않았습니다.

나중에 다른 규칙 층도 같은 언어로 옮길 수 있습니다. 조건(창 안의 필드 검사)은 같고, 층마다 *의미*와 *동작*만 다릅니다.

| 층 | 의미 | 동작 | 같은 언어로 쓰면 |
|---|---|---|---|
| local / global | 처음 참 | 후보 선택, 대입 | 지금 그대로 |
| cg-lite (`cg_rules.tsv`) | tier별 고정점 | `select`, `remove` | `layer cg tier 2` 아래 `select ko == "쪽" when src == "側" and src[-1] == "同じ"` |
| senses (`senses.tsv`) | 키별 처음 맞는 행 | 번역 선택 | `sense "前"` 아래 `pick "앞" when src[-1] == "の" and not src[-2] in {…}` |
| constraints (코드) | 모두 적용 | 후보 제거 | 표로 쓸 수 있는 것만 `remove` 규칙으로 |
| 학습 규칙(Brill, CG 학습) | 생성된 순서 | 같음 | 학습기가 AST를 만들어 `hdrulec`의 출력기로 씀. 통계는 주석 |

이렇게 하면 진단, 테스트 줄, `-why`의 `파일:규칙`, 생성 코드가 모든 층에 같이 쓰입니다. 층마다 의미가 다르므로
`layer` 머리에 의미를 적고(처음 참, 모두 적용, 고정점), 검사도 의미에 맞게 합니다(고정점 층에서는 가림 검사를 하지 않음).

## 상태

- 된 것: 두 문법의 파서와 출력기, 검사 21종, 변환기, 덤프, 해시, L 0–499와 전역 38개의 생성 코드(커밋),
  규칙 테스트, PRIO, `-why`의 층, 규칙 열, ctest `rules.lint`, `rules.verify`, `rules.identity`, `rules.generated`.
- 남은 것: 나머지 규칙의 생성 코드(측정은 끝남), Windows MSVC x86에서 생성 코드 빌드 확인(CI), 읽기 쉬운 문법을
  원본으로 바꾸기(그때는 `.txt`를 생성물로), `call` 원자, 다른 층의 이전.
