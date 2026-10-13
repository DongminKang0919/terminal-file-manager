# 느린 읽기 응답성 개선 — 2026-10-13

## 범위와 재현

기준 커밋 `8ee3962`. 앞선 검색 경로 복제·미리보기 할당 최적화의 미커밋 변경을 보존한 채 이어서 작업했다. 원격 푸시·릴리스·시스템 설정·실제 사용자 파일 변경은 하지 않았다.

실제 느린 mount를 새로 구성하지 않고, 테스트 전용 linker wrapper로 디렉터리 열기를 임시 제어 파일이 없어질 때까지 대기시켰다. 수정 전 소스로 별도 빌드한 `/tmp/tfile-scan-baseline`에서 검색·디렉터리 이동 모두 50×9/100×24에서 최소 800ms 동안 취소 입력이 처리되지 않았다. 제어 파일을 제거해야 호출이 반환됐다. 이 실험은 syscall 지연 시 UI 정지를 재현하며 실제 네트워크 파일시스템의 처리량/지연 분포를 측정한 것은 아니다.

수정 후 같은 대기를 풀지 않고 Esc·마우스 Cancel·리사이즈·SIGTERM을 보냈다. 최초 열기뿐 아니라 64번째 항목 이후 열거 호출을 대기시키는 경우까지 검사한다. 입력 이후 화면 복원/취소/종료, worker 회수, 이전 목록 유지와 재시도를 검사한다. 600ms는 회귀 허용 상한이며 개별 실제 결과는 로그에 남긴다.

## 구현

- 메인 목록의 이동·새로고침·방문 복원·필터 적용과 재귀 검색을 읽기 전용 worker 프로세스로 분리했다. 메인 루프 진입 전 초기 목록 로드는 기존 동기 초기화를 유지한다.
- 플랫폼은 fork, framed pipe, 비차단 수신, 신호·FD·자식 회수를 관리한다. worker는 터미널 FD와 관계없는 상속 FD를 닫고 shutdown 신호를 기본 처리로 복원하며 부모 종료 시 SIGKILL을 받도록 설정한다. worker 진단은 익명 memfd에 수집해 불완전 종료 시 부모에 오류로 전달한다.
- core는 파일 정보·검색 상태·완료 메시지를 전송하고 한 poll에 최대 128개 메시지를 처리한다. 메시지는 최대 1MiB이며 경로·이름 길이/종료와 배열 용량을 확인한다. 수신이 밀려 있으면 입력 대기를 추가하지 않고, 결과를 기다릴 때만 UI가 짧게 대기한다.
- ncurses 호출은 부모 UI에서만 수행한다. 검색 부분 결과는 순차 전송하며 정상 완료·취소 시 기존 비교기로 정렬한다. 마지막 콜백 이후의 나머지 결과도 전송한다. IPC 단절·worker 오류·부모 OOM은 모은 검색 결과를 유지하며 실패를 표시한다.
- 목록은 완전히 수신한 후 기존 필터·정렬·방문 기록 정책으로 교체한다. 취소/실패는 기존 목록·경로·마킹을 보존한다. 진행 중 다른 탐색을 실행하지 않고 해당 UI 흐름에서만 결과를 소비하므로 취소된 작업이 나중에 다른 패널/경로를 덮어쓰지 않는다.
- 빠른 로드는 대기 중인 키를 소비하지 않는다. 100ms가 지나면 Loading directory 창을 표시하고 Esc/Cancel/닫기/리사이즈로 취소한다. 검색은 기존 진행 화면과 Enter/Esc의 부분 결과 유지 정책을 유지한다.
- 한 worker만 실행하며 취소 후 회수되지 않은 자식이 있으면 새 작업을 거부하고 재시도를 안내한다. 회수는 검색 결과 화면과 메인 루프에서도 비차단으로 수행한다. 앱 종료 때는 최대 약 20ms 동안 회수를 시도하며 읽기 syscall을 기다리는 blocking wait는 하지 않는다. 커널의 uninterruptible I/O 상태라면 즉시 종료/회수를 보장할 수 없으며 부모 종료 신호 설정은 유지된다.

스레드를 추가하지 않아 휴지통의 전역 umask 변경과 동시에 실행되는 worker thread는 없다. 복사·이동·삭제·휴지통 작업의 실행 정책은 그대로다. 느린 읽기 중 탐색을 계속하는 방식 대신 취소 후 다시 탐색하는 방식이다.

## 성능 해석과 남은 범위

최초 열기 대기 실험의 첫 결과에서는 입력 지연이 약 0–41ms였다. worker로 분리하는 목적은 입력 응답성이다. 로컬 디렉터리 처리량 향상을 주장하지 않는다.

