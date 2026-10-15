# 비동기 도입 후 통합 사용 흐름 점검 (2026-10-15)

시작 HEAD는 `81a28fd`이고 작업 트리는 깨끗했다. `36ebf8e`의 읽기/미리보기,
`687fd1d`의 전체 화면, `81a28fd`의 변경 worker 구현과 각 보고서를 대조했다.
직전의 전체 검사 통과를 이번 검사로 대신하지 않는다. 직전 LSan ptrace 제약,
실제 터미널 그래픽 표시 확인, 시작 시 ncurses 이전 읽기와 폼 검증의 동기 경로는
미완료/제한으로 남아 있었다. 이번에는 실제 사용 흐름과 worker 경계 오류를 다룬다.

## 수정 전 재현 기록

### 종료 키가 읽기/검색 진행 중에는 종료하지 않음

- 조건: 격리된 HOME/XDG 및 임시 폴더에서 linker gate로 실제 목록/검색 worker의
  directory open을 붙잡고, 진행 창이 표시된 뒤 F10을 보냈다.
- 기대: 명시적 Quit 키가 종료 의도를 보존하고 읽기 worker를 취소·회수한 뒤 종료.
- 실제: 목록 읽기는 취소만 하고 원래 목록으로 돌아갔고, 검색도 창만 닫았다.
  추가 입력 없이 600ms 안에 종료하지 않았다. 변경 작업 진행 중 F10과 달랐다.
- 근거: `directory_progress`와 `search_progress`는 F10을 취소로만 분류하며
  shutdown flag를 설정하지 않았다.

### 변경 작업의 충돌 팝업이 worker 수명 감시를 멈춤

- 조건: 두 대상을 복사할 때 첫 이름 충돌에서 worker가 UI 결정을 기다리게 했다.
  충돌 창에서 F10을 누르거나 테스트가 worker를 SIGKILL했다.
- 기대: F10은 협력적 취소 후 종료. worker 사망은 추가 입력 없이 불명확 결과와
  확인 경로를 표시하고 회수. 충돌 파일을 덮어쓰거나 다음 대상을 처리하지 않음.
- 실제: generic blocking choice dialog에서 F10을 무시하고, worker 사망 후에도
  이름 충돌 창에 남았다. 600ms 동안 추가 입력이 없으면 결과가 반영되지 않았다.
- 근거: `run_async`가 `ui_choices` 안에서 멈추어 `core_mutation_poll`을 호출하지
  않았다. 정상 진행 루프의 취소/종료/통신 오류 처리가 이 경계에서는 작동하지 않았다.

재현 로그는 별도 원문에 보존한다. 테스트는 제어 가능한 held-call/event를
사용하며 시간 제한을 늘리거나 assertion을 제거해 통과시키지 않는다.

### 불명확 결과의 하단 표시

- 충돌 worker 사망을 자동 반영하도록 수정한 재검사에서, 50×9 하단 알림이
  `[Partial] ! Details`로 표시되는 것을 관찰했다. 최근 결과 본문은 Uncertain이었다.
- 기대: 확인되지 않은 변경을 확인된 부분 완료로 표시하지 않고 하단도 Uncertain.
- 원인: footer는 `Result.partial`을 결과 코드보다 먼저 분류했다. 불명확 결과는
  “변경 가능성 있음”으로 partial flag를 쓰므로 별도 코드 우선 표시가 필요했다.

### 최소 화면에서 미리보기 상태가 같은 문구로 보임

- 연결 세션의 50×9 PNG 선택에서 이미지 Off의 상태가 `[i] Preview`만 보였다.
  이 크기는 본문 한 줄이라 줄바꿈된 `disabled`는 표시되지 않았다. 미지원도 같은
  첫 줄이고 오류 역시 상태어 `failed`가 다음 줄로 밀렸다.
- 기대: 지원하는 최소 화면에서도 비활성/미지원/오류/로딩/빈 파일을 구별 가능.
- 수정 범위: 본문 한 줄에서는 짧은 상태 제목을 사용하고, 일반 화면의 상세 이유와
  기존 긴 오류 스크롤은 유지한다. 이미지 기능이나 터미널 지원을 추가하지 않는다.

### 파일 작업 후 목록 갱신 실패와 미리보기

- 조건: 실제 별도 preview worker로 `moved.txt`의 미리보기를 준비한 뒤, 파일 삭제는
  성공시키고 `app_refresh`만 ACCESS로 실패시킨다. 임시 fixture의 삭제 사실과 성공/
  갱신 실패 분리는 기존 assertion으로 대조한다.
