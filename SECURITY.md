# 보안 정책

hdTrans 엔진은 게임, 후킹 도구, 웹 페이지처럼 다른 프로그램의 프로세스 안에서 외부 텍스트를 받아 처리합니다.
메모리 안전성 문제는 번역 오류와 따로 비공개로 받습니다.

## 지원 버전

최신 릴리스만 보안 수정을 받습니다. 수정은 다음 릴리스로 나갑니다.

## 신고 방법

공개 이슈로 올리지 말고 [비공개 취약점 신고](https://github.com/sokcuri/hdTrans/security/advisories/new)로 보내 주세요.

다음을 적어 주시면 빨리 확인할 수 있습니다.

- hdTrans 버전과 실행 형태(Windows 번역 창, CLI, `hdTransEngine.dll`, `J2KEngine.dll`, 웹, npm)
- 재현 입력. 바이트 그대로 필요하면 파일로 첨부
- 사전이나 설정 파일을 바꿨다면 그 파일
- ASan, UBSan 출력이나 크래시 덤프가 있다면 그 내용

## 대상

- 번역할 입력 텍스트
- 번역 엔진과 그 C API, `J2K_*` 내보내기 함수
- CLI, Windows 번역 창, WebAssembly 빌드와 npm 패키지
- 사전, 사용자 사전, 설정 파일, 스냅샷(`.hdsnap`)을 읽는 코드
- 릴리스 빌드와 배포 과정

다음은 대상이 아닙니다.

- 번역 결과의 품질. [번역 오류 이슈](https://github.com/sokcuri/hdTrans/issues/new/choose)로 올려 주세요.
- hdTrans를 불러 쓰는 다른 프로그램 자체의 문제

## 처리

14일 안에 접수 여부를 답합니다. 확인된 문제는 수정 릴리스와 함께 GitHub 보안 권고로 공개하고, 원하시면 신고자를 권고에 표기합니다.

## 담당

보안 신고는 메인테이너 소쿠릿([@sokcuri](https://github.com/sokcuri))이 처리합니다.

## English

Report vulnerabilities privately through [GitHub private vulnerability reporting](https://github.com/sokcuri/hdTrans/security/advisories/new), not public issues. Only the latest release receives fixes. Reports are acknowledged within 14 days, and confirmed issues are published as GitHub security advisories with the fix release, crediting the reporter on request. Security reports are handled by the maintainer, sokcuri ([@sokcuri](https://github.com/sokcuri)).
