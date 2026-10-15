# 미리보기 읽기 응답성과 최초 이중 패널 생성 검증 (2026-10-15)

기준 코드는 `3330c52`이며 작업 시작 시 작업 트리는 깨끗했다. 원격 코드를
가져오거나 푸시하지 않았다. 이번 범위는 선택/스크롤 중 남아 있던 읽기의
분리와 최초 두 번째 패널 생성 수정이다. 파일 변경 작업 전체의 비동기화는
수행하지 않았다. 모든 데이터는 `/tmp` 임시 디렉터리에 생성했다.
수정 전후·별도 최종 측정 실행과 sanitizer 출력은 [원시 로그](ASYNC_PREVIEW_MEASUREMENTS_2026-10-15.txt)에 보관한다.

## 재현과 원인

`src/ui/preview.c`의 준비 함수는 UI 프로세스에서 다음 함수를 호출했다.

- `preview_session_open`: reader 열기와 4096바이트 peek.
- `core_preview_directory_empty`: opendir/readdir로 첫 실제 항목 확인.
- `preview_session_check`: 경로 lstat와 열린 reader fstat, 변경 시 재개방.
- `preview_session_page`: 순차 행 읽기. 먼 위치는 목표까지 순차적으로 읽는다.
- 링크 대상 읽기 역시 준비 중 동기로 실행됐다.

렌더 함수 자체에는 이 I/O가 없지만 준비 함수가 draw 경로 안에서 실행되어
느린 호출이 끝날 때까지 키 입력과 종료가 정지했다. linker `--wrap`으로
각 호출에 **한 번의 1초 지연**을 넣었다. marker에 기록한 PID가 수정 전에는
UI PID, 수정 후에는 worker PID인 것도 확인했다. 선택 이동 `j`와 종료 `q`를
동시에 보낸 뒤 프로세스 정상 종료까지 측정했다. 후속 호출은 지연시키지
않아 동일 지연을 여러 번 더하는 실험이 되지 않게 했다.

| 지연 호출 | 50x9 수정 전 → 후 (ms) | 100x24 수정 전 → 후 (ms) |
| --- | --- | --- |
| 파일 열기 | 990.6 → 0.4 | 990.9 → 0.5 |
| 앞부분 peek | 990.3 → 0.4 | 990.9 → 0.4 |
| 디렉터리 비어 있음 | 990.9 → 1.6 | 990.9 → 0.4 |
| 텍스트 행 읽기 | 990.7 → 0.5 | 990.8 → 0.6 |
| 메타데이터 검사 | 992.0 → 0.7 | 990.6 → 0.5 |
| 준비된 텍스트에서 다음 페이지 읽기 | 990.4 → 1.4 | 990.4 → 1.6 |

페이지 실험은 본문 준비/포커스 진입 뒤 PgDn으로 추가 행 읽기를 시작했다.
이는 합성 지연 실험의 관측값이며 실제 느린 디스크/네트워크 성능 수치가 아니다.
수정 후 최종 재검사에서도 기본 10개 조합 모두 1ms 안팎이었다. 스케줄러 및 PTY
처리 시간이 포함되며, 모든 환경에서의 상한을 보장하는 값은 아니다.

## 구현과 작업 수명

UI → core → platform 의존 방향을 유지했다. core worker는 ncurses를 호출하지
않는다. 기존 검색/디렉터리 작업의 platform task 생성·bounded IPC·진단 FD·
강제 취소·비차단 회수 기능을 재사용한다. 기존 ReadJob 파일 목록 프로토콜을
미리보기 데이터로 억지 확장하지 않고 `PreviewJob`으로 분리했다.

- 탐색/검색 슬롯 하나와 미리보기 슬롯 하나를 둔다. 탐색/검색은 미리보기의
  느린 시스템 호출이나 회수 완료를 기다리지 않는다. 각 슬롯에는 활성 작업
  하나와 회수 중인 PID 하나만 허용한다. 회수 중에는 같은 슬롯의 추가 fork를
  하지 않아 uninterruptible I/O에서도 자식이 무한히 늘어나지 않는다.
