# 번역 품질 벤치마크

2026-10-20에 고정한 C++ 엔진과 기본 사전으로 측정했습니다.
0.2.0 릴리스 소스를 빌드한 기준선과 개선 엔진을 비교합니다. 원본 ezTrans의 전체 벤치마크는 실행하지 않았습니다.

## 공개 회귀 테스트 결과

| 구분 | 문장 수 | 0.2.0 소스 빌드 | 개선 C++, 스냅샷 | 개선 C++, TSV |
|---|---:|---:|---:|---:|
| 수정 대상 `fix` | 30 | 0/30 | 30/30 | 30/30 |
| 유지 대상 `keep` | 30 | 30/30 | 30/30 | 30/30 |
| 알려진 짧은 예시 `example` | 6 | 6/6 | 6/6 | 6/6 |
| 합계 | 66 | 36/66 | 66/66 | 66/66 |

점수는 고정한 한국어 기대 출력과의 완전 일치 수입니다. 띄어쓰기와 문장부호도 비교합니다.
직접 만든 중립 문장 60개를 10개 주제별로 수정 3개와 유지 3개씩 골랐고, README의 짧은 예시 6개를 더했습니다.
수정 확인용으로 선별한 테스트이지 무작위 표본이나 일반 번역 정확도 평가는 아닙니다.
예시 6개는 0.2.0에서도 이미 통과하므로 이번 변경의 신규 개선 수에 넣지 않습니다.

- [입력과 고정 기대값](../benchmarks/neutral.tsv)
- [0.2.0 실제 출력](../benchmarks/baseline/results.tsv), [측정 정보](../benchmarks/baseline/summary.json)
- [개선 엔진의 실제 출력](../benchmarks/improved-snapshot/results.tsv), [측정 정보](../benchmarks/improved-snapshot/summary.json)
- [개선 엔진 TSV 결과](../benchmarks/improved-tsv/results.tsv), [측정 정보](../benchmarks/improved-tsv/summary.json)

## 사전과 규칙으로 고친 내용

아래는 같은 입력을 두 C++ 엔진에 넣은 실제 결과입니다.

| 개선 항목 | 일본어 | 0.2.0 소스 빌드 | 개선 C++ |
|---|---|---|---|
| 인용된 명령과 가능형 구별 | 「本を読め」と言った。 | 「책을 읽을 수 있다」라고 말했다. | 「책을 읽어라」라고 말했다. |
| 복합동사의 의미 유지 | 崖を這い上がる。 | 벼랑을 긴다. | 벼랑을 기어오른다. |
| 과거 진행과 한정 표현 | 本を読んでいただけだ。 | 책을 읽어 받을 수 있고다. | 책을 읽고 있었을 뿐이다. |
| 수동형과 부정 희망 | 気づかれたくない。 | 정신적 피로 싶지 않다. | 들키고 싶지 않다. |
| 과거 원인과 동사 구별 | 静かだったため、眠れた。 | 조용했던 모아 두어 잘 수 있었다. | 조용했기 때문에, 잘 수 있었다. |
| 수혜 방향 구별 | 私は友人からパンを分けてもらった。 | 나는 친구로부터 빵을 나누어 주었다. | 나는 친구로부터 빵을 나누어 받았다. |
| 정중 부정의 이중 부정 방지 | 短い番号は登録できませんね。 | 짧은 번호는 등록할 수 없지 않아요. | 짧은 번호는 등록할 수 없겠네요. |
| 가나 표기 동사 | ご飯をよそる。 | 밥을휜다. | 밥을 담아낸다. |
| 관용 표현 | 示しがつかない。 | 계시가 대하지 않는다. | 체면이 서지 않는다. |
| 구어체 형용사 복원 | 坂は危ねぇ。 | 비탈은 위 응. | 비탈은 위험하다. |

사전에는 빠진 복합동사, 가나 표기와 관용 표현을 보강했습니다.
활용과 문맥 조건을 함께 처리해 `読める`의 가능형, `いただけますか`의 부탁, `水をためた`의 동사처럼 비슷한 표현은 유지합니다.
명령 인용의 활용 규칙, 정중 부정 어미와 구어체 형용사 처리도 좁은 조건으로 고쳤습니다.

## 측정 조건

| 항목 | 값 |
|---|---|
| 기준선 | 0.2.0 릴리스 커밋 `95716099b83a49a6b0b3e2d176778600fd1b40d7`의 소스 빌드 |
| 개선 소스 | `9668adc493919de5fd33e45e8ea6fa542ae5c51a` |
| 실행 방식 | macOS ARM64 네이티브 CLI, 기본 번역 모드 |
| 빌드 | AppleClang 21.0.0, macOS 26.5 SDK, Release, 병렬 작업 2개 |
| 입력 단위 | `--each`로 한 줄씩 독립 번역 |
| 사전 | 각 소스에 맞는 기본 TSV, 개선 엔진은 고정 스냅샷도 별도 실행 |
| 외부 설정 | 빈 설정 파일, 빈 사용자 사전, Ehnd 비활성, 캐시 비활성 |

