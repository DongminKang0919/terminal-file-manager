# 파일 변경 worker와 취소·종료 검증 (2026-10-15)

기준 HEAD는 `687fd1d`이며 시작할 때 작업 트리는 깨끗했다. 직전
[응답성 검증](ASYNC_PREVIEW_VALIDATION_2026-10-15.md)과 원시 기록을 읽었다.
그 기록의 복사·삭제 실행은 여전히 UI의 동기 콜백 사이에서 native 호출을
실행했다. 이번에는 복사·이동/이름 변경·삭제·휴지통의 **실행**을 분리했다.
사용자 파일이나 실제 사용자 Trash를 테스트하지 않았다. 모든 fixture와
HOME/XDG/Trash, 모의 공간 부족·통신 오류는 임시 디렉터리 안에서 다뤘다.
원격 푸시는 하지 않는다.

## 병목과 선택한 구조

기존 흐름은 `UI run_file_operation/run_batch_operation/run_trash_operation →
core_transfer_progress/batch_execute → POSIX backend`였다. 단일 이동과
이름 변경은 `core_transfer`를 UI에서 직접 호출했다. 진행 콜백 사이의
`read`, `write`, `fstatat`, `readdir`, `unlinkat` 등이 느리면 UI 입력도
같이 멈췄다. 직전 기록의 1초 지연/약 1,026ms 취소 지연을 이번 기준 바이너리로
재확인했다. 동일 테스트 래퍼를 기준 커밋 소스의 별도 `/tmp` checkout에서도
빌드하여 수정 전후를 비교했다.

읽기/미디어의 fork·IPC 구조를 검토했다. 미디어의 프로세스 그룹 강제 종료와
읽기의 SIGKILL/부모 사망 SIGKILL은 변경 작업에 적용하지 않았다. 기존 task
transport의 bounded framing·비차단 UI 수신·진단 FD·FD 격리·회수를 재사용하되
**변경 전용 슬롯 하나**와 협력적 제어 채널을 추가했다. UI → core → platform
경계를 유지한다. core worker에는 ncurses/입력 콜백을 넘기지 않는다. fork로
물려받은 UI 충돌 콜백도 실행 전에 worker의 IPC 콜백으로 교체한다.

- UI가 확정한 원본 경로·이름·목적지·대상 순서를 고정한 BatchJob을 사용한다.
  단일 작업도 내부적으로 한 대상의 snapshot을 만든다. 시작 전 할당 실패는
  파일을 변경하지 않는다. fork 후 worker의 문자열/결과 배열은 사유 사본이다.
  UI는 작업 종료 전 이 요청을 해제하거나 대상 경로를 변경하지 않는다.
- 파일시스템 executor는 기존 backend를 그대로 사용한다. 부모 FD pinning,
  no-replace, 심볼릭 링크 비추적, source 재검증, 깊이/FD 제한, umask/권한,
  원자적 이동 및 EXDEV 거절, Trash payload/metadata/동기화 정책을 유지한다.
  snapshot은 파일시스템 identity의 원자적 스냅샷을 뜻하지 않는다.
- worker의 기존 안전한 콜백 지점에서 취소 명령을 확인한다. 이동은 부모 FD를
  확보한 뒤 원자적 rename 직전에도 확인한다. rename 자체를 중간에 취소하거나
  copy-and-delete로 대체하지 않는다.
- 진행 메시지는 최초/대상 전환/100ms 간격이다. 재귀 총량을 구하려고 사전
  순회하지 않는다. 완료 수·성공한 write 바이트·현재 경로를 표시하고 최상위
  대상 수와 재귀 항목 수를 구분한다. 긴 진행 경로는 1023바이트로 제한한다.
  최근 결과에는 고정한 원본 경로와 목적지/이름을 보존한다.
- UI는 한 tick당 최대 32개 IPC 레코드와 최대 64개 입력 이벤트를 처리한다.
  정상 진행 화면은 최대 초당 10회 갱신하며 10ms 간격으로 양쪽을 확인한다.
  대상별 확정 레코드는 결과를 잃지 않도록 전송한다. 따라서 많은 최상위 대상은
  O(N) 결과 메시지가 필요하며, socket backpressure로 worker가 느려질 수는 있다.
  단일 파일의 청크마다 화면/메시지를 보내는 구조는 아니다.