- 기대: 목록 경로·커서·스크롤·마킹의 기존 실패 보존 정책을 지키되 삭제 전 미리보기
  worker/page/path를 무효화한다. 다음 준비는 실제 경로를 새로 검사해야 한다.
- 실제: 삭제 성공·갱신 실패 뒤에도 `preview_job`, `preview_ready`, cached page/path가
  유지됐다. 기존 `load_dir`는 refresh 성공에서만 preview_reset을 호출했다. 다음
  1초 검사 조건에 도달할 redraw가 없으면 삭제 전 빈 파일/본문 상태가 남을 수 있었다.
- 수정: `refresh_operation_lists`의 실패 분기에서도 preview_reset을 호출한다.
  일반 사용자의 단독 `r` 실패는 기존 상태 보존을 유지한다. 취소는 미리보기 읽기 전용
  worker에만 적용하며 파일 변경 worker 정책은 바꾸지 않는다.
- 회귀: controller 검사에 실제 async preview 준비/삭제/의도적 refresh 실패를 연결했다.
  수정 전 캐시 무효화 assertion이 실패함을 확인했고, 기존 파일 작업 결과·갱신 오류·
  목록 상태 보존 assertion은 유지한다.

## 변경과 보존한 정책

- 목록/검색 실행 중과 검색 결과 창의 F10은 공통 shutdown 의도를 남기고 읽기
  worker를 취소·회수한다. 취소 Esc와 종료 F10을 구분한다.
- 충돌 choice 창에 선택적 40ms pending 콜백을 추가했다. 변경 작업에서만
  `core_mutation_poll`을 계속 호출해 worker 종료/통신 손실을 감지한다.
  F10은 협력적 취소 후 실제 worker 종료를 기다린다. Esc/마우스 닫기/resize/
  SIGTERM도 검사했다. 일반 선택창의 기존 blocking 입력 정책은 유지한다.
- 종료 flag가 있으면 패널 refresh를 시작하지 않고 별도 Cancelled refresh 결과를
  남긴다. 확정된 파일 작업 결과를 바꾸지 않으며 기존 두 패널 snapshot을 보존한다.
  native 검사는 두 패널 모두 `app_refresh`/directory open이 호출되지 않음을 확인한다.
- 파일 작업 후 목록 갱신이 실패해도 이전 preview worker/page/path를 무효화한다.
  일반 새로고침 실패의 목록·커서·스크롤 보존은 유지한다.
- 하단 알림은 RESULT_UNCERTAIN을 partial flag보다 우선해 `[Uncertain]`으로 표시한다.
  확인된 성공/실패와 미확인 대상을 섞지 않는 core의 기존 checkpoint 정책은 그대로다.
- 본문 한 줄에서는 Disabled / Unavailable / Error / No tool / Loading / Convert /
  Empty dir 제목을 사용한다. 일반 화면의 설명과 긴 오류 스크롤은 유지한다.
- UI → core → platform 경계, 단일 모달 변경 worker, 입력 차단/대기 입력 폐기,
  cooperative 취소, FD 기준 경로 접근·링크·no-clobber·EXDEV·권한 정책은 유지한다.
  데이터 처리 backend나 취소 시 부분 파일 보존 정책은 변경하지 않았다.

## 검사 범위와 관찰

`tests/integrated_session_pty.py`는 각 크기(50×9/100×24)마다 임시 HOME/XDG와
새 fixture를 만들고 다음을 **한 프로세스에서 연결**한 뒤 같은 설정으로 재시작한다.

1. 마우스 선택/폴더 열기 → 상위 이동 → 정렬 → 필터 → 재귀 검색 → 마우스 Open →
   필터 해제된 결과 위치 확인 → 방문 기록 뒤/앞 이동.
2. 마킹 두 개 → 최초 이중 패널 생성 → 반대 패널 목적지 탐색 → 복사/다른 두 대상
   이동 → 양쪽 실제 파일·목록 및 대상별 Success 결과 확인.
3. 텍스트 페이지 → PNG/PDF/빠른 선택 변경 → 최종 텍스트 복원. 이 연결 검사에서는
   이미지 Off를 명시하며 최소 화면의 Disabled와 실제 Poppler 텍스트 fallback을 확인한다.
   최소 화면의 PDF 제목과 본문은 Tab/Down으로 읽는다. 빈 파일/바이너리/권한 거부
   상태도 구별한다. root 실행이면 실제 권한 거부만 SKIP으로 명시한다.
