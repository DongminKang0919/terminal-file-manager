# 미디어 종료 관측·회수 검사 (2026-10-16)

## 출발점과 판단

검토 기준은 `d08ea44`와 [직전 UI 검증 기록](UX_TARGETS_VALIDATION_2026-10-16.md)이다.
시작 시 작업 트리는 깨끗했고, 이후 제품의 미디어 종료 구현은 바뀌지 않았다.
`posix_media.c`와 기존 media_pty의 최신 관련 변경은 `07dab0a`이며
그 이후 HEAD까지 종료 구현의 수정은 없었다. 최신 코드에서 이미 해결됐다는 근거는 없다.
원격 [Linux checks 38863483342](https://github.com/DongminKang0919/terminal-file-manager/actions/runs/38863483342)는
동일한 `d08ea44b83ba8e763e7e2e30de73a53f6b3b963f`에 대해 regression·sanitizer 모두 success였다.
`gh run list`, job별 `gh run view --json`, 전체 `--log`로 실제 확인했다.
기존 media_pty, native worker body, 기본 LSan의 CI 실행 성공이다. **이번 미커밋 변경의 CI 결과는 아니다.**
최신 CI에는 실패 artifact가 없고, 과거의 무관한 실패 run을 이번 문제로 분류하지 않았다.

이전 `/tmp/tfile-ux-full-verified.log`가 남아 있었다. UI 종료 후 `media_pty.py:185`의
`assert not Path('/proc/PID').exists()`에서 한 번 실패했다. 당시 PID 값·상태·부모·종료 상태는
기록되지 않았다. `/tmp/tfile-ux-media-probe.log`의 기준/수정 바이너리 재실행은 모두 통과했다.
따라서 **그 한 번의 실패가 실제 살아 있는 프로세스 누수였는지는 소급 판정할 수 없다.**

새 결정적 검사에서는 테스트 프로세스만 Linux subreaper로 설정했다. worker와 converter를
함께 종료하면 worker는 UI가 회수하지만 converter는 테스트에 입양된다. 테스트가 잠시
회수를 보류하면 `/proc/PID`가 남은 `Z` 상태와 exit code 9를 확실히 관측할 수 있다.
이때 이전 assertion은 실패하지만 converter는 실행하지 않으며 FD도 갖지 않는다.
이는 기존 검사의 수명 계약 오류를 재현한 것이며, 과거 실패의 유일한 원인을 확정한 것은 아니다.
제품의 종료 순서를 추측으로 바꾸거나 앱에 subreaper를 추가하지 않았다.

## 실제 구현과 소유권

| 경로 | 현재 정책 |
| --- | --- |
| 생성 | UI → core_media_open → platform media_start. UI의 직접 자식인 준비 worker가 process-group leader. worker가 fork/exec한 converter와 일반 fork 자손은 같은 PGID |
| 소스/FD | 소스 open·서명·identity 검증·도구 탐색은 worker. converter에는 소스 FD와 stdin/stdout/stderr만 전달. UI는 captures/report FD와 private directory handle 수명만 소유 |
| 정상 완료 | worker가 converter를 waitpid, 소스 identity 재검증, 필요한 PDF 2단계 실행 후 결과 보고. UI가 worker를 waitpid하고 남은 그룹에 SIGKILL, 결과 capture를 읽음 |
| 선택 변경/Off/모드 변경 | preview_reset/media_reset → close: 그룹과 직접 worker에 SIGKILL. UI 루프에서 WNOHANG 회수. retiring 하나가 남으면 새 미디어 작업은 시작하지 않고 다음 tick에 재시도 |
| 팝업 | dialog_window 진입 시 진행 중 media_job reset, ncurses는 UI에만 존재. 닫힌 후 현재 선택을 새로 요청할 수 있음 |
| resize | 최소 크기 미만이면 취소. 유효 크기의 실제 변환 geometry 변경이면 취소하고 120ms 안정화 후 재요청. 완료된 안전한 작은 raster는 위치만 변경할 수 있음 |
| Vim | 바이너리/미디어 커서 자체는 Vim 검사에서 거절. 텍스트 커서로 이동한 뒤 preview_reset과 bounded pause, endwin 후 editor 실행. 복귀 시 입력/화면/preview 복원 |
| q/F10/종료 신호 | ui_free → preview_reset → read_shutdown → media_shutdown. SIGINT/SIGTERM/SIGHUP는 UI shutdown flag로 정상 unwind. media_pause는 최대 20회 × 1ms pause와 WNOHANG 회수, 무한 wait 없음 |
| 해제 | 직접 worker 회수 후 dispose가 captures/source FD를 닫고 private raster·디렉터리를 제거. 완료된 결과와 선택 일치 검증 및 세대 무효화는 변경하지 않음 |

`launch`의 converter와 `media_start` worker는 PDEATHSIG=SIGKILL과 부모 변경 검사를 사용한다.
close/timeout은 그룹과 worker를 즉시 SIGKILL한다. 협력적으로 파일 상태를 확정해야 하는
**파일 변경 worker의 취소 정책에는 손대지 않았다.**

UI는 자신의 직접 worker를 회수할 수 있다. 동시에 죽은 converter/그 자손은 PID 1 또는
기존 subreaper에 입양되며 UI가 waitpid할 자식이 아니다. 입양자가 회수하기 전의 좀비를
"UI가 회수 완료"라고 부르지 않는다. 새 검사에서는 테스트가 별도로 waitpid한 상태까지 기록한다.

## 회귀 검사와 경계 제어

`tests/media_lifecycle_pty.py`는 격리한 HOME/XDG·임시 PNG/텍스트·모의 magick/Vim만 사용한다.
mock은 FIFO gate가 열릴 때까지 변환을 멈추고, 단일 append write로 사건을 기록한다.
각 사건에는 monotonic 시각, PID/PPID/PGID/starttime/private directory가 있다.
임의 sleep으로 시작/완료를 추측하지 않는다.

- 변환 시작 직후 종료, 선택 변경, Off, 팝업, 최소 크기 resize, 텍스트 선택 후 Vim 진입,
  SIGTERM, SIGSTOP 상태 converter 취소.
- worker를 SIGSTOP한 뒤 converter만 gate 해제: converter가 실제 `Z`가 된 것을 관측해
  worker의 waitpid **직전**에서 앱 종료·팝업·resize·Vim과 겹친다.
- 정상 반환 결과가 UI에 Sixel protocol frame으로 도착한 **이후** 종료.
- SIGTERM을 무시하는 mock과 같은 PGID의 일반 fork 자손. 자손 시작은 pipe acknowledgement로 확정. 팝업 종료에는 SIGHUP도 보낸다.
- 자손 포함 반복 시작/취소 6회, 반복 구간 30초 상한. 총 21개 시나리오이며 실패를 재시도하지 않음.
- 원본 두 파일 불변, 살아 있는 worker 동안 private directory 존재, 종료 후 해당 작업의 directory 부재,
  worker `/proc` 부재, converter 실행 종료/입양 회수, 테스트 FD 수 및 최종 waitpid(ECHILD) 확인.
  팝업을 닫아 새 요청이 시작된 경우도 모든 PID를 추적한다.

`tests/process_probe.py`는 `/proc/PID/stat`에서 괄호를 포함할 수 있는 comm 필드를 분리하고
state/ppid/pgid/starttime/exit code를 읽는다. 조회 중 ENOENT/ESRCH는 정상 소멸 경쟁으로 취급한다.
권한 오류나 잘못된 stat 형식은 숨기지 않는다. starttime으로 PID 재사용을 구분한다.
`wait_stopped`는 최대 2초 동안 상태를 확인한다. Z는 실행 종료로만 인정하고 reaped=false로
출력한다. S/R/T/D 등 실행 가능하거나 아직 종료되지 않은 상태는 기한을 넘으면 반드시 실패한다.
이 기한은 기존 2초 PTY 종료/상태 기한을 따른 것으로 늘리지 않았다.

기존 media_pty의 PID 부재 검사도 위 관측으로 교체했다. timeout 검사에서 converter identity는
종료 전에 확보한다. 기존 원본/선택/응답성/protocol/임시 파일/CPU idle assertion은 보존했다.

`tests/media_lifecycle_fault.py`는 **그룹 SIGKILL만 무효화**하고 직접 PID kill은 유지하는
작은 LD_PRELOAD 변형을 임시 빌드한다. 이를 UI에만 적용하고 같은 `descendant-exit` 검사를
한 번 실행한다. converter의 PDEATHSIG로도 자손은 남는다. 새 검사가 입양된 **살아 있는 S**
자손을 기한 내 실패시키는지 확인한다. 테스트 감독/실패 정리에는 변형을 적용하지 않는다.
단순 살아 있는 자식도 `wait_stopped`가 거부하는 별도 negative control이 있다. 같은 unmodified binary를 `--legacy-absence`로 실행해
기존 assertion 자체가 실패하는 비교도 자동 실행한다.

## 자동화

Makefile의 기존 check-media 및 check-isolated media_pty 다음에 두 검사를 추가했다.
`run_media_sanitizers.py`의 기존 독립 tfile 빌드를 그대로 사용하며 7개 필수 suite가 실행된다.
build 로그, suite별 exit/시간, summary.log/json을 보존하고 누락·빌드 실패 시 INCOMPLETE가 남는다.
GitHub step summary에 검사별 PASS/FAIL을 표시한다. CI의 기존 *.log artifact에 새 로그가 포함되며
media summary.json도 artifact에 추가했다. CI의 기본 LSan, 보안, timeout, 병렬도는 유지한다.
정상 실행 파일·테스트 바이너리를 sanitizer로 덮어쓰지 않는다.

## 실행 결과

| 검사 | 실제 결과 | 로그·시간 |
| --- | --- | --- |
| 최신 원격 d08ea44 | regression/sanitizer success | 위 run 링크, `/tmp/tfile-media-latest-ci.log` |
| 기존 media_pty 단독 + 초기 gate/negative | PASS | `/tmp/tfile-media-targeted.log`, 23.43초 |
| 최종 gate 21 + 그룹 kill 변형 | PASS; 고의 변형의 내부 검사는 예상대로 exit 1 | `/tmp/tfile-media-lifetime-targeted-final.log`, 7.17초 |
| 기존 미디어 ASan/UBSan runner | 7/7 PASS, 명시적 `--disable-leaks`; LSan 미완료 | `/tmp/tfile-media-lifetime-san-verified/`, wall 62.44초, runner 59.853초 |
| 최종 비교·gate·변형 ASan/UBSan 재확인 | PASS; 원래 PID 부재 assertion 실패와 살아 있는 자손 검출 확인 | `/tmp/tfile-media-san-final-cases.log`, 8.07초 |
| 최신 소스에서 별도 빌드한 media_worker_test ASan/UBSan | PASS; 정상 반환 body 20회 반복 및 PDF/도구/접근 실패 | `/tmp/tfile-media-body-final/asan-ubsan-no-lsan.log`, 0.288초 |
| 부모 LSan | 미완료, exit 1; ptrace fatal | `/tmp/tfile-media-parent-lsan.log` |
| 정상 반환 worker-body LSan | 미완료, exit 1; ptrace fatal | `/tmp/tfile-media-body-final/lsan.log`, 0.288초 |
| 최종 전체 `make -j2 check` | PASS, exit 0; 미디어·터미널·실제 Vim·종료·전체 UI 포함 | wall 597.79초(9분57.79초), 최대 단일 RSS 155496 KiB; `/tmp/tfile-media-full-final.log`, `.time` |
| 문서·architecture·Python 구문·diff | PASS | 관련 정적 검사 |

일반 빌드는 실제 UI/worker/process group 수명을 검사한다. ASan/UBSan PTY는 부모 메모리 오류와
실제 프로세스 정리를 검사하지만 SIGKILL/_exit로 끝난 worker의 전체 내부 누수를 증명하지 않는다.
정상 반환 `media_worker_test`는 본문 자원 정리를 보완한다. 현재 환경의 부모/본문 LSan을
과거 CI의 기본 LSan 성공과 섞지 않는다. 실제 Sixel 픽셀·잔상은 이번 자동 검사 범위 밖이다.
기존 native media_test의 정상 완료/50회 취소 FD·직접 자식·임시 경로 검사도 보존하고 실행했다.

전체 회귀에서 새 gate 21개와 legacy assertion 실패/그룹 kill 누락 검출을 실제 실행한 PASS를 확인했다.
반복은 각 gate suite마다 정해진 6회만 수행했으며, 실패한 전체/초기 harness 실행은 위 성공에 포함하지 않는다.
최종 전체 회귀 로그에는 SKIP이나 컴파일 warning이 없었다.

sanitizer 빌드 최대 단일 프로세스 RSS는 81816 KiB였다(동시 합산 peak 아님).
새 gate suite는 약4.8초, 변형은 약2.4초였으며 최종 legacy 비교 추가 후 두 suite 재실행은
8.07초였다. CI timeout/job/병렬도 조정이 필요하다는 근거는 없어 유지했다.

개발 중 검사 자체의 오류도 기록한다. 처음 새 harness는 팝업을 닫은 뒤 재시작한 converter를
추적하지 않아 최종 ECHILD 검사에서 실패했다. 모든 시작 사건을 수집하도록 고쳤다.
첫 전체 회귀는 191.89초에 새 harness의 입양 전 waitpid(ECHILD) 가정으로 실패했다.
로그에는 자손 Z/PPID=converter, converter R/PPID=test와 exit 9가 동시에 남았다.
입양·소멸 상태를 WNOHANG과 2초 기한으로 기다리도록 고쳤고 실패를 단순 무시하지 않았다.
새 변형 검사 전후의 고의 실패는 PASS를 확인하기 위한 negative control이며 예상치 못한 실패와 구분한다.
초기 fault launcher의 `env`가 mock-only PATH에서 발견되지 않는 오류도 `/usr/bin/env`로 수정했다.
이 실패들을 제품 누수나 최종 통과로 집계하지 않는다. 제품 C 코드는 변경하지 않았다.

[핵심 실행·입양 상태·고의 실패와 CI 근거](MEDIA_LIFETIME_EVIDENCE_2026-10-16.txt)를 저장했다.

## 남는 한계와 수동 확인

실제 커널 D 상태 I/O를 합성 FIFO/SIGSTOP으로 재현했다고 주장하지 않는다. SIGKILL도 D 상태가
풀리기 전에는 실제 종료를 완료하지 못할 수 있다. 최대 약20ms의 직접 worker 회수 기간 뒤에도
worker가 남으면(일반 스케줄링 지연도 이론상 가능) UI 종료/편집기 handoff는 계속 진행한다. 준비 worker는 터미널 FD를 소유하지 않는다.
그때 host가 자식을 입양하며 private directory가 남을 수 있다. 직접 worker를 회수한 사실만으로
모든 손자 프로세스의 실제 종료 시점까지 증명할 수는 없다. dispose는 worker 회수를 기준으로 하므로
중단 불가능한 자손 I/O까지 임시 경로 수명이 보장된다는 주장도 하지 않는다.

mock은 일반 fork로 같은 process group에 남는 실제 지원 구조를 검사한다. setsid/setpgid로
그룹을 탈출하는 도구, 외부 daemon에 작업 위임, UI SIGKILL, 적대적 converter의 임의 파일 변경은
이번 종료 계약/검증 밖이다. host PID 1/subreaper가 좀비를 회수하지 않는 환경은 별도의 host 문제다.
테스트용 subreaper가 회수한 것을 제품이 자손 회수를 보장한 것으로 보고하지 않는다.

실제 터미널에서 이미지 변환 중 q/F10, Off, 49×8→100×24 resize, 텍스트 선택→e→Vim 복귀를
확인한다. PDF도 반복하고 terminal mode·커서·마우스와 임시 경로를 확인한다. 자동 PTY는 Sixel
protocol 순서만 검증하며 실제 픽셀·잔상·폰트는 수동 미확인이다.

```sh
make -j2 tfile tests/media_test
python3 tests/isolated_check.py python3 tests/media_pty.py
python3 tests/media_lifecycle_pty.py
python3 tests/media_lifecycle_fault.py
python3 tests/isolated_check.py make -j2 check-media
# ptrace가 없는 실제 WSL 터미널에서, 동일 runner의 기본 누수 검사
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-lifetime-wsl
# 정상 반환 worker-body LSan은 별도 native harness로 실행
python3 tests/isolated_check.py python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-media-lifetime-wsl
```