- IPC는 단방향 pipe에서 로컬 socketpair 스트림으로 바꿨다. 응답은 기존
  framing을 사용한다. UI의 페이지 명령은 작은 고정 크기 레코드이며, outstanding
  명령은 하나만 둔다. 쓰기는 비차단이며 실패 시 작업을 닫고 오류를 기록한다.
- worker는 선택 파일의 reader와 기존 512행 ring cache를 유지한다. 완료된
  요청 다음의 페이지/메타데이터 요청은 같은 worker를 재사용한다. 뒤로 이동과
  캐시 범위, EOF 보정, 파일 변경 시 0행 재시작 정책을 유지한다.
- 경로 변경/refresh는 세대를 올리고 이전 worker와 page/link buffer를 닫는다.
  요청에는 세대·시작 행·페이지 용량을 담고 UI는 화면 행/열 변경도 세대 변경에
  반영한다. 처리 중 페이지나 geometry가 달라지면 결과를 읽기 전에 취소한다.
  worker의 EOF 보정/파일 변경에 따른 시작 행 변경은 같은 세대에서만 수용한다.
- busy worker 취소는 socket/진단 FD 및 부분 IPC 버퍼 해제, SIGKILL,
  WNOHANG 회수로 처리한다. 잠시 회수가 남으면 마지막 요청을 UI 상태로 유지하고
  40ms tick으로 재시도한다. “worker closing; retry”를 사용자 오류로 표시하지 않는다.
  탐색/검색도 자기 슬롯 회수 대기 중 취소 가능한 기존 진행 콜백을 사용한다.
- 빠른 미리보기는 loading 문구 없이 완료한다. 100ms가 넘는 준비/페이지 읽기는
  loading 상태를 표시한다. 페이지 준비 중 스크롤 offset과 포커스를 성급히
  잘라내지 않는다. 일반 선택의 완료 자체는 미리보기 포커스를 요청하지 않는다.
  Enter로 텍스트 미리보기를 명시적으로 열었을 때만 해당 세대의 focus 의도를
  완료 시 적용한다. 다음 입력·선택 변경·모달은 의도를 취소하며 미디어 변환에는
  적용하지 않는다.
- 최소 크기 미만, 모드 전환, 미리보기 끄기, refresh, 종료에서 작업/버퍼를
  정리한다. 정상 크기에서 resize는 이전 요청을 취소하고 새 geometry로 요청한다.
- 이미지/PDF signature 확인만 읽기 worker로 옮겼다. 변환 worker, 도구 검사,
  Sixel 지우기/출력, PDF text fallback은 기존 UI/media 정책을 사용한다.
- raw terminal decoder는 raw 입력 필터가 활성화된 동안 keypad 모드를 유지하여
  작업 완료 polling 때 반복해서 전환하지 않는다. 입력이 없어도 제어 문자열을 출력하던 회귀를 idle 검사로
  발견해 수정했다. 처음 raw 모드를 켤 때 terminfo 키 정의는 초기화한다.

동기 core preview/session API는 터미널 없는 기존 단위 검사와 직접 호출자를
위해 유지한다. 실제 앱 main loop에서는 async 읽기를 활성화하므로 UI의 준비
경로는 PreviewJob을 사용한다. quick find의 입력 루프도 미리보기 작업/회수가 남은 동안만 timed polling하여
worker 완료가 추가 키 입력 없이 반영되게 했다. 동일 선택의 idle 상태에는 주기적 내용 읽기나
메타데이터 polling을 추가하지 않았다. 다음 redraw/viewport 변경 때 검사한다.

## 두 번째 패널 최초 생성

문제는 `ui_set_mode`의 빈 candidate에 `load_progress/load_context`가 없는
**최초 생성 구간**이었다. main loop가 다음 차례에 양쪽 콜백을 다시 설정하므로
오른쪽 패널이 계속 동기로 동작하는 문제는 아니었다.

candidate에 활성 패널의 읽기 콜백/context를 복사한 뒤 기존 initialize 경로를
사용한다. 초기화 전체가 성공할 때만 candidate 소유권을 넘긴다. 읽기 실패나
취소에서는 candidate만 해제하며 기존 mode, active, directory, entries,
selected/top, marks를 보존한다. 진행/오류 상태 메시지는 갱신된다.