4. 실제 설치된 Vim에서 편집·저장·종료 → 파일 내용·목록/미리보기·마우스 복원 →
   외부 atomic replacement 후 새 본문 확인. 도구 부재 시 해당 경로는 SKIP이다.
5. 두 Trash 대상 중 두 번째를 외부에서 삭제 → 첫 대상 payload 보존과 Success,
   둘째 Failed 대조 → 별도 영구 삭제 기본 Cancel 및 마우스 실행 확인.
6. 옵션 hidden/wheel 저장 → 정상 종료 → 같은 임시 설정으로 재실행해 복원 확인.

설치된 도구는 Vim 9.1, ImageMagick convert 6.9.12-98, Poppler pdftoppm/pdftotext
24.02.0이다. 실제 PNG/PDF 변환·Sixel protocol/dimensions는 기존 media_real_fit
및 media PTY에서 별도로 검사한다. **PTY cell/protocol 검사를 실제 터미널의
픽셀·잔상 검사로 해석하지 않는다.** 최소 화면은 그래픽 배치 공간이 없을 수 있다.

`tests/async_boundaries_pty.py`는 worker directory open을 gate로 붙잡은 채 F10을
전달한다. gate 해제 없이 종료·회수를 확인한다. 별도 충돌 worker는 선택 대기 이벤트를
확인한 뒤 F10/SIGKILL/Esc/마우스 닫기/resize/SIGTERM을 전달한다. 원본 두 개와
기존 충돌 목적지가 변하지 않고 다음 대상이 생성되지 않는지 확인한다.
수정 전 종료/사망 반영 검사는 600ms 한도에서 실패했고, 수정 후 같은 한도에서
목록/검색 종료 약 2ms, 충돌 F10/SIGTERM 약 23ms에 종료·회수됐다. 합성 gate 검사이며
저장장치 성능 측정이 아니다. crash/cancel/resize 로그의 약 270–380ms는 결과 창을
열고 닫는 PTY 확인 시간까지 포함하므로 순수 UI 반응 시간으로 주장하지 않는다.

기존 관련 검사에서 stale/buffered preview 결과·연속 취소·페이지 중 선택 변경,
검색/목록과 preview 독립 슬롯, 첫 dual 성공/실패/취소, source/peer refresh 실패와
파일 작업 성공의 분리, worker IPC/crash·취소 중 부분 결과와 FD/자식 회수,
queued background 명령 차단, 이동/Trash 안전성을 재검사한다. 재현되지 않은 항목을
새 버그로 기록하지 않는다. 고정 send 읽기 간격만으로 완료를 가정하지 않고 실제
상태/파일/프로세스 이벤트를 기다린다. 기존 assertion과 제한은 유지했다.

## 검사 중 확인한 기대값 변경

첫 전체 회귀 실행은 `keyboard_pty.py`의 최소 화면 바이너리 상태 기대값에서
실패했다. 기존 검사는 잘려 보이던 `Preview`를 찾았고, 수정 화면은 `[i] Unavailable`을
표시했다. 기대값을 최소 화면 `Unavailable`, 일반 화면 `Preview unavailable`로
더 구체화했다. 기존 Tab/클릭/휠 초점 차단과 목록 보존 assertion은 유지하며,
대기 시간을 늘리지 않았다. 키보드·미리보기 초점 관련 검사를 먼저 재실행하고
최종 전체 회귀를 다시 실행한다. 충돌 F10 안내도 최소 폭 42셀 안에 완전히
표시되는지 새 경계 검사에서 확인한다.


## 재실행 및 sanitizer 범위

```sh
make -j4 check
make check-integration
make check-preview-async check-reads check-conflicts check-settings
make check-docs
```

별도 `/tmp` 바이너리를 `-O1 -g -fsanitize=address,undefined
-fno-omit-frame-pointer`로 빌드하고 ASAN_OPTIONS=detect_leaks=0,
UBSAN_OPTIONS=halt_on_error=1로 실제 UI의 연결 세션/경계 검사, panels/controller native,
preview/mutation worker-body harness를 실행한다. worker-body harness는 실제 작업
본문을 in-process로 정상 반환시켜 정리 경로를 검사하는 별도 범위다. 실제 fork worker는
`_exit`/강제 취소를 사용하므로 부모 검사 성공을 worker 내부 전체 누수 검사로
확대하지 않는다.