- 진행 중에는 취소·리사이즈·F10 종료만 처리한다. 일반 탐색/추가 작업 키는
  실행하지 않으며 종료 시 대기 입력을 비운다. q는 기존 모달 정책대로 일반
  background 명령으로 실행하지 않는다. 종료는 F10 또는 종료 signal이다.
  작업 목록/백그라운드 manager/동시 변경 작업은 만들지 않았다.
- 이름 충돌은 worker가 멈춰 UI 결정을 기다린다. Stop/Skip/Skip all/Cancel은
  UI의 기존 창에서 선택하고 worker에 전달한다. 단일 전송의 기존 충돌 오류와
  폼 편집 흐름은 유지한다. 부분 복사 충돌과 다른 오류는 건너뛰지 않는다.

## 상태 전이와 취소·종료 정책

`준비 → 실행 → (충돌 결정 대기 / 취소 요청됨) → 결과 수신 → 실제 자식 종료 확인
→ 최근 결과 기록 → 양쪽 목록 갱신`이다. 통신/worker 실패 시에는
`결과 불명확 → 협력적 중단 요청 → 실제 종료 확인 → 불명확 결과/목록 갱신`이다.

- Cancel/Esc/Enter/닫기/리사이즈를 받으면 UI는 즉시 `Cancellation requested;
  waiting for worker cleanup`을 표시한다. 이 문구는 **UI의 요청 접수**이며
  kernel 호출 중단이나 worker 종료를 뜻하지 않는다. 취소 명령이 일시적으로
  쓰이지 않으면 후속 tick에서 재전송한다. 입력/표시는 worker 콜백을 기다리지 않는다.
- 느린 syscall은 계속 실행될 수 있다. worker는 다음 안전한 지점에서 정리하고
  최종 Result를 보낸다. 이미 완료한 변경은 되돌리지 않으며 부분 복사 파일이나
  디렉터리를 임의로 삭제하지 않는다. 최종 완료 메시지뿐 아니라 `waitpid(WNOHANG)`의
  실제 정상 종료까지 확인한 뒤 새 변경 작업을 허용한다.
- 마지막 변경을 이미 끝낸 뒤 취소가 도착하면 성공일 수 있다. 완료 직전 gate
  검사는 실제 출력이 완성된 상태의 Cancel 뒤에도 Success가 유지되는지 확인한다.
  취소 요청이 있었다는 이유로 성공을 Cancelled로 바꾸지 않는다.
- F10, SIGINT/SIGTERM/SIGHUP은 UI에서 취소를 접수하고 **정리 후 앱 종료**한다.
  이미 취소를 요청한 뒤에도 F10을 받는다. 느린 syscall 때문에 종료가 오래
  걸릴 수 있다. 자동 강제 종료 시간 제한이나 SIGKILL fallback은 없다.
- 부모가 예기치 않게 사라지면 변경 worker의 parent-death SIGTERM handler가
  협력적 중단 flag를 설정한다. IPC EOF도 중단 요청으로 처리한다. 부모가
  복구/결과 화면을 표시할 수는 없으며, persistent journal도 없으므로 경로를
  직접 확인해야 한다. syscall이 무기한 멈추면 orphan도 그 반환까지 남을 수 있다.
  강제로 죽인 UI의 terminal mode 복구를 보장하지 않는다.
- worker에 직접 온 SIGINT/TERM/HUP도 flag를 설정한다. SIGPIPE는 무시하여 IPC
  단절이 변경 도중의 강제 종료로 이어지지 않게 한다. 실제 worker SIGKILL,
  비정상 exit, 중간 EOF/잘못된 레코드/누락된 대상 checkpoint는 결과 불명확이다.
- 대상별 checkpoint는 순서·중복·길이·카운터를 검사한다. 최종 성공은 모든
  대상 checkpoint와 최종 집계가 맞아야 수용한다. 정상 종료와 유효한 final이
  모두 있을 때만 아직 시작하지 않은 대상을 Unexecuted로 확정한다.