`tests/panels_first_latency_pty.py`는 앱이 이미 실행된 뒤 단일 목록으로 전환하고
최초 이중 목록 전환의 디렉터리 open을 gate로 잡는다. 수정 전은 PID가 UI이며
800ms 동안 Esc가 처리되지 않았고, gate를 해제한 뒤에야 전환됐다. 수정 후는
별도 worker이며 gate를 유지한 상태에서 약 242–252ms 안에 Esc가 취소됐다.
이 수치는 100ms 로딩 표시 임계값과 Esc decoding/스케줄링을 포함한다.
성공 실험은 gate를 명시 해제한 뒤 약 20–21ms에 완료됐다. 기준 코드의
동기 최초 생성만 복원한 비교 바이너리도 별도로 사용했다.

시작부터 이중 패널인 경우는 ncurses 초기화 이전의 `ui_init` 경로여서 콜백이
아직 없고 동기 초기화가 유지된다. `settings_pty.py`의 저장 후 재시작과 기존
`startup_ui_test`/`panels_ui_test`로 이 경로를 구분해 검사한다. 시작 읽기까지
취소 가능하게 만드는 것은 이번 실행 중 전환 수정의 범위 밖이다.

## 복사·삭제 측정 (변경 작업 비동기화 미구현)

`tests/operation_latency_gate.c`는 실제 작업 콜백 사이의 시간을 측정하고,
작업 실행 중에만 선택한 native 호출에 한 번 1초 지연을 넣는다.
`tests/operation_latency_pty.py`는 실제 ncurses 진행 화면에 Esc를 보내고
결과가 기록될 때까지 측정한다. 결과 후 양쪽 패널과 실제 부분 결과도 검사한다.
아래는 한 번의 로컬 실행 관측값이다. 캐시/파일시스템/부하에 의존한다.
변경 작업은 수정하지 않았으므로 이 표는 개선 전후 비교가 아닌 현재 경로의 진단이다.
원시 로그의 별도 최종 실행에서는 부하에 따라 완료 시간과 취소 지점이 달라진다.

| 실제 임시 데이터 (인위적 지연 없음) | 전체 완료 (ms) | 최대 콜백 간격 (ms) | 별도 Esc 실험 (ms) |
| --- | --- | --- | --- |
| 64MiB 파일 복사 | 169.594 | 5.566 | 25.7 |
| 512B × 2000개 파일 디렉터리 복사 | 34.239 | 0.493 | 25.7 |
| 같은 크기의 디렉터리 삭제 | 18.925 | 0.307 | 26.2 |

Esc 실험은 별도 새 fixture에서 진행 경계 marker를 관찰한 뒤 입력했다.
이 marker는 작업을 멈추지 않는다. PTY 관찰/입력 처리 시간도 포함된다.
64MiB 복사 취소 후 393216바이트 출력이 남았다. 디렉터리 복사는 568개 완료,
569개 출력 항목(마지막 미완료 포함), 290816바이트가 남았으며 원본은 그대로였다.
삭제 취소는 65개를 지우고 1935개를 남겼다. 변경은 rollback되지 않는다.

| 합성 1초 지연 | 최대 콜백 간격 (ms) | Esc 결과까지 (ms) | 관측 부분 결과 |
| --- | --- | --- | --- |
| 큰 파일 read | 1000.176 | 1026.1 | 출력 0B, partial |
| 큰 파일 write | 1000.265 | 1026.0 | 출력 65536B, partial |
| 큰 파일 fstatat | 1000.212 | 1025.9 | 출력 0B, partial |
| 트리 복사 read/write | 1000.149 / 1000.208 | 1015.9 / 1016.0 | 출력 1항목, 0B / 512B |
| 트리 복사 fstatat/readdir | 1000.561 / 1000.561 | 1026.3 / 1026.1 | 출력 디렉터리 생성, 자식 0개 |
| 트리 삭제 fstatat/readdir | 1000.377 / 1000.396 | 1026.3 / 1026.4 | 변경 없음, partial 아님 |
| 트리 삭제 unlinkat | 1000.199 | 1026.1 | 1개 삭제, 1999개 잔존, partial |

