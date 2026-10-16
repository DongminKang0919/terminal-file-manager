# CLI·README 정비 검증 (2026-10-16)

기준은 03b19db이며 시작 시 작업 트리는 깨끗했다. 기존 main은 argc>2만 거절한 뒤
터미널 검사와 ncurses 초기화로 들어갔다. --help/-h/--version 지원과 앱 버전 정의,
로컬·원격 Git 태그와 GitHub Release·버전 정책은 없었다(설정 파일의 version=1은 앱 버전이 아님).

## 명령행과 버전 정책

main 첫 단계에서 인자를 검사한다. 정보·오류 반환은 setlocale, terminal_session_check,
initscr, ui_startup, 설정·worker 초기화보다 먼저다. UI·파일 정책·시작 로딩 구조는 유지한다.

- --help 또는 -h 단독: 짧은 목적·형식·요구사항·F1/F9·프로젝트 주소를 stdout, exit 0.
- --version 단독: tfile + 버전을 stdout, exit 0. TTY/TERM/HOME/XDG/PATH 도구 불필요.
- 옵션 없는 실행: 현재 cwd 또는 경로 하나. -- 자체도 현재 cwd. -- 뒤 경로는 최대 하나이며
  -folder/--help/--version/--/-도 경로로 처리. 일반 상대 경로 ./-folder도 유지.
- 정보 옵션과 경로·다른 옵션을 섞거나 옵션 반복/묶기, 인자 초과는 stderr의 원인·Usage·도움말
  안내와 exit 1. 옵션 우선순위에 따라 나머지 인자를 무시하지 않는다.
- 일반 non-TTY/시작 오류 exit 1, 정상 종료·시작 취소 exit 0은 기존 정책 유지.

버전 원본은 src/version.h의 TFILE_VERSION 하나다. 첫 시험 배포 전 개발 상태라
0.1 계열의 개발 접미사를 선택했다. 안정판 1.0을 선언하지 않으며 태그/Release는 만들지 않는다.
C는 헤더를 직접 포함하고 Makefile HEADERS 의존성이 변경 시 재빌드를 보장한다.
문서·Makefile에 같은 버전 숫자를 중복하지 않는다. Git/빌드 시각/호스트 경로를 버전 문자열에
넣지 않으므로 소스 아카이브·설치 파일은 Git이나 소스를 실행 시 요구하지 않는다.

## README와 설치 안내

소개·대표 화면·핵심 기능 → 짧은 빌드/실행 → 사용자 설치 → 첫 예제/기본 키 → 제한 →
개발/검증·라이선스 순으로 정리했다. 시스템 설치·DESTDIR·PATH·제거·선택 도구의 긴 설명은
사용자용 INSTALL.md로 옮겼다. 날짜별 설치 검증 기록은 실행 근거로만 링크한다.
휴지통/영구 삭제, 덮어쓰기·교차 FS 이동, 취소·부분 완료·결과 불명확, 이미지/PDF 범위를 보존했다.
수업 팀 코드가 현재 구현에 그대로 남았다는 인상 없이 사용자가 제공한 프로젝트 유래를 적었다.
라이선스 상태는 그대로다. 과거 수동 sanitizer를 최신 HEAD 결과로 바꾸지 않았다.

기존 main/help/image_preview/pdf_preview 네 이미지를 직접 열어 확인했다. main에는 이전 Target: cursor,
초점/하단 안내가 남아 있어 README에 이전 실물 화면임을 명시했다. 현재 main(마킹 유무/이중
패널 포함), F1 도움말, Copy/Move 폼의 새 실제 터미널 캡처가 필요하다. 미디어/PDF 스크린샷에도
이전 Target·하단 안내가 남아 있어 주변 UI를 함께 갱신해야 한다. 이번에 실제 실물 캡처를 얻지 않았고 PTY 재구성을 새 실물
화면이라고 표기하지 않았다.

## 검사 연결과 결과

`make check-cli`/전체 check에 tests/cli_test.py를 연결했다. TERM 없음/빈 값/dumb/잘못된 값과
HOME/XDG 없음/임시 설정의 조합에서 정보 옵션을 검사한다. optional PATH는 비워 둔다.
stdin/stdout pipe/redirect에서도 제어 문자 없이 동일 출력, stderr 없음, exit 0을 검사한다.
잘못된 옵션/인자/조합은 exit 1과 stderr, stdout 없음이어야 한다.
실제 PTY로 -- 뒤 -folder/--help/--version/--/- 경로와 현재 cwd/일반 실행을 검증하고
종료 후 termios·설정 미생성을 확인한다. FIFO 설정·무관한 파일을 포함한 snapshot은
읽기 위해 FIFO를 열지 않고 메타데이터/일반 파일 내용으로 비교한다.