- `RESULT_UNCERTAIN`과 `BATCH_UNKNOWN`을 추가했다. 이미 수신한 확정 성공/건너뜀/
  실패/취소는 보존하고, 확인하지 못한 대상은 성공 또는 미실행으로 단정하지 않는다.
  불명확 결과의 partial은 **변경 가능성**을 나타내며 UI는 확정된 부분 완료와
  구분해 표시한다. 마지막으로 확정된 카운터만 보고하며 재시도 전 원본·요청 목적지·
  대상 목록을 확인하도록 안내한다. Trash의 미확정 결과는 source filesystem의
  Trash files/info와 XDG/HOME Trash도 확인해야 한다. crash 직전 선택한 실제
  Trash payload 경로가 전송되지 않았을 수 있다.
- 통신 오류가 난 작업은 제어 채널을 닫고 협력적 TERM을 요청하되 실제 종료까지
  UI의 진행/종료 처리를 유지한다. 불명확 정리 대기 상태도 별도 표시한다.
- 종료/취소가 확정된 뒤 기존 controller/batch_finish에서 양쪽 목록을 갱신한다.
  작업 Result와 원본 refresh/peer_refresh Result를 분리하고 성공/건너뜀/실패/
  취소/미실행/불명확을 최근 결과 화면에 연결한다. 확정 성공만 선택 해제하며,
  불명확 target의 집계 수만으로 성공을 추론하지 않는다.

## 동일 조건의 수정 전후 측정

[원시 로그](ASYNC_MUTATION_MEASUREMENTS_2026-10-15.txt)에 재실행 자료를 보관한다.
PTY에 입력을 쓴 monotonic 시각을 기준으로 다음을 별도로 측정했다.

1. UI 입력 처리: 부모 `input_wide`가 취소 이벤트를 반환한 시각. 단독 Esc의
   구분 대기 약 25ms를 포함한다.
2. 취소 표시: PTY 화면에 `Cancellation requested`가 나타난 시각. worker 종료
   전이며 fixture의 지연 gate가 아직 풀리지 않았음을 확인한다.
3. 실제 종료: 자식 PID가 회수되어 /proc에서 사라진 시각. 기준 동기 버전에는
   자식이 없으므로 동일 executor가 반환한 시각이다. final UI 반영은 별도 흐름이다.

### 합성 1초 지연: 단일 호출 반환은 여전히 느리지만 UI는 반응함

64MiB 일반 파일 또는 512B 파일 2000개의 트리를 사용했다. 각 지정 native
호출에 한 번만 1초 지연을 주입했다. 뒤의 호출은 지연시키지 않았다.

| 호출/fixture | 수정 전 입력 처리 (ms) | 수정 후 입력 처리 (ms) | 수정 후 취소 표시 (ms) | 수정 후 실제 worker 종료 (ms) |
| --- | --- | --- | --- | --- |
| 큰 파일 read | 1025.3 | 35.2 | 56.0 | 1006.5 |
| 큰 파일 write | 1025.5 | 35.2 | 35.8 | 1006.2 |
| 큰 파일 fstatat | 1025.5 | 34.9 | 35.3 | 1007.2 |
| 트리 복사 read | 1015.6 | 25.3 | 25.8 | 997.0 |
| 트리 복사 write | 1015.4 | 35.1 | 35.7 | 997.7 |
| 트리 복사 fstatat | 1025.6 | 35.2 | 35.7 | 996.1 |
| 트리 복사 readdir | 1025.6 | 35.2 | 35.8 | 996.7 |
| 트리 삭제 fstatat | 1025.8 | 34.6 | 35.0 | 1006.7 |
| 트리 삭제 readdir | 1025.3 | 35.0 | 35.5 | 1006.5 |
| 트리 삭제 unlinkat | 1025.4 | 34.6 | 35.1 | 1007.9 |

이것은 로컬 PTY 합성 지연 수치이며 실제 느린 저장장치 측정값이 아니다.
1000ms보다 작은 종료 측정값은 marker 관찰 후 입력까지의 시간 차 때문이다.
수정 전에는 취소 접수/정리 중 표시가 없고 syscall 반환 뒤에만 입력이 처리됐다.
수정 후에도 worker의 최대 콜백 간격은 약 1000ms다. read 취소는 출력 0B,
write 취소는 이미 기록한 65536B(트리 512B), unlink 취소는 1개 삭제/1999개
잔존을 그대로 보존했다. 메타데이터 단계에 따라 아직 변경이 없거나 빈 대상
디렉터리가 남는 경우도 구분했다.

