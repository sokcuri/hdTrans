# 기여와 릴리스

## 브랜치와 병합

- 작업 브랜치는 `develop`에서 만들고, PR도 `develop`으로 보냅니다.
- 일반 PR은 **Squash and merge**로 합칩니다. PR 제목이 최종 커밋 제목이 됩니다.
- 정식 릴리스에 넣을 변경은 `develop` → `main` PR로 모읍니다. 이 PR은 **Create a merge commit**으로 합쳐 개별 변경의 제목을 보존합니다.

오역 수정과 검증 방법은 [fixing-translations.md](fixing-translations.md)에 있습니다.

## 커밋과 PR 제목

[Conventional Commits](https://www.conventionalcommits.org/ko/v1.0.0/) 형식을 씁니다. 커밋과 PR 제목 모두 같은 규칙입니다.

```text
종류(범위): 변경 내용
```

범위는 생략할 수 있고, 변경 내용은 한국어나 영어로 씁니다.

| 종류 | 용도 | main PR의 버전 변화 |
|---|---|---|
| `fix` | 오역, 오류 수정 | patch: 0.1.0 → 0.1.1 |
| `feat` | 새 기능 | minor: 0.1.0 → 0.2.0 |
| `perf` | 성능 개선 | patch |
| `docs` | 문서 | 단독으로 릴리스하지 않음 |
| `refactor`, `style` | 동작을 유지하는 구조, 형식 수정 | 단독으로 릴리스하지 않음 |
| `test`, `build`, `ci`, `chore` | 테스트, 빌드, 자동화, 기타 관리 | 단독으로 릴리스하지 않음 |
| `revert` | 기존 변경 취소 | patch |

```text
fix(rules): 조건 뒤 인용 번역 수정
feat(cli): 사용자 사전 여러 개 지원
perf(dict): 사전 로딩 시간 단축
docs: Windows 빌드 설명 보완
ci: 릴리스 자동화 추가
```

호환되지 않는 변경에는 `!`를 붙입니다. `major:`라는 종류는 쓰지 않습니다.

```text
feat(api)!: 초기화 함수의 인자 변경
```

커밋 본문에 `BREAKING CHANGE: 기존 호출 코드를 수정해야 함`을 적어도 됩니다.
Squash할 때는 이 본문도 남기거나 PR 제목에 `!`를 붙입니다. **0.x에서도 하위 호환성을 깨는 변경은 major를 올립니다**(0.1.0 → 1.0.0).
PR 제목은 CI에서 검사합니다. 원인, 테스트, 번역 판정 숫자는 제목 대신 본문에 적습니다.

## 릴리스

- `develop` 빌드는 CI 아티팩트로만 남고 GitHub Release나 태그를 만들지 않습니다. `main` PR 병합의 **Build**가 성공하면 봇이 버전과 CHANGELOG를 커밋해 일반 push하고 태그와 GitHub Release 초안을 만듭니다. 태그 push의 **Publish npm**이 그 커밋을 전체 빌드, 테스트한 뒤 GitHub Release와 npm에 게시합니다. 별도 릴리스 PR은 없습니다.
- 버전 커밋의 `main` push **Build**는 matrix를 건너뜁니다. 같은 커밋을 태그 빌드가 테스트하기 때문입니다. 이 커밋에 `[skip ci]`를 붙이면 같은 커밋의 태그 push도 건너뛰므로 붙이지 않습니다.
- 최종 버전은 병합된 `main` PR 제목이 결정합니다. `feat`는 minor, `fix`/`perf`/`revert`는 patch, `!`나 PR 본문의 `BREAKING CHANGE:`는 major입니다. 문서나 CI 제목만으로는 릴리스하지 않으며, 개별 커밋은 변경 내역에 씁니다. 첫 기준은 이미 게시된 `0.1.0`이고 `develop` 빌드와 `dry_run=true`는 npm에 게시하지 않습니다.
- 버전 갱신 재시도는 **Release main**에서 `main`과 성공한 main push Build의 `build_run_id`를 지정합니다. 패키지 재시도는 **Build**에서 같은 태그와 `dry_run=false`, npm 게시를 다시 하려면 **Publish npm**을 같은 태그에서 `dry_run=false`로 실행합니다. 이 실행은 이전 Build 결과를 받지 않고 태그 커밋을 처음부터 빌드하고 테스트한 뒤 게시합니다.
