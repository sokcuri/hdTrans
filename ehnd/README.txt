Ehnd 필터와 사용자 사전

Ehnd는 기본으로 꺼져 있습니다. hdtrans-data/user/hdtrans.config.yaml에
ehnd: true를 적으면 이 ehnd 폴더를, ehnd: <경로>를 적으면 그 폴더를 읽습니다.
상대 경로는 설정 파일이 있는 폴더 기준입니다.

읽는 파일 이름: PreFilter*.txt, PostFilter*.txt, SkipLayer*.txt, UserDict*.txt
대소문자는 구분하지 않으며 하위 폴더는 읽지 않습니다.

함께 든 예제 파일도 이 규칙으로 읽지만 모든 줄이 // 주석이라 적용되지 않습니다.
예제 안에서 필요한 줄의 //를 지우거나, PreFilter_custom.txt 같은 새 파일에
복사해서 작성할 수 있습니다. 필드는 탭으로 구분하고 UTF-8로 저장하세요.

PreFilter: 일본어 원문을 고침
PostFilter: 한국어 번역문을 고침
두 필터의 열: 찾을 문자열, 바꿀 문자열, 우선순위, 정규식 여부(0 또는 1)
UserDict의 열: 일본어, 한국어, TYPE(예제는 1)

전체 형식: https://github.com/sokcuri/hdTrans/blob/main/docs/ehnd-compat.md