### 인위적 지연 없는 실제 임시 데이터

같은 로컬 파일시스템에서 각 fixture를 새로 만든 한 번의 관측이다. 저장장치
종류나 냉각 캐시를 통제한 벤치마크는 아니며 캐시·부하·PTY 관찰 비용이 섞인다.

| 실제 fixture | 수정 전 executor 시간 (ms) | 수정 후 executor 시간 (ms) |
| --- | --- | --- |
| 64MiB 파일 복사 | 90.971 | 93.831 |
| 512B × 2000개 트리 복사 | 30.264 | 44.792 |
| 같은 트리 삭제 | 12.994 | 12.398 |

별도의 실제 취소 실험은 syscall 지연 없이 취소가 실행 중에 도착할 기회를
주기 위해 256MiB 파일/10000개 파일을 사용했다. 수정 후 UI 입력 처리는
34.7–35.0ms, 실제 종료는 45.5–46.0ms였다. 그 시각까지 큰 파일은 35323904B,
트리 복사는 1507개/771584B, 트리 삭제는 4419개가 처리됐다. 원본/잔여물과
보고 값이 일치함을 검사했다. 부분 처리량은 속도·취소 시각에 따라 바뀌며
기준 버전과 같은 부분 개수를 강제하지 않는다. 실제 저장장치의 장시간 지연,
네트워크 파일시스템, 전원 상실 내구성은 이번 측정의 범위가 아니다.

### 종료와 화면 크기

1초 read 지연 중 별도 PTY 검사에서 resize/복원, F10, Cancel 후 F10,
SIGINT/TERM/HUP의 요청 표시가 약 10–36ms에 나타났고 실제 종료는
993–1006ms에 완료됐다. ASan 계측 실행에서도 별도로 검사했다. worker를
SIGKILL하면 Uncertain 및 원본/요청 목적지 안내가 나타났고, 부모를 SIGKILL하면
orphan이 다음 안전한 지점에서 정상 종료해 subreaper 테스트가 회수했다.

## 검사와 재실행

- `mutation_job_test`: 실제 별도 프로세스에서 read/write/metadata/completion
  gate의 32회 반복 취소, 완료 직전 취소가 성공을 뒤집지 않는지, 매 반복 FD 수와
  ECHILD, EACCES, 7바이트 부분 write 뒤 ENOSPC/EIO, EXDEV, 실제 move/Trash,
  conflict Skip all, worker SIGKILL, 잘못된 framing, checkpoint 전송 실패 및
  final 앞 checkpoint 누락, 확정 성공과 불명확 결과 분리, 부모 예기치 않은 종료.
- `mutation_worker_test`: 실제 worker body를 in-process로 정상 반환시켜 owned
  snapshot/result/path 정리를 검사한다. fork/IPC/부모 사망 검사는 위 테스트에서
  별도로 수행한다. 이 harness가 실제 fork worker의 모든 누수 검증을 뜻하지 않는다.
- `mutation_shutdown_pty.py`: UI의 취소 표시를 worker 반환보다 먼저 확인하고,
  최소 크기 미만 축소·복원, F10/취소 후 F10/signals, worker/부모 강제 사망을 검사한다.
- 기존 progress/batch-progress pipe gate 검사는 UI의 취소 요청 표시를 본 뒤에만
  worker gate를 해제하도록 동기화했다. 기존 카운터/부분 결과/차단된 배경 입력
  assertion은 유지했다. 계측 바이너리만 linker 래퍼로 gate FD를 자식에 보존한다.
  production worker는 terminal/다른 FD를 상속해 사용하지 않는다.
- 기존 layout 검사는 고정 send 읽기 구간만으로 비동기 page 완료를 가정하던
  실패가 있었다. 본문/EOF가 실제 도착하는 조건을 표준 2초 제한 안에서 기다리고
  원래 셀/EOF assertion을 유지한다. UI 응답성은 별도 held-call 검사로 측정한다.
- 기존 FD/no-clobber/경로 교체/권한·umask/Trash/이중 패널/일괄 선택 회귀도 실행한다.
  최종 실행 결과와 sanitizer 결과는 아래에 별도로 기록한다.

```sh
make -j4 check
make check-mutations
make check-operation-latency
# 원본 기준 checkout /tmp/tfile-mutation-baseline을 준비한 경우에만:
python3 tests/isolated_check.py python3 tests/operation_latency_pty.py --baseline
```

