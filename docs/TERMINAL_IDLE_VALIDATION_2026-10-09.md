# 터미널 PTY idle 동기화 검증 — 2026-10-09

사용자 제공 `/tmp/tfile-terminal-sanitizers-manual-lsan/pty.log`는 terminal_pty.py의 idle 출력 assertion 실패이며 누수/ptrace 진단이 없다. 사용자 보고의 기본 25·미디어 5·Vim/마킹 PTY LSan 통과는 사용자 실행 결과이고 이번 직접 재실행 결과와 구분한다. 해당 수동 실행의 정확한 소스 커밋은 제공되지 않았다.

## 원인과 재현 범위

최신 제품 코드 `0c63415`와 기존 idle 논리를 비교했다. 원래 순서의 일반/ASan·UBSan 실행에서는 자연 발생 실패를 재현하지 못했다. 실패할 때까지 반복해 통과를 고른 것이 아니라 일반/계측/제어 조건을 구분해 각각 실행했다. 기존 논리는 Esc 후 `send()`의 0.15초와 idle의 0.25초 고정 read가 복원 완료를 보장한다고 가정했다. `Options in text`도 넓은 화면 메인 헤더의 Options 안내에 일치하므로 팝업 관찰이 아니었다.

부모 tfile만 SIGSTOP으로 정지해 Esc 처리·복원을 idle baseline 뒤로 미루고 SIGCONT로 진행시켰다. 수정 전 논리는 일반/ASan·UBSan 모두 동일 assertion으로 실패했다. 이것은 제어된 지연 재현이며 사용자 원래 실행의 스케줄링·추가 바이트까지 동일했다고 주장하지 않는다. 기존 로그에는 전후 카운터/원시 출력이 없어 그 이벤트 자체를 완전히 확정할 수 없다.

| 제어 재현, 수정 전 | 전 `(bytes, images, clears)` | 후 | 추가 내용 |
| --- | --- | --- | --- |
| 일반 및 ASan/UBSan 50×9 | `(34400, 2, 5)` | `(37177, 2, 5)` | 2777바이트, 메인 텍스트/입력 모드 복원 |
| 일반 및 ASan/UBSan 100×24 | `(70773, 3, 7)` | `(78273, 4, 7)` | 7500바이트, 메인 텍스트와 6×6 Sixel 한 번 복원 |

추가 출력은 `ESC[?1h ESC= … ESC[H …`의 ncurses 입력 모드·커서 이동·색/문자 출력이고, 이미지 화면에서는 `ESC P…q"1;1;6;6…ESC\` Sixel 프레임을 포함한다. 마지막은 `ESC[?1l ESC>`의 raw 입력 대기 복귀다. 전체 추가 바이트의 repr는 `/tmp/tfile-terminal-idle-{regular,sanitized}-controlled-before.log`(50×9), `/tmp/tfile-terminal-idle-{regular,sanitized}-image-before.log`(100×24)의 IDLE_MEASURE에 보존했다. 추가 DA `ESC[c`와 cell 조회 `ESC[16t`는 모두 0회, converter 재실행은 없었다. 전체 지우기는 모달 진입 때 이미 발생했고 위 복원 구간에서는 추가되지 않았다. 반복 redraw/진단 타이머 출력은 관찰되지 않았다.

## 수정

제품 코드는 변경하지 않았다. SetupScreen이 실제 터미널 application-cursor 모드를 관찰하고, 팝업 제목을 해당 창 좌표에서 확인한다. F7 전 텍스트 셀과 현재 이미지 위치/크기를 보존하고, Esc 뒤 팝업 소멸·원래 텍스트/이미지 복원·완성된 출력 프레임·raw 입력 대기 모드를 bounded predicate wait로 관찰한다. 고정 settling read를 제거하고 정상 복원 뒤에만 1.2초 idle 측정을 시작한다. 관찰 deadline 실패는 그대로 실패하며 재시도/긴 sleep으로 숨기지 않는다.

회귀에는 정상 닫기와 SIGSTOP으로 send()의 읽기 구간 내내 정지한 지연 닫기를 둘 다 포함한다. 정지 확인 후 Esc를 전송하고 복원 관찰을 시작할 때 재개한다. idle의 바이트/이미지/전체 지우기 완전 동일 assertion과 CPU ticks ≤2 assertion을 유지한다. 지속 출력이 있으면 counters와 실제 추가 출력 repr를 실패 로그에 남긴다. `TFILE_TERMINAL_IDLE_TRACE=1`은 성공한 복원/idle 구간도 계측한다.

| 수정 후, 일반 및 ASan/UBSan | 정상/지연 닫기 복원 | 이후 1.2초 idle 변화 |
| --- | --- | --- |
| 50×9 | 2777바이트, 이미지 0·지우기 0 | 바이트 0·이미지 0·지우기 0·CPU ticks 0 |
| 100×24 | 7500바이트, 이미지 1·지우기 0 | 바이트 0·이미지 0·지우기 0·CPU ticks 0 |

계측 로그 `/tmp/tfile-terminal-idle-regular-after.log`, `/tmp/tfile-terminal-idle-sanitized-after.log`. 임시 PNG와 fixture converter, 동일 화면 크기/입력 순서로 비교했다. 실제 그래픽 픽셀·육안 깜빡임을 증명하는 결과는 아니다. 테스트가 모방하는 xterm 입력 모드에서의 관찰이며 모든 터미널/스케줄링 상태에서 출력이 없다는 보장은 아니다.

## 검증과 수동 LSan

최종 관련 검사: `make check-terminal` PASS(exit 0), `/tmp/tfile-terminal-idle-check.log`(native terminal/input/probe 및 terminal_pty/terminal_auto_pty). `run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-idle-sync --disable-leaks`의 5개 PASS(exit 0), `/tmp/tfile-terminal-idle-sanitizers.log`. 일반/ASan·UBSan 계측 시나리오 전체도 각각 PASS(위 after 로그). 제품 코드 전체 make check는 `0c63415`에서 PASS이며 이 테스트 동기화 수정 전의 결과다. 동기화 수정 후에는 변경 영역 전체인 check-terminal과 terminal sanitizer 5개를 실행했다. architecture/Python AST/diff 검사 PASS. 후속 작업 트리는 이번 코드/문서만 포함하며 원격 푸시는 하지 않는다. 이번 자동 sanitizer는 detect_leaks=0으로 실행하며 LSan 통과로 기록하지 않는다. 알려진 ptrace 제한을 우회하거나 같은 LSan 실패를 반복하지 않았다. 일반 사용자 WSL/Linux 셸에서 최신 체크아웃으로 아래 명령을 한 번 실행하고 각 로그를 확인한다.

```sh
python3 tests/isolated_check.py env TFILE_TERMINAL_IDLE_TRACE=1 python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-idle-sync-manual-lsan
```

이 명령은 기본으로 LSan을 활성화하고 terminal/input/probe/pty/auto_pty 5개를 실행한다. 실패 시 해당 출력 디렉터리의 pty.log IDLE_MEASURE/RESTORE_MEASURE와 traceback을 그대로 보고한다. Python assertion 실패와 sanitizer 런타임/누수 진단을 분리한다.