LSan은 panels와 preview/mutation worker-body 세 범위 모두 재시도했으나
`LeakSanitizer does not work under ptrace` fatal error로 실행하지 못했다.
**LSan 미완료**다. ptrace 없는 실제 터미널에서 아래 두 범위를 따로 실행해야 한다.

```sh
make -B CFLAGS='-O1 -g -Wall -Wextra -Wpedantic -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer' tests/panels_ui_test tests/preview_async_test tests/preview_worker_test tests/mutation_job_test tests/mutation_worker_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/isolated_check.py ./tests/panels_ui_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/isolated_check.py ./tests/preview_async_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/isolated_check.py ./tests/mutation_job_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./tests/preview_worker_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./tests/mutation_worker_test
make -B all tests/panels_ui_test tests/preview_async_test tests/preview_worker_test tests/mutation_job_test tests/mutation_worker_test
```

마지막 두 harness도 모든 fork/IPC/강제 사망 시점의 누수 증명은 아니다.

## README 대조와 수동 점검

README의 빌드 필수 의존성·`make`/`./tfile [directory]`·50×9 최소 크기·메뉴와
커서/마킹·Vim 예외·기본 Cancel·명시적 설정 저장·선택 도구 정책을 현재 코드 및
first_run/설정/연결 검사와 대조했다. 취소 요청 접수와 실제 중단을 구분하고,
진행/충돌 F10 및 결과 불명확 안내를 보완했다. 새 OS에서 apt 설치 전체를 다시
수행한 것은 아니며 과거 WSL 결과를 최신 커밋의 검증으로 확대하지 않는다.

실제 사용자 터미널에서 확인할 짧은 목록:

- Sixel 지원 터미널에서 PNG/PDF ↔ 텍스트를 빠르게 바꾸고 팝업·축소/복원 후
  이미지 위치·지우기·잔상·한글 폭을 눈으로 확인한다. 50×9의 짧은 상태명도 확인한다.
- Vim 편집·저장·복귀 후 화면 전체 크기, 마우스 클릭/휠, Tab 초점과 키 입력을 확인한다.
- 임시 연습 파일로 느린 작업의 Cancel/F10 안내와 실제 종료 대기를 확인하고,
  별도 ptrace 없는 터미널에서 위 LSan 명령을 실행한다. 사용자 파일로 시험하지 않는다.

## 남은 제한과 다음 작업 후보 (우선순위)

1. **높음:** UI에 남은 폼 경로 검증/생성·미디어 source pinning/도구 탐색의 느린 호출을
   계측하고, 실제 정지가 재현되는 호출부터 분리한다. 이번에 전체 I/O 비동기화를 주장하지 않는다.
2. **중간:** ncurses 전 시작 디렉터리/시작 dual 초기화의 취소 UX. 실행 중 첫 dual은
   이미 별도 worker를 사용하지만 시작부터 dual은 다른 경로다.
3. **중간:** 변경 작업의 durable intent/확정 기록과 crash reconciliation 설계.
   현재 불명확 결과는 안전하게 안내하지만 앱 재시작 후 자동 복구/재실행은 제공하지 않는다.

진행 중 kernel 호출이 반환하지 않으면 협력적 변경 worker 종료도 기다릴 수 있다.
실제 장시간 저장장치/네트워크 FS 지연·전원 상실·GUI 외부 뷰어 표시·물리 Sixel
잔상은 미실행이며, 합성 gate와 자동 PTY의 통과로 대체하지 않는다.


## 최종 검사 결과

- **통과:** 최종 `make -j4 check` 전체 회귀, 종료 코드 0. 기존 읽기/검색/preview/
  media/mutation, 실제 Vim·ImageMagick/Poppler, 설정 재시작, 최소/일반/큰 화면,
  native 안전성, 이중 패널/전송/연속 흐름/복구/일괄 작업 검사를 포함한다.
- **통과:** `make check-integration`(두 크기의 18개 경계와 연결 세션, panels/controller),
  별도 키보드/preview focus, `check-preview-async check-reads check-conflicts check-settings`.
  마지막 캐시 수정 후 controller/panels 및 UI 연결·경계 검사도 별도 재실행했다.
