하드트랜스 (hdTrans)

일본어를 한국어로 번역하는 오프라인 번역 프로그램입니다.
제작자: 소쿠릿
홈페이지: https://sokcuri.neko.kr/
X: https://x.com/sokcuri
웹에서 써 보기: https://trans.kuri.kr

시작하기

1. 원하는 폴더에 압축을 풉니다. 별도 설치나 소스 빌드는 필요 없습니다.
2. Windows에서는 hdTransGUI.exe를 실행합니다.
   일본어를 넣고 "번역하기"를 누릅니다. 파일은 "문서 번역"에서 고릅니다.
   "한꺼번에 묶어서 번역"(기본으로 켜짐)과 "문서 번역"은 글 전체를 한 문서로 번역하며,
   문서에서 배운 이름을 모든 줄에 같게 씁니다. 그래서 같은 줄도 함께 번역한 글에 따라 다르게 나올 수 있습니다.
   이름 하나는 사용자 사전으로 정하고, 줄마다 따로 번역하려면 이 칸을 끕니다.
   자세히: https://github.com/sokcuri/hdTrans#문서-번역에서-배우는-이름
3. CLI는 터미널에서 압축을 푼 폴더로 이동해 실행합니다.
   입력과 출력은 UTF-8입니다.

   Windows:
   .\hdtrans.exe --in input.ja.txt --out output.ko.txt

   Linux, macOS:
   ./hdtrans --in input.ja.txt --out output.ko.txt

다른 번역 도구에 연결하기

도구가 읽는 위치에 J2KEngine.dll과 hdtrans-data 폴더를 함께 둡니다.
도구에 엔진 경로를 지정하면 원본 이지트랜스를 따로 설치할 필요가 없습니다.
XUnity는 J2KEngineH.dll을 먼저 읽으므로 기존 파일이 있으면 제거합니다.
도구별 설정: https://github.com/sokcuri/hdTrans/blob/main/docs/using-with-tools.md

사전과 설정

모든 운영체제에서 실행 파일 옆 hdtrans-data/dicts.hdsnap이 기본 사전입니다.
이 파일은 필수이며 같은 폴더의 dicts.txt는 사전 버전입니다. 원본 TSV는 필요 없습니다.
user/, 설정 파일과 ehnd/는 선택입니다. 다른 사전은 --dicts, 다른 설정은 --config로 지정합니다.

사용자 사전은 hdtrans-data/user/userdict.txt에 만듭니다.
UTF-8로 저장하고 일본어와 한국어를 탭으로 구분합니다.
userdict로 시작하는 .txt 파일로 나누어도 됩니다.

등장인물, 지명처럼 작품의 이름은 같은 user 폴더의 names.txt에 일본어와 한국어를 탭으로 구분해 적습니다.
사용자 사전과 달리 이름으로 읽어 경칭, 조사, 띄어쓰기를 맞춥니다. 형식은 names.example.txt에 있습니다.
자세히: https://github.com/sokcuri/hdTrans#문서-이름-목록

설정은 같은 user 폴더의 hdtrans.config.example.yaml을 hdtrans.config.yaml로 복사해 고칩니다.
Ehnd 사전은 기본으로 꺼져 있으며 필요할 때 설정에서 켭니다. 번역 품질이 낮아질 수 있습니다.

대사 말투

설정의 speech가 문장 끝을 정합니다. strict(기본)는 -다를 두고, soft는 「」 안과 대사로 보이는 줄을 반말로 씁니다.
0.8부터 「」로 시작하는 대사 줄은 번역한 뒤 말투 수준 규칙이 해체, 해요체 등으로 다시 씁니다
(「白崎、俺が手伝うから大丈夫だ」 → 「시라사키, 내가 도우니까 괜찮아」).
이 단계를 끄려면 설정에 speech_levels: keep을 적습니다.
자세히: https://github.com/sokcuri/hdTrans#대사의-말투-수준

업데이트할 때는 실행 중인 번역 창과 연결 도구를 끄고 같은 폴더에 덮어 풉니다.
직접 만든 사용자 사전과 설정은 유지됩니다. *.example.* 견본은 교체되니 복사해서 쓰세요.

다운로드와 안내: https://github.com/sokcuri/hdTrans
오류, 오역 신고: https://github.com/sokcuri/hdTrans/issues/new/choose
라이선스: MPL-2.0 (LICENSE), Copyright (c) Sokcuri (소쿠릿, https://github.com/sokcuri)
외부 라이브러리 라이선스: THIRD-PARTY-NOTICES.txt