## 최종 결과

- **통과:** 최종 `make -j4 check` 전체 회귀(exit 0), 새 mutation job/worker body/
  shutdown PTY, 기존 progress/batch-progress, 이동·Trash·권한·충돌·FD 안전성,
  preview/read/media, 최초 이중 패널/상태 보존, fullscreen, 최소/일반/큰 화면,
  workflow/recovery/batch 검사. 새 checkpoint 검증 후 별도 mutation job 재실행도 통과.
- **통과:** 수정 전/후 동일 17개 지연·실제 fixture 측정, ASan/UBSan의 아래 범위,
  문서 링크/architecture 검사와 `git diff --check`.
- **수정 후 해소한 검사 실패:** 첫 전체 실행의 layout 비동기 page 준비 경쟁.
  위에 기록한 상태 대기 동기화로 수정한 뒤 최종 전체 검사가 통과했다.
- **미완료:** 환경 ptrace 제약으로 LSan 두 범위 실행 불가. 실제 저장장치의
  장시간 syscall 지연, 전원 상실, 네트워크 FS, crash 후 영구 기록 복구는 미실행.
- 원시 측정·sanitizer·최종 전체 검사 출력은
  [측정 원문](ASYNC_MUTATION_MEASUREMENTS_2026-10-15.txt)에 보존했다.

## Sanitizer 검증 범위와 환경 제약

ASan/UBSan(`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`,
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`)으로 mutation job,
worker body, operations, Trash, batch/conflict, panels native 검사 및 실제 UI의
resize/F10/signals/worker crash/parent death PTY 검사를 통과했다. operations는
기존 fixture runner로 권한을 낮춰 실행했다. worker 진단에 sanitizer 오류가
있으면 통신 실패를 기대한 검사에서도 실패하도록 검사한다.

LSan은 부모/job 및 정상 반환 worker-body harness 모두에서
`LeakSanitizer does not work under ptrace`로 실행하지 못했다. **누수 검사는
미완료**다. 부모 sanitizer 통과를 worker 내부 누수 통과로 해석하지 않는다.
실제 fork worker는 `_exit`하므로 LSan 종료 hook도 자동 실행되지 않는다.
worker-body harness는 같은 작업 본문과 정리 코드를 정상 반환시키는 별도 범위이며,
강제 사망이나 모든 fork/IPC 경로의 누수 증명이 아니다.

ptrace가 없는 환경에서 아래 명령으로 두 범위를 따로 확인할 수 있다.

```sh
make -B CFLAGS='-O1 -g -Wall -Wextra -Wpedantic -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer' tests/mutation_job_test tests/mutation_worker_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/isolated_check.py ./tests/mutation_job_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ./tests/mutation_worker_test
# 일반 빌드 복원
make -B all tests/mutation_job_test tests/mutation_worker_test
```

## 남은 제한과 다음 단계

- 하나의 변경 작업만 허용하는 모달 executor다. 백그라운드 탐색/추가 작업,
  재시작 복구, persistent journal, 일시정지/재개는 없다.
- kernel 호출이 무기한 반환하지 않으면 취소 완료/앱 종료도 기다릴 수 있다.
  자동 강제 kill로 이를 숨기지 않는다. 부모 사망 후 결과가 보존되는 journal이
  없으므로 crash 후 자동 재실행은 안전하지 않다.
- UI의 편집 폼/선택기 사전 검증, 파일/디렉터리 생성, 미디어 source pinning과
  도구 탐색은 이번 실행 분리의 범위 밖이며 일부 동기 호출이 남는다.
- 모든 결과 메시지의 CPU 비용까지 0은 아니다. 많은 최상위 대상의 결과 집계와
  IPC backpressure를 측정한 뒤 묶음 전송을 검토할 수 있다. UI 입력을 막는
  무제한 메시지 drain이나 정확한 총량을 얻기 위한 재귀 pre-scan은 도입하지 않았다.
- 다음 우선순위는 작업/대상별 durable intent·확정 기록과 crash reconciliation,
  폼/생성/미디어 준비의 느린 호출 계측·분리다. EXDEV copy-delete 지원과
  백그라운드 manager는 별도 안전성 설계가 필요하다.