- **통과:** 최종 캐시 수정까지 반영한 ASan/UBSan UI 연결·경계 및 controller/panels,
  preview/mutation worker-body harness. 반복 취소의 FD/자식 정리는 기존 실제 worker
  검사에서 확인했고, worker 내부 LSan 통과라고 확대하지 않는다.
- **실패 후 해소:** 첫 전체 실행의 옛 최소 화면 `Preview` 기대값. 관련 assertion을
  명확한 상태명으로 교체한 후 직접 검사와 최종 전체 실행이 통과했다. 캐시 문제도
  수정 전 실제 async worker 회귀 assertion 실패를 확인했다. assertion 삭제/제한 시간
  증가로 실패를 숨기지 않았다.
- **미완료:** LSan 세 범위 모두 ptrace fatal error. **미실행:** 물리 터미널의 픽셀·잔상,
  새 OS 패키지 설치, 실제 장시간 저장장치/네트워크 FS/전원 상실, GUI 뷰어 표시.
  설치된 Vim/변환 도구 경로에 대한 SKIP은 없었다.
- 증거: [재현·검사 출력 원문](INTEGRATION_EVIDENCE_2026-10-15.txt).

원격 푸시는 하지 않는다. 검증한 수정·회귀 검사·README·이 기록을 로컬 커밋으로 남긴다.

## 후속: GitHub Actions에서 드러난 경쟁 조건

로컬 통과가 CI 통과를 뜻하지 않았다. 실제 로그를 대조했다.

- [81a28fd 실행](https://github.com/DongminKang0919/terminal-file-manager/actions/runs/38730618036):
  progress PTY의 정확한 부분 복사 기대값 65536B에 대해 131072B가 기록됐다.
  UI의 `Cancellation requested` 출력 후 실제 Cancel 명령 전송 전에 worker gate를
  해제할 수 있었다. 명령을 보내기 직전 UI를 별도 gate로 붙잡으면 이 잘못된
  순서를 로컬에서도 결정적으로 재현한다. 기대 동작은 UI가 명령을 먼저 전송한 뒤
  요청 접수를 표시하는 것이다. worker 완료/현재 syscall 즉시 중단을 뜻하지 않는다.
- [e36f846 실행](https://github.com/DongminKang0919/terminal-file-manager/actions/runs/38734584910):
  연결 세션에서 회수 중 자식의 `/proc/PID/comm` read가 ENOENT 대신 ESRCH
  (`ProcessLookupError`)를 냈다. child 이름 관측은 둘 다 “이미 사라짐”으로 다뤄야
  하지만 다른 오류(예: EACCES)는 숨기지 않아야 한다.
- 첫 실행의 terminal sanitizer `auto_pty.log`: quick find가 끝난 뒤 고정 80ms send
  읽기 구간만으로 async text preview 완료를 가정해 `ordinary text` assertion이
  실패했다. 실제 본문 도착을 기존 wait 한도 내에서 기다리며 이미지 없음/idle
  assertion은 유지한다. 제한 시간을 늘리지 않는다.

부분 결과 byte/item/status assertion은 그대로 유지하고, UI Cancel 전송 전 gate와
worker 파일 작업 gate를 독립적으로 제어해 순서를 검증한다. procfs는 ENOENT/ESRCH
및 숨기면 안 되는 access 오류를 별도 검사한다.

후속 관련 검사: Cancel 전송 전 UI gate의 수정 전 assertion 실패를 확인했고,
수정 후 단일/일괄 progress PTY와 정확한 부분 결과, procfs 예외 검사, 두 크기의
연결 세션, Auto PTY가 통과했다. ASan/UBSan progress PTY 및
`run_terminal_sanitizers.py --disable-leaks`의 terminal/input/probe/pty/auto_pty/
vim_pty/editor_shutdown_pty/mark_discovery_pty/reads_pty가 모두 통과했다.
LSan을 끈 로컬 실행이므로 누수 검사 성공은 포함하지 않는다. 취소 전송 실패 시의
기존 비차단 재전송 정책은 유지하며, UI는 전송을 먼저 시도한 뒤 요청 접수를 표시한다.
전체 `make -j2 check`와 원격 CI도 후속 커밋으로 재실행하며, 원격 실행 결과는
[Linux checks 실행 기록](https://github.com/DongminKang0919/terminal-file-manager/actions/workflows/linux.yml)에서
커밋별로 구분해야 한다. 이전 로컬 통과만으로 CI 통과라고 보고하지 않는다.