근거는 `src/platform/posix.c`의 `copy_file_at` 안 `op_poll → read → write`와
재귀 열거/메타데이터/삭제 사이 `op_poll`이다. 콜백에서 입력을 자주 확인해도
개별 native 호출이 반환하기 전에는 UI가 입력을 처리하지 못한다. 다음 우선순위는
복사/삭제/이동/휴지통의 실제 mutation executor와 진행/취소 IPC 분리다. 읽기
worker처럼 즉시 kill하는 정책은 쓰기 작업에 그대로 적용하지 말고, cooperative
취소·현재 syscall 지연의 한계·정확한 부분 결과·부모 경로 FD 소유권·양쪽 refresh를
함께 설계해야 한다. 도구 탐색과 미디어 source pinning/open도 아직 UI에서 동기다.

## 검사 및 재실행

최종 검사 결과는 아래 목록에 기록한다. 실패한 assertion을 제거하거나 시간 제한을
늘려 통과시키지 않았다. Vim 복귀 검사는 자식이 전혀 없다는 전제를 수정하여
`comm=tfile-preview`의 상주 cache worker만 별도로 구분하며 Vim 종료 검증은 유지했다.
Auto 이미지 검사 역시 고정 send() 읽기 구간이 끝났다는 이유로 완성됐다고
가정하지 않고 기존 wait의 같은 제한 안에서 실제 hint 도착을 기다린다.
자식 comm 확인 도중 정상 회수가 일어나는 /proc 경쟁도 종료된 자식으로 처리한다.

- 신규 native: async I/O가 UI PID에서 실행되지 않는지, 지속 reader/cache,
  오래된 선택/이미 IPC에 완료된 이전 결과/40회 취소/FD 안정성/종료 후 자식 없음, page 도중 취소,
  geometry 축소·복원, 모달·모드 정리, 탐색/검색과 동시 실행,
  최초 dual 성공·실패·취소의 상태/소유권 보존.
- 신규 worker-body harness: 실제 worker 함수를 정상 return까지 실행한다.
  20회 reader/cache 및 먼 이동/뒤로 이동/디렉터리/실패 정리를 검사한다.
  fork/kill 수명 검사는 별도의 실제 프로세스 검사에서 수행한다.
- 신규 PTY: 파일/디렉터리/open/peek/line/metadata 지연 중 탐색+종료,
  50x9/100x24, 최초 dual 성공·취소, 복사/삭제 실제 및 합성 지연.
- 기존 전체 검사와 ASan/UBSan, LSan 결과는 아래 최종 실행 기록에서 구분한다.

```sh
make -j4 check
make check-preview-async check-panel-first
make check-operation-latency
```

연속 휠 110회 검사는 준비 중 이전 페이지로 `more`를 다시 계산하던 회귀를
찾았다. 준비 중에는 기존 스크롤 가능 상태를 보존하고 완료 후 새 페이지로
보정하여 수정했다. Enter로 숨긴 텍스트 미리보기를 다시 여는 포커스 회귀도
명시적·취소 가능한 세대별 focus 의도로 해결했다.

LSan은 이 실행 환경에서 `LeakSanitizer does not work under ptrace`로 종료했다.
**미완료**이며 부모 검사나 ASan 성공을 worker 누수 검사 성공으로 확대하지 않는다.
실제 fork worker는 `_exit`/SIGKILL을 쓰므로 LSan exit 검사가 자동으로 실행되지
않는다. worker-body harness는 그 내부 정상 정리를 검사할 수 있도록 따로 제공했다.
ptrace 없는 별도 터미널에서 다음을 실행해야 한다.

```sh
make -B CFLAGS='-O1 -g -Wall -Wextra -Wpedantic -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer' tests/preview_async_test tests/preview_worker_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/isolated_check.py ./tests/preview_async_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./tests/preview_worker_test
make -B all  # 일반 빌드 복원
```