개선 엔진은 릴리스 버전명이 아니라 위 커밋의 번역 동작을 가리킵니다.
측정에 쓴 고정 실행 파일의 버전 표시는 `0.0.0-bf5145d`입니다. 해당 소스의 새 빌드로도 66/66과 동일한 출력을 확인했습니다.
TSV와 스냅샷의 번역 출력은 66행 모두 같았습니다.

입력, 실행 파일, 소스와 사전의 SHA-256은 각 측정 정보 JSON에 있습니다.
소스 해시는 `src/`, `include/`, `cmake/`, `CMakeLists.txt`, `release-version.txt`의 파일별 해시를 정렬해 계산합니다.
사전 폴더는 최상위 TSV의 파일명과 해시를 정렬해 계산하고, 스냅샷은 파일 바이트를 해시합니다.
새로 빌드한 실행 파일과 스냅샷의 해시는 도구와 빌드 경로에 따라 달라질 수 있습니다.

## 재현

CMake, Ninja, C++20 컴파일러와 Python 3이 필요합니다. 저장소 루트에서 실행합니다.
작업 중인 소스 대신 커밋별 소스를 별도 폴더에 풀어 빌드합니다.

```sh
work=$(mktemp -d)
base=95716099b83a49a6b0b3e2d176778600fd1b40d7
improved=9668adc493919de5fd33e45e8ea6fa542ae5c51a
mkdir "$work/base-src" "$work/improved-src"
git archive "$base" | tar -x -C "$work/base-src"
git archive "$improved" | tar -x -C "$work/improved-src"

cmake -S "$work/base-src" -B "$work/base-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/base-build" --target hdtrans_cli --parallel 2
cmake -S "$work/improved-src" -B "$work/improved-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/improved-build" --target hdtrans_cli --parallel 2

python3 -I tools/benchmark-neutral.py \
  --cli "$work/base-build/hdtrans" --dicts "$work/base-src/dicts" \
  --source "$work/base-src" --revision "$base" \
  --label baseline-0.2.0-source --out "$work/baseline"
python3 -I tools/benchmark-neutral.py \
  --cli "$work/improved-build/hdtrans" --dicts "$work/improved-src/dicts" \
  --source "$work/improved-src" --revision "$improved" \
  --label improved-cpp-tsv --out "$work/improved-tsv"

"$work/improved-build/hdtrans" --dicts "$work/improved-src/dicts" \
  --compile-snapshot "$work/improved.hdsnap"
python3 -I tools/benchmark-neutral.py \
  --cli "$work/improved-build/hdtrans" --dicts "$work/improved.hdsnap" \
  --source "$work/improved-src" --revision "$improved" \
  --label improved-cpp-snapshot --out "$work/improved-snapshot"
```

각 실행은 `results.tsv`와 `summary.json`을 만듭니다. 기대값을 수정하지 않습니다.
기준선 36/66, 개선 TSV와 스냅샷 각각 66/66이 비교값입니다.
스크립트는 임시 HOME, 빈 설정과 사용자 사전을 만들고 실제 CLI에 `--each --no-cache`를 전달합니다.

## 추가 회귀 확인

같은 개선 엔진의 완료된 검증 범위입니다. 위 66문장 점수와 합산하지 않습니다.

| 검사 | 결과 |
|---|---:|
| 단위 테스트 | 791개 통과 |
| 주제별 규칙 테스트 | 762행 통과 |
| 고정 규칙 테스트 | 763행 통과 |
| 별도 게임 입력 | 81,864행에서 TSV와 스냅샷의 출력, 선택 로그 일치 |
| 마스킹한 웹 예시 | 18문서에서 TSV와 스냅샷의 출력, 선택 로그 일치 |

별도 변경 검토에서는 웹 20건과 게임 18건의 국소 개선을 확인했습니다.
이는 개선한 부분의 수이지 문장 전체가 정확해진 수나 전체 자료의 번역 정확도가 아닙니다.

## X에 공개했던 개발 기록

현재 C++ 측정과 당시 개발 기록은 구분합니다.

- [첫 번째 글](https://x.com/sokcuri/status/2111440446430019989/photo/1)은 초기 Go 엔진의 의성어와 구어체 처리 실험입니다. 당시 표의 말더듬은 54%에서 91%, 장음은 70%에서 94%, 반각 가나는 60%에서 87%로 바뀌었습니다. 현재 C++의 정확도 수치가 아닙니다.
- [두 번째 글](https://x.com/sokcuri/status/2111443062698451143/photo/1)은 원본 ezTrans와 당시 개발판의 짧은 오역 비교입니다. README에는 중립 예시 6개만 인용했고, 개선 C++ 출력은 이번에 다시 측정했습니다. ezTrans 열은 작성자가 공개한 결과이며 이번에 원본 런타임을 재실행한 값은 아닙니다.
- [세 번째 글](https://x.com/sokcuri/status/2111476236954599694/photo/1)은 당시 개발판의 문학 발췌 전후 사례입니다. 인명과 색상어 구별, 왕태자와 왕위 계승 순위, 신분을 나타내는 높고 낮음, 관용 표현의 개선을 보여 줍니다. 선택한 사례의 정성 비교로, 전체 정확도나 위 C++ 벤치마크 점수로 환산하지 않습니다.
