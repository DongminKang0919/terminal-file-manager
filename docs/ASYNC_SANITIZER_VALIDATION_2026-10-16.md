# 비동기 전용 검사 sanitizer 자동화 (2026-10-16)

## 연결 점검과 선택

시작 커밋 `645f1ff`, 작업 트리는 깨끗했다. Makefile의 `check-isolated`는
`check-preview-async`와 `check-mutations`를 통해 네 전용 검사를 이미 실행한다.
일반 회귀 연결은 바꾸지 않았다. `run_sanitizers.py`, `run_media_sanitizers.py`,
`run_terminal_sanitizers.py` 어디에도 이 네 전용 검사는 없었다.
기존 preview/panels/controller/media/terminal 검사가 비동기 경로 일부를 실행하는
것은 전용 IPC/수명/본문 정리 harness를 실행하는 것과 다르다.
이전 비동기 preview/mutation 및 통합 문서(2026-10-15)의 수동 sanitizer 결과는
당시 결과이며 새 자동 실행을 대신하지 않는다.

기존 native runner에 네 대상을 추가했다. 같은 core/UI 소스 구성과 빌드 로그,
3개 병렬 빌드, 순차 실행을 재사용하므로 별도 runner보다 유지보수 비용이 작다.
일반 바이너리를 덮어쓰지 않고 `/tmp/tfile-sanitizers-*` 아래에 빌드한다.
각 실행은 새 임시 HOME과 XDG config/data/cache를 사용한다.
ASan/UBSan과 기본 `detect_leaks=1`을 유지하고 suppression/자동 fallback은 없다.
명시적 `--disable-leaks` 결과는 누수 검사 미완료로 표시한다.

| 전용 검사 | Makefile 대조: 소스 / wrapper / 추가 정의 |
| --- | --- |
| preview_async_test | core+posix+UI(main 제외), ncursesw / platform_task_send, platform_reader_open, platform_reader_peek, platform_reader_line, platform_reader_changed, platform_directory_empty / _XOPEN_SOURCE=700 |
| preview_worker_test | core(preview_job.c 제외)+posix / platform_task_send, platform_task_command / 없음 |
| mutation_job_test | core+posix / read, write, fstatat, renameat2, platform_task_send, platform_task_receive / 없음 |
| mutation_worker_test | core(mutation_job.c 제외)+posix / platform_task_send, platform_mutation_control / 없음 |

두 harness는 해당 worker `.c`를 직접 포함하므로 독립 번역 단위에서는 제외한다.
공통 sanitizer 플래그에 `_XOPEN_SOURCE=700`이 있으며 harness의 `_GNU_SOURCE`와
함께 빌드된다. 테스트 hook 추가 정의는 네 대상 모두 필요하지 않다.

## 검사 범위와 한계

| 검사 | 일반 회귀 | ASan·UBSan | 부모 LSan | worker-body LSan |
| --- | --- | --- | --- | --- |
| preview_async_test | 실제 fork/IPC·stale 결과·취소·자식 회수·FD | 부모 및 실행되는 자식 경로 | 정상 종료 부모의 자원 | 보증하지 않음 |
| mutation_job_test | 실제 fork/IPC·취소·crash·부분 결과·회수·FD | 부모 및 실행되는 자식 경로 | 정상 종료 부모의 자원 | 보증하지 않음 |
| preview_worker_test | 페이지/cache·directory/error 본문 정리 | in-process 본문 | 별도 부모 경로 아님 | 정상 반환까지 실행한 본문 |
| mutation_worker_test | copy/move·협력 취소 본문 정리 | in-process 본문 | 별도 부모 경로 아님 | 정상 반환까지 실행한 본문 |

LSan 열은 기본 실행이 요청하는 범위다. 실제 성공 여부는 아래 결과를 따로 본다.
실제 fork worker의 `_exit` 또는 강제 종료는 정상 종료 시 LSan 검사를 생략할 수 있다.
부모 LSan 통과를 자식 내부 전체 누수 통과로 해석하지 않는다. 본문 harness는 IPC를
wrapper로 대체하여 정상 반환시키므로 정리 자원을 검사하지만 실제 fork/IPC/강제 사망의
모든 중단 지점을 증명하지 않는다. 두 방식은 서로 보완한다.

## CI와 증거

기존 sanitizer job의 native 단계가 네 검사를 실행한다. 보안 권한, action pin,
credential 비보존, timeout, 다른 sanitizer 단계는 유지했다.
검사별 build/run PASS/FAIL·종료 코드를 출력하고 `summary.json`, `summary.log`와
GitHub step summary에 필수 검사 전체 목록·결과·시간을 남긴다. 빌드 실패 대상은
미실행 실패로 기록하며 최종 성공에 포함하지 않는다. 실행 실패 뒤에도 나머지를 실행한다.
기존 native `*.log` artifact 경로가 새 개별 로그도 수집하며 JSON 요약 경로도 추가했다.
원격 CI는 이번 작업에서 실행하지 않았고 통과를 주장하지 않는다.

## 수동 재현 (ptrace 없는 실제 WSL 터미널)

```sh
cd /home/dongmin/workspace/file_manager_project
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-wsl-20261016
# 누수 검사를 생략하는 명시적 대체 실행: LSan 성공으로 해석하지 않는다.
python3 tests/run_sanitizers.py --disable-leaks --output-directory /tmp/tfile-sanitizers-wsl-fallback-20261016
make -j2 check
```

각 출력 디렉터리의 `*.build.log`, `*.run.log`, `summary.log`, `summary.json`을 보존한다.
정상 실행 바이너리 복구를 위한 재빌드는 필요하지 않다.

## 로컬 실행 결과