10,027개 목록 native 두 실행은 동기 13–18ms, worker 104–115ms(테스트의 poll 사이 1ms 대기 포함)였다. 두 결과의 이름·경로·유형·크기를 대조했다. 실제 UI는 pipe backlog가 있으면 이 대기를 넣지 않는다. IPC 복제와 fork 비용이 있고 worker와 부모의 목록이 잠시 함께 존재하므로 메모리 최고 사용량 감소를 주장하지 않는다.

초기 실행, 폴더 선택기의 목록, 검색 결과를 열기 전 메타데이터 확인, 텍스트 미리보기와 일부 경로 확인은 여전히 동기다. 실제 느린 mount에서의 체감 지연과 가장 느린 개별 메타데이터 호출, 큰 텍스트의 먼 위치 이동은 후속 측정 대상이다. 실제 터미널 육안/네트워크 mount 검증은 이번 실행에 포함하지 않는다.

## 검증

- `read_job_test`: 동기/비동기 결과·순서 대조, 재귀/마지막 일부 결과/숨김/제한/없는 경로, IPC 불완전 종료·worker 진단·부모 OOM, 목록 취소 시 상태 보존, 32회 취소와 FD/자식 회수, 10,027개 목록을 검사한다.
- `scan_latency_pty.py`: 최초 열기/열거 중 대기 × 검색/목록 × Esc/마우스/리사이즈/SIGTERM × 50×9/100×24, 32개 경우를 실행한다. 대기를 풀지 않고 응답해야 한다.
- 기존 search gate는 UI와 worker가 나뉜 구조에 맞춰 부모의 취소를 관찰하며 64개 결과 유지 assertion은 보존한다.
- `check-reads`를 전체 make check에, read_job_test를 native sanitizer에, 느린 읽기 PTY를 terminal sanitizer runner에 포함했다.

직접 실행 결과:

| 검사 | 결과 | 로그 |
| --- | --- | --- |
| 기존 동기 구현 지연 재현 | 검색/목록 × 두 크기, 최소 800ms 입력 정지 재현 | `/tmp/tfile-scan-baseline`로 `scan_latency_pty.py --baseline` 실행 |
| 최초 열기/열거 중 지연 PTY | 32개 PASS, 응답 0.3–27.4ms | `/tmp/tfile-read-full-final.log`의 check-reads 구간 |
| native ASan/UBSan | 27개 PASS(exit 0), LSan 제외 | `/tmp/tfile-read-sanitizers-final.log`, `/tmp/tfile-sanitizers-reads-final-20261013/` |
| 최신 read_job_test 추가 일반/ASan·UBSan | 대규모 목록·worker 진단까지 PASS(exit 0) | `/tmp/tfile-read-native-final.log`, `/tmp/tfile-read-native-sanitized.log` |
| 지연 PTY ASan/UBSan | 32개 PASS(exit 0), 응답 0.4–41.8ms, LSan 제외 | `/tmp/tfile-read-ui-sanitized.log` |
| 전체 회귀 | `make -j4 check` PASS(exit 0), 메인 루프 회수 보강 전 결과 | `/tmp/tfile-read-full-final.log` |
| 최신 메인 루프 보강 후 관련 재검사 | `check-reads` + 기존 검색 UI + terminal PTY PASS(exit 0) | `/tmp/tfile-read-related-final.log`, `/tmp/tfile-read-search-final.log`, `/tmp/tfile-read-terminal-final.log` |
| 최신 메인 루프 보강 후 UI ASan/UBSan | 지연 PTY 32개 PASS(exit 0), 0.5–41.7ms, LSan 제외 | `/tmp/tfile-read-ui-final-sanitized.log` |

최종 메인 루프 보강은 미디어/외부 작업이 있어도 읽기 자식을 독립적으로 회수하도록 조건의 short-circuit를 제거한 것이다. 이 보강 후 전체 검사를 반복하지 않고 관련 일반/ASan·UBSan 검사를 재실행했다. 최신 문서 링크·헤딩 앵커, 계층 경계, 변경 Python 문법과 `git diff --check` PASS. 원격 CI는 이번에 실행/확인하지 않았다.

첫 통합 검사에서 빠른 refresh가 뒤따르는 입력을 소비하는 회귀를 발견했고, Loading 창이 표시되기 전에는 입력을 소비하지 않도록 수정했다. 수정 후 설정 저장/검색/terminal 관련 검사와 위 전체 검사를 통과했으며 실패를 성공으로 기록하지 않는다. 최초 실행의 실패 로그는 `/tmp/tfile-read-full.log`에 보존한다.

재현:

```sh
make -j4 check-reads
make -j4 check
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-reads-new --disable-leaks
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py \
  --output-directory /tmp/tfile-terminal-sanitizers-reads-new --disable-leaks
```

`--disable-leaks`는 ASan/UBSan 검사이며 LSan은 포함하지 않는다. worker는 `_exit`로 종료하므로 child의 종료 시 LSan 검사를 별도로 완료했다고 주장하지 않는다.
