# 설정·터미널 입력 검토 후 수정 (2026-10-04)

기준 HEAD: `a95cf9825f1a79c32381738e4363384d3a4ac5c8`. Linux 작업 복사본의 실제 파일로 검토·실행했다. 기존 미추적 파일 `q`는 변경하거나 커밋에 포함하지 않았다.

## 재현과 변경

- 수정 전 `terminal_input_test`에 `ESC[999~` 뒤 `q`를 넣었을 때 `q` 반환 assertion이 실패했다.
- 기준 HEAD의 terminal_input/image_setup을 별도 `/tmp/tfile-review-before` 바이너리로 빌드했다. PTY에서 진단 완료 → Esc로 옵션 닫기 → 무입력 1.2초 검사 시 출력/갱신 assertion이 실패했다. 첫 진단 화면의 SGR 마우스 [x] 클릭도 창을 닫지 못했다.
- 입력 프레임 보존과 Esc 판별용 대기를 분리했다. 전달한 Esc나 보관 중인 보고서는 원래 입력 대기 정책을 사용하며 유휴 polling을 강제하지 않는다.
- 미등록 일반 CSI는 완결 문자에서 버린다. DA `ESC[?`, 셀 `ESC[6;`, 기존 검사에서 보호하던 표식 누락 DA `ESC[62;`는 종료 문자 c/t까지 격리한다. 취소 뒤 본문을 재생하지 않으며 새로운 ESC [/O 또는 8비트 CSI 프레임은 복구 경계로 사용한다. 표식 없는 임의의 잘못된 응답과 일반 입력의 완벽한 구분은 지원하지 않는다.
- 모든 이미지 진단 루프에서 표준 `mouse_key` 닫기 처리를 사용하고 기존 공통 정리 경로를 거친다. 이미지 지우기, capture 종료와 취소 시 이전 설정 보존을 유지한다.
- README의 두 번째 패널 시작 기본값과 검사 명령·검증 한계를 정리했다.

## 회귀 범위

native 입력 검사는 미등록 CSI 뒤 q/F1/방향키/일반 텍스트, 잘못된 DA/셀 응답의 취소·꼬리 차단·새 프레임 복구를 추가한다. 기존 지연/분할/C1 응답, Unicode와 마우스 검사도 유지한다.

PTY는 Esc 뒤 1.2초 동안 출력 바이트·이미지 프레임·화면 지우기 횟수 불변 및 CPU 증가 2 tick 이하를 확인한다. 첫 화면·이미지 표시·지우기 확인·활성화 확인·질의 대기에서 [x]를 누르고 이미지 정리·기존 설정·옵션 초점·후속 늦은 응답 차단을 확인한다. 같은 프로세스에서 F7 완료 뒤 폴더 탐색·검색·명시적 마우스 복사도 연속 실행한다.

## 실행 결과

- 최종 `terminal_input_test` 및 `python3 tests/terminal_pty.py`: PASS. 로그 `/tmp/tfile-terminal-pty-final.log`.
- `ASAN_OPTIONS=detect_leaks=1 make check-settings-sanitize`: 샌드박스에서는 LSan ptrace 오류. 샌드박스 밖에서 같은 명령 재실행: PASS, LSan 오류/생략 없음. 로그 `/tmp/tfile-settings-sanitize-escalated.log`.

- `python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-review-final`: 샌드박스의 LSan ptrace 오류 후 샌드박스 밖에서 누수 검출을 켠 채 재실행. terminal/input/PTY 모두 PASS, ASan/UBSan/LSan 오류나 생략 없음. 각 로그는 해당 출력 디렉터리에 보관했다.
- 전체 `make check`: PASS (exit 0). 격리 HOME/XDG에서 설정·터미널·core·platform·UI·PTY 및 연속 탐색/전송/실패 복구/일괄 작업을 실행했다. 최종 추가 F7 연속 작업 검사는 별도 PTY 및 최종 sanitizer에서도 PASS. 로그 `/tmp/tfile-full-check.log`.
- 최종 diff 검토 및 `git diff --check`: PASS.

## 실행 검증의 한계

PTY는 터미널 프로토콜과 앱 입력·수명주기를 검증하며 실제 Sixel 픽셀 표시·잔상은 입증하지 않는다. 실제 터미널의 F7 육안 확인은 이번 환경에서 수행하지 않았다. 과거 환경변수 기반 육안 확인과 구분한다. 전체 파일시스템 엔진을 새로 완전 감사한 결과가 아니다.