startup_ui_test에는 terminal_session_check/initscr/ui_startup 진입 금지 guard를 추가했다.
정보/오류 경로를 stdscr 초기화 전 실행해 문자열 출력만으로 통과시키지 않는다.
기존 시작·크기·취소·FD/worker 회수 assertion은 유지한다. native sanitizer의 기존
startup_ui_test 대상에도 이 guard가 포함된다. terminal sanitizer에는 CLI suite를 추가했다.
ASan/UBSan 옵션은 의도적으로 빈 HOME/TERM 환경에서도 전달해 sanitizer 오류를 숨기지 않는다.

clean-source의 기존 src/*.h 복사 규칙으로 version.h가 들어간다. Git 없는 복사본에서
--version과 CLI 검사를 실행하고, 다른 src 타임스탬프는 바이너리보다 과거로 맞춘 뒤 버전
헤더만 변경한다. make -q가 dirty(1), 재빌드와 변경된 버전 출력, 원본 복원까지 확인한다.
버전 probe 숫자는 임시 테스트 값이며 제품 버전 원본이 아니다. 설치 검사도 기대 버전을
소스 삭제 전에 확보하고 설치/staging 바이너리 모두 CLI를 검사한다.

문서 검사 대상에 사용자 설치·문서 색인·이번 검증 기록을 추가했고 README/INSTALL의
--help/-h/--version/-- -folder 설명 존재를 검사한다. 동작은 CLI runtime 검사와 함께 검증한다.

| 검사 | 실제 결과 | 시간·로그 |
| --- | --- | --- |
| `make -j2 check` | PASS, exit 0; CLI, 기존 일반/시작 로딩/설정 복원 및 전체 회귀 | 716.09초(11분56.09초), 최대 단일 RSS 155384 KiB; `/tmp/tfile-cli-full.log`, `.time` |
| `make check-install` | PASS, exit 0; Git 없는 빌드·버전 헤더만 변경한 재빌드·복원·설치/staging CLI·소스 삭제 후 UI | 114.33초, RSS 47896 KiB; `/tmp/tfile-cli-install-final.log`, `.time` |
| 마지막 일반/ASan CLI 직접 검사 | PASS, exit 0; 실제 일반 파일 stdout 리다이렉션 추가 확인 | `/tmp/tfile-cli-final-cases.log`, `/tmp/tfile-cli-final-cases-san.log` |
| native ASan/UBSan `--disable-leaks` | 33/33 PASS; 새 startup 진입 금지 guard 포함 | wall 89.11초 / runner 88.916초, RSS 144788 KiB; `/tmp/tfile-sanitizers-cli/` |
| terminal ASan/UBSan `--disable-leaks` | 16/16 PASS; CLI·시작·기존 설정/터미널/Vim 경로 | wall 296.40초 / runner 296.141초, RSS 74084 KiB; `/tmp/tfile-terminal-sanitizers-cli/` |
| 기본 LSan으로 새 sanitizer 바이너리 `--version` | 미완료, exit 1: LeakSanitizer does not work under ptrace | `/tmp/tfile-cli-lsan.log` |
| 문서·계층·Python 구문·diff | PASS | check_docs.py, check_architecture.py, py_compile, git diff --check |
| Clang / 새 OS / 새 커밋 원격 CI / 실제 터미널 픽셀·새 캡처 | 미실행 | 로컬 Clang 없음. 패키지 설치·푸시·태그·Release·라이선스 변경 없음 |

RSS는 동시 프로세스 합산 peak가 아니다. timeout·assertion을 늘리거나 제거하지 않았다.
전체 회귀의 고의 media cleanup 변형에서 발생하는 내부 assertion은 예상한 negative control이며
상위 검사는 PASS였다. 문서 옵션 경계를 검사하면서 -h를 한국어 조사와 분리 표기하도록 정정했다.
그 외 예상하지 못한 검사 실패나 제품 버그는 없었다.

`--disable-leaks`는 명시적 대체 실행이며 LSan 통과가 아니다. 이번 native worker-body harness도
LSan을 제외해 실행했으므로 내부 누수 검증 완료로 보고하지 않는다. CI 기본 누수 검사는 유지했다.
부모 LSan만으로 _exit/강제 종료 worker의 전체 누수를 증명하지 않는 기존 구분도 유지한다.
과거 WSL 사용자 로그를 이 HEAD의 검증으로 소급하지 않았다.

[실행 출력·요약·소스/바이너리 SHA256](CLI_README_EVIDENCE_2026-10-16.txt)을 저장했다.

ptrace가 없는 실제 WSL 터미널에서 동일 runner의 기본 누수 검사를 실행하려면:

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-cli-wsl
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-cli-wsl
```

새 실물 캡처에는 커밋·터미널 종류·화면 크기도 함께 기록한다. 기본 화면(마킹 없음/있음·이중 패널),
F1, 단일/다중 Copy/Move 폼은 100×24를 우선하고 50×9도 확인하면 좋다.
이미지/PDF는 실제 Sixel 표시·지우기 확인과 함께 현재 주변 UI로 다시 캡처해야 한다.