- 기본 native runner: 31개 모두 빌드 성공, 실행 31개 모두 LSan ptrace fatal로
  실패, 종료 코드 1. 부모/worker-body LSan **미완료**. 새 네 검사도 실제 실행했다.
- 명시적 `--disable-leaks`: 새 네 검사를 포함한 31/31 PASS, 종료 코드 0.
  ASan/UBSan 진단 없음. runner 내부 88.002초, 외부 wall 92.90초.
- 기존 media runner `--disable-leaks`: media/preview/picker/media_pty/media_redraw
  5/5 PASS, 종료 코드 0. LSan 성공은 포함하지 않는다.
- 기본 첫 실행 외부 wall 92.26초, `/usr/bin/time -v` 최대 단일 프로세스 RSS
  156688 KiB; fallback 156016 KiB. 이 값은 동시 프로세스 합산 peak가 아니다.

로그: `/tmp/tfile-sanitizers-default/`, `/tmp/tfile-sanitizers-fallback/`,
`/tmp/tfile-media-sanitizers-review/`, `/tmp/tfile-terminal-sanitizers-review/`.
외부 출력은 같은 이름의 `/tmp/*.log`, native 시간은
`/tmp/tfile-sanitizers-{default,fallback}.time`, 전체 회귀는
`/tmp/tfile-regression.log`, `/tmp/tfile-regression.time`에 있다.
문서·Python 구문·diff 공백 검사도 통과했다.
원격 CI와 ptrace 없는 실제 WSL LSan 재실행은 미실행이다.

최종 runner로 기본 LSan을 다시 실행해 동일하게 31개 실행 실패/종료 코드 1을
확인했다(`/tmp/tfile-sanitizers-measured/`). wall 74.771초,
100ms 간격 736회 `/proc`에서 runner와 모든 자손의 RSS를 합산한 최대값은
208322560 bytes(198.67 MiB)였다. 샘플 사이 순간 peak는 놓칠 수 있으며 RSS는
공유 페이지를 중복 합산할 수 있다. 측정 원문은
`/tmp/tfile-sanitizers-measured.metrics.json`이다. 기존 3개 병렬 빌드에서 OOM 없이
완료했고 20분 CI 예산에 비해 native 실행 시간이 작아 job 분리/병렬도/timeout은
변경하지 않았다. 로컬 시간은 원격 runner 실행 시간 보증이 아니다.

기존 terminal fallback 첫 실행은 mark_discovery_pty에서 실패(종료 코드 1)했다.
별도 재실행에서도 재현했다. 진단 화면은 `Copying`, completed items 0, 기존
marked 상태를 보여 줬다. 고정 `Terminal.send`의 120ms 읽기만으로 worker 완료를
가정한 테스트 경쟁 조건이다. 제품 변경 없이 mark_discovery 검사에서 성공 후
마킹 해제와 진행창 종료를 관찰한 다음 파일/다음 명령을 확인하도록 고쳤다.
2초의 유한 상태 대기를 사용하고 기존 파일 내용·원본 보존·대상 수·마킹 assertion은
모두 유지했다. CI timeout이나 sleep을 늘리지 않았다. 수정 후 동일 sanitized
바이너리로 해당 검사 전체(두 크기)가 통과했다(`/tmp/tfile-mark-fixed.log`).
terminal runner 전체도 다시 실행하여 앞선 실패와 구분한다.

runner 실패 집계도 검증했다. 임시 PATH의 의도적으로 exit 9 하는 가짜 cc를 사용하자
31개 필수 빌드 모두 FAIL·미실행으로 요약되고 runner는 exit 1을 반환했다.
이것은 제품 빌드 실패가 아닌 자동화 오류 처리 검사다.
로그 `/tmp/tfile-sanitizers-build-failure.log` 및 같은 이름 디렉터리에 보존했다.

재사용 출력 디렉터리의 이전 PASS 실행 로그가 빌드 실패 뒤 남지 않도록,
빌드 실패 시 해당 `.run.log`에도 `NOT RUN`과 빌드 종료 코드를 기록한다.
가짜 cc 실패 검사를 재실행해 31개 로그가 모두 명시적으로 미실행인지 확인했다.
핵심 실행 출력과 최초 terminal 실패/진단/수정 후 결과는
[증거 원문](ASYNC_SANITIZER_EVIDENCE_2026-10-16.txt)에 함께 보존한다.

## 최종 완료 결과

- `make -j2 check`: 전체 회귀 PASS, 종료 코드 0, wall **9분 33.10초**.
  네 전용 검사와 native/PTY/media/연결 세션/경계/일괄 작업을 포함한다.
  전체 실행의 mark_discovery는 수정 전에 이미 통과했으며, 수정 후 동일 검사를
  일반 바이너리로 별도 재실행하여 통과했다. 제품 및 Makefile 변경은 없다.
- 최종 `run_terminal_sanitizers.py --disable-leaks`: terminal/input/probe/pty/
  auto_pty/vim_pty/editor_shutdown_pty/mark_discovery_pty/reads_pty **9/9 PASS**,
  종료 코드 0. 최종 로그 `/tmp/tfile-terminal-sanitizers-final.log`와
  `/tmp/tfile-terminal-sanitizers-final/*.log`. 최초 실패와 구분한다.
- native 기본 31/31 빌드 성공·31/31 ptrace 실패, fallback 31/31 PASS,
  기존 media fallback 5/5 PASS. **부모 LSan 및 두 worker-body LSan 미완료**.
- 문서 링크, Python 구문, `git diff --check` 통과. 로컬 커밋만 생성하며
  원격 push/CI 실행 및 실제 WSL 터미널 LSan 결과는 이번 검증에 포함하지 않는다.