첫 명령의 실행은 부모의 누수 검사와 실제 자식 수명 검사이고, 둘째 실행은
worker 본문을 in-process로 실행한 정상 정리의 누수 검사다. 둘 다 실제 fork
worker 내부의 모든 실패/강제 취소 시점에 대한 LSan 보증은 아니다. 실제 사용자
터미널에서의 Sixel 시각 확인도 이번 자동 PTY 검사로 대체하지 않는다.


## Sanitizer 실행 범위

최종 소스로 `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`를
사용했다. `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`에서
`preview_async_test`, `preview_worker_test`, `preview_test`, `panels_ui_test`,
`read_job_test`, `terminal_input_test`가 모두 통과했다. 마지막 비동기 검사에는
buffered stale result, 110회 스크롤 중 액션 유지, 명시적 Enter 포커스,
메타데이터 오류 시 보관 그래픽 해제도 포함한다. sanitizer 진단은 없었다.
실제 fork 경로의 ASan/UBSan 실행과 worker-body 정상 정리 harness 실행을
구분하며, 위 결과에 LSan 성공은 포함하지 않는다.


## 최종 검사 결과

- `make -j4 check`: 종료 코드 0. 기존 전체 native/PTY 검사와 신규 preview,
  worker-body, 최초 dual 전환 검사를 포함한다. 관련 수정 뒤 전체 재실행도 0으로
  완료했다. 이미지/PDF/그래픽 복원, 50x9/80x24/100x24/160x32, 축소·복원,
  모달/검색/폴더 탐색, 연속 스크롤, 앱 종료, 이중 패널/전송/복구/일괄 작업이 통과했다.
- `make check-operation-latency`: 종료 코드 0. 실데이터 완료/취소와 한 번의
  native-call 지연을 분리한 17개 조합, 부분 결과 및 양쪽 패널 갱신 검사 통과.
- `keyboard_pty.py`, `preview_focus_pty.py`, `smoke.py`의 원래 assertion을
  유지한 직접 검사도 통과했다. 신규 반복 취소 검사에서는 40회 후 FD가 누적되지
  않았고 종료 후 `waitpid(..., WNOHANG)`가 `ECHILD`임을 확인했다.
- ASan/UBSan: 위 6개 검사 통과. LSan: 부모 및 worker-body 시도 모두 ptrace
  오류로 실패하여 **미완료**. 실제 fork worker 내부 누수 통과로 간주하지 않는다.
- 미실행: 실제 사용자 화면 터미널의 물리적 Sixel 표시·지우기 재확인, 실제 느린
  네트워크/디스크의 성능 실험. 합성 PTY 결과를 그 환경의 성능 수치로 확대하지 않는다.

남은 우선순위는 (1) 복사·삭제 등 변경 executor의 진행/취소 분리,
(2) 미디어 source pinning/open/검증과 cold tool 탐색의 느린 호출 계측·분리,
(3) ncurses 이전 시작 디렉터리/시작 dual 초기화의 취소 UX다. 유지한 동기 core
직접 호출 API와 기존 미디어 준비 경로까지 모두 비동기화했다고 주장하지 않는다.

## 후속: 실제 터미널 전체 영역 사용

실행 화면이 왼쪽에 치우쳐 보인다는 후속 보고를 확인했다. 패널 배치는 이미
ncurses가 인식한 폭 끝까지 사용하지만, 상속된 `COLUMNS=80`, `LINES=24`가
실제 PTY 140×40보다 작으면 폭 60열과 아래쪽 공간을 사용하지 않는 것을 재현했다.
앱 시작 시 이 두 크기 힌트를 제거하여 ncurses가 실제 터미널 크기를 읽게 했다.
이는 앱 프로세스의 환경만 바꾸며 호출한 셸의 환경은 변경하지 않는다.

`make check-fullscreen`은 같은 오래된 환경변수 조건에서 미리보기·단일 목록·
이중 패널이 140×40 전체를 쓰고 100×24, 160×42, 80×20 리사이즈에서도
오른쪽/아래쪽 테두리가 실제 화면 끝에 닿는 것을 검증했다. 기존 `layout_pty.py`
(50×9/80×24/100×24/160×32)와 `first_run.py`도 별도로 통과했다. 이 후속
변경은 전체 회귀 검사를 다시 실행한 것으로 간주하지 않는다.
