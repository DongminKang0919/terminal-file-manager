# 시작 디렉터리 응답성 검증 (2026-10-16)

기준은 `07dab0a957ae60ae28a63869c372596c09cb4cc4`이다. 작업 시작 시 사용자 변경은
없었다. [직전 UI I/O 기록](UI_IO_VALIDATION_2026-10-16.md)의 시작 디렉터리 후속
범위만 변경했다. 기존 탐색·검색·미리보기·변경 executor와 폼 조회는 재구현하지 않았다.

## 선행 CI 확인

[기준 커밋 Actions 실행](https://github.com/DongminKang0919/terminal-file-manager/actions/runs/38854993904)은
처음 조회할 때 진행 중이었고, 이후 **regression / sanitizer 모두 success**를 확인했다.
GCC/Clang clean-source와 기본 누수 검사를 포함한다. 조회 JSON은 증거 파일에 보존한다.
이는 기준 커밋의 원격 결과이며 이번 로컬 수정의 원격 CI 결과는 아니다. 이번 변경은
푸시하지 않으므로 새 원격 CI는 미실행이다.

## 원인과 수정 전 재현

기존 main은 terminal_session_check 뒤 `ui_init`를 호출하고, 그 성공 후 initscr를
호출했다. ui_init → initialize_panel → app_init_settings → app_navigate의
load_directory가 UI에서 core_resolve_directory → platform_resolve(realpath),
검증용 opendir, 목록용 opendir/readdir 및 항목 metadata를 실행했다.
저장 설정이 dual이면 ui_set_mode가 같은 경로로 두 번째 목록을 읽었다.
기본 경로는 `.`이며 UI에서 별도 getcwd를 호출하지 않는다. 상대 경로/현재 디렉터리의
절대 경로 결정은 realpath 안에서 수행되므로 이것도 막히는 경로에 포함된다.

`tests/ui_io_gate.c`의 platform_resolve, platform_directory_open,
platform_directory_next wrapper에서 FIFO reader를 먼저 등록한 후 PID/phase marker를
공개한다. controller는 marker를 확인한 뒤 입력/resize를 보내고 명시적으로 release한다.
목록 읽기 상태를 고정 sleep으로 추측하지 않는다. 수정 전 기준 트리에 현재 gate만
링크해 별도 `/tmp/tfile-startup-before`를 만들었다. 일반 앱/테스트 바이너리를 덮어쓰지 않았다.
resolve는 같은 번역 단위의 core 함수가 인라인될 수 있어 platform 경계를 gate한다.

수정 전 22개 동일 조건(경로 해석/열기/항목 읽기 × Esc/F10/resize × 두 크기,
기본 현재 경로, 초기 두 번째 패널)에서 gate PID가 UI PID였고 **202.1–202.7ms 이상**
첫 화면과 입력 처리가 막혔다. 이것은 최소 관측 정지 시간이며 syscall의 실제 지연
분포가 아니다. gate를 해제하지 않으면 읽기는 끝나지 않는다. 초기 terminal echo는
앱의 첫 화면으로 세지 않았다. 오류 문구는 missing=`No such file or directory`,
file=`Not a directory`, denied=`Permission denied`, 종료 코드 1이었다.

모든 fixture는 임시 HOME/XDG config/data/cache, 별도 cwd와 파일만 사용했다.
일반 실행·합성 지연·sanitizer 실행의 측정값은 서로 합치지 않는다.

## 시작 상태와 수명 정책

1. 기존 인자 수 검사와 대화형 terminal_session_check를 먼저 수행한다. 실패하면
   curses에 진입하지 않고 기존 메시지/종료 코드 1을 유지한다.
2. ncurses·입력·종료 signal handler를 초기화하고 ui_startup을 호출한다.
   설정/즐겨찾기와 기본 옵션을 복원한 뒤 ui_enable_async_reads를 적용한다.
   기존 ui_init 동기 API는 native 호출자에 유지한다.
3. 기존 initialize_panel/app_navigate/load_directory를 그대로 사용하되
   load_progress가 연결되어 있으므로 **기존 read lane**이 경로 해석부터 목록까지 수행한다.
   ncurses는 부모 callback에서만 호출한다. 별도 startup worker/IPC는 만들지 않았다.
4. 시작 전체에 100ms 표시 기준을 적용한다. 그 전에 끝나면 별도 로딩창 없이 최종
   목록을 그린다. 오래 걸리면 대상 입력 경로(기본 `.`), Left/Right 패널,
   받은 항목 수, Esc/F10 안내를 stdscr에 표시한다. 좁은 화면의 경로는 기존 안전한
   텍스트 렌더러로 줄이고, 작은 화면에서도 창 생성 실패 때문에 취소하지 않는다.
5. dual은 왼쪽 성공 후 같은 read lane의 retirement를 회수하고 오른쪽을 시작한다.
   오른쪽은 왼쪽이 확정한 절대 경로를 사용한다. 한 번에 read worker 하나다.
   두 패널을 모두 준비하기 전 일반 draw/preview/명령 루프에 진입하지 않는다.
   어느 쪽이든 실패하면 전체 시작을 종료한다. 왼쪽 성공을 단일 패널로 조용히 대체하지 않는다.
   실행 중 최초 dual 전환도 기존 ui_set_mode/initialize_panel 경로를 계속 사용한다.
6. 목록/경로/방문 기록은 기존 app_navigate의 성공 commit만 사용한다. 기본 선택은
   정렬된 첫 항목, focus는 Files/왼쪽, hidden/sort/mode/image 설정은 기존대로 복원한다.
   preview는 시작 완료 뒤 기존 async preview 경로에서 시작한다.

시작 중 Esc/F10/q는 **앱 종료(0)**다. 이전 목록이 없으므로 빈 작업 화면에 남지 않는다.
SIGINT/SIGTERM/SIGHUP도 shutdown 요청으로 같은 정리 경로와 코드 0을 사용한다.
resize는 취소하지 않고 현재 크기에 다시 그리며, 최소 크기 미만에서 복원도 가능하다.
다른 키와 마우스 작업은 폐기한다. 완료 시에도 queued 종료 키를 확인한 뒤 나머지
입력을 flush하여 생성/복사/삭제/탐색 키가 뒤늦게 실행되지 않게 한다.

취소는 기존 core_read_cancel/close와 비차단 retirement를 사용한다. 종료에서는
core_read_shutdown의 기존 제한된 회수를 사용한다. 실제 D-state I/O가 끝나지 않으면
SIGKILL 뒤에도 자식 소멸을 즉시 보장할 수 없다. 부모는 무한 wait하지 않고
parent-death 보호를 유지한다. 정상 gate에서의 회수 성공을 D-state 보장으로 확대하지 않는다.

실패는 endwin 후 ui_free로 상태와 worker를 정리하고 stderr에 기존 detail을 출력한다.
대체 디렉터리를 고르지 않는다. PTY는 termios 전체 일치, cursor show와 mouse disable
sequence를 확인한다. 오류 상세는 alternate screen 복원 후에도 stderr에 남는다.

## 검사 연결과 범위

`make check-startup`은 기존 startup_ui_test와 새 startup_latency_pty.py를 실행하며
check-isolated를 통해 전체 회귀/CI에 연결된다. native 검사는 한 번의 worker read,
한 번의 부모 정렬, raw-name tie와 기존 picker 순서를 유지한다. 정상 시작/오류와
20회 같은 프로세스 내 취소 후 FD 수 및 waitpid(ECHILD)를 확인한다.
성능 측정용 performance.c도 startup input poll에서는 종료하지 않게 조정하여
이전처럼 실제 시작 완료를 측정한다.

새 PTY는 총 **59개 보고 사례**다(41 gate, 11 일반 정상, 6 경로 오류,
비대화형/인자 오류 한 묶음). 50×9/100×24, 35×5→복원, 기본 cwd/명시/상대 경로,
세 화면 모드, 두 번째 패널 실패·취소·복원, 초기 파일 preview, hidden 설정,
로딩 중 삭제, 세 종료 signal, 연속 작업 키 폐기, 반복 10회 취소를 포함한다.
held 상태에서 child PID와 부모의 단일 자식도 확인한다. 파일 payload는 종료 후 대조한다.

| 경로 | 일반 회귀 | ASan/UBSan | 부모 LSan | worker 본문 LSan |
| --- | --- | --- | --- | --- |
| 시작 resolve/list/dual | native + 59 PTY | native runner와 terminal runner의 startup_pty | 정상 부모 종료에서 owned 상태/IPC 자원; 로컬 미완료 | read worker는 `_exit`하므로 전용 body 누수 검사가 아님 |
| 기존 preview/mutation/media | 전체 기존 회귀 유지 | 기존 native body harness 유지 | 별도 기본 실행 필요 | 기존 정상 반환 body harness는 native runner에 유지; 로컬 미완료 |

native sanitizer의 기존 startup_ui_test linker wrapper를 Makefile과 함께 갱신했다.
terminal runner는 기존 별도 gated binary에 resolve/next wrapper를 추가하고 startup_pty를
필수 검사로 실행한다. CI의 기존 terminal sanitizer 단계 이름에 startup을 명시했다.
기존 `*.log`와 summary.json artifact가 startup_pty.log 및 build/exit/PASS/FAIL을 포함한다.
기본 ASan/UBSan/LSan과 명시적 --disable-leaks 대체 실행, 실패 전파는 유지한다.
보안 설정·job timeout·병렬도를 바꾸거나 suppression을 추가하지 않았다.

## 측정과 실행 결과

PTY write 시각을 입력 전송으로 기록한다. 내부 입력 접수의 정확한 시각은 계측하지
않았다. Esc/F10/신호는 종료 관측이 입력 반영의 상한이며 따로 가짜 ack 시간을 만들지
않는다. resize-frame은 gate를 해제하기 전의 화면 반영이며, control-to-exit은 이후
명시적 release·정상 시작·q까지 포함한다. worker-gone은 `/proc/PID` 소멸 관측이다.
취소된 worker가 느린 syscall을 성공 완료했다는 뜻이 아니다.

일반 로컬 기준 실행은 50×9/100×24의 단일/preview/dual와 기본/상대 경로에서
첫 목록까지 **2.1–5.9ms**였다. worker 전환에는 fork/IPC와 10ms polling 지연이 추가된다.
일반 실행은 별도 로딩 frame이 나오지 않았음을 raw output으로 확인했다.
수정 후 일반 실행은 **12.5–25.4ms**였다(동시 검사 부하 포함). 순수 디스크 처리량 향상으로
해석하지 않는다. 아래 값은 같은 gate harness의 최종 별도 실행이다.

| 관측 | 수정 전 | 수정 후 |
| --- | --- | --- |
| held-call 첫 로딩 frame | 해제 전 없음(22개 모두 최소 202.1–202.7ms 정지) | 102.8–109.9ms (전체 41 gate) |
| held-call Esc 전송→프로세스 종료 | 해제 전 미반영 | 35.5–37.3ms |
| held-call F10 전송→프로세스 종료 | 해제 전 미반영 | 10.4–10.7ms |
| held-call resize 화면 반영 | 해제 전 미반영 | 0.5–1.6ms; gate를 계속 유지한 상태 |
| SIGINT/SIGTERM/SIGHUP→종료 | 수정 전 별도 미측정 | 10.4–10.5ms |

resize/복구의 최종 종료는 작업 키 폐기 확인과 명시적 release/q를 포함해 최대 74.0ms였고,
이를 취소 ack 시간으로 보고하지 않는다. 단위는 ms이며 PTY 관측/해상도 및 부하의 영향을 받는다.

첫 전체 시도는 추가한 35×5 검사에서 실패했다. PTY Screen 모델을 같은 크기로
resize할 때도 비워서, ncurses가 다시 그릴 필요가 없는 기존 frame을 잃은 검사 오류였다.
크기가 변할 때만 모델을 바꾸고 기존 화면·복원 assertion을 유지했다. 다른 중간
실패도 부분 frame에서 전체 목록을 기대하거나 50열 dual에서 비활성 패널이 동시에
보일 것을 기대한 oracle 오류였다. 최종 frame/active footer 사건을 기다리도록 고쳤다.
제품 assertion 제거나 timeout 증가는 하지 않았다.

## 남은 동기 경로

- terminal_session_check/initscr/termios/terminfo 초기화 자체의 terminal I/O.
- 설정/즐겨찾기의 경로 구성과 안전한 FD 기반 config 읽기. ncurses 초기화 뒤지만
  이 호출이 느리면 아직 startup callback에 도달하지 못한다. 이번에 gate 개선을
  재현한 디렉터리 I/O와 다르며 시작 전체가 비동기라고 주장하지 않는다.
- 수신 목록의 메모리 할당·부모 정렬·방문 기록 commit. 아주 큰 목록에서는 CPU
  시간이 입력을 지연시킬 수 있다. 디렉터리 metadata I/O는 read worker에서 수행한다.
- fork/IPC 진단 memfd/socket 생성과 로컬 시스템 자원 접근. 기존 미디어 임시 파일,
  설정 저장, Vim 검증의 한계는 직전 기록과 같다.

실제 느린 네트워크 mount, D-state, 물리 터미널/Sixel 화면, ptrace 없는 WSL의
최신 수정 LSan은 미실행이다.

## 수동 점검

실제 WSL 터미널에서 다음 명령은 HOME/XDG를 격리한다.

```sh
make -j2 all
make check-startup
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-startup-wsl
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-startup-wsl
# ptrace 환경의 명시적인 ASan/UBSan 대체 결과(누수 검사 아님)
python3 tests/run_sanitizers.py --disable-leaks --output-directory /tmp/tfile-sanitizers-startup-fallback-wsl
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --disable-leaks --output-directory /tmp/tfile-terminal-sanitizers-startup-fallback-wsl
```

짧은 실제 화면 점검은 아래처럼 동일한 격리 HOME과 fixture로 두 번 실행한다.

```sh
python3 tests/isolated_check.py sh -c 'mkdir -p "$HOME/start"; printf "startup fixture\n" > "$HOME/start/a.txt"; ./tfile "$HOME/start"; ./tfile "$HOME/start"'
```

첫 실행의 선택/preview를 확인하고 F7에서 dual을 저장한 뒤 종료한다. 두 번째 시작의 왼쪽 focus와
Tab 전환을 확인한다. 작은 창으로 시작해 확대하고, 느린 테스트 mount의 임시 폴더에서
로딩 경로와 Esc/F10 및 종료 후 커서·echo·마우스가 돌아오는지 확인한다.
잘못된 경로는 종료 코드 1과 복원 뒤 오류가 남는지 확인한다. 사용자 설정/파일을
변경하는 방식으로 지연을 재현하지 않는다.

## 최종 실행 기록

| 검사 | 실제 결과 | 시간 / 로그 |
| --- | --- | --- |
| 직전 커밋 원격 CI | regression / sanitizer success | 위 Actions 링크; 이번 수정의 결과 아님 |
| `make -j2 check` | 전체 PASS, exit 0; 새 59개 포함 | 628.86초; `/tmp/tfile-startup-full-final.log`와 `.time` |
| `make check-startup` | native + PTY 59개 PASS, exit 0 | 6.05초; `/tmp/tfile-startup-target-final.log`와 `.time` |
| native ASan/UBSan `--disable-leaks` | 33/33 PASS, exit 0 | wall 96.70초 / runner 91.162초; `/tmp/tfile-sanitizers-startup/` |
| terminal ASan/UBSan `--disable-leaks` | 13/13 PASS, startup PTY 59개 포함, exit 0 | wall 301.07초 / runner 284.310초; `/tmp/tfile-startup-terminal-san-final/` |
| 기본 startup native LSan | exit 1, ptrace fatal; **미완료** | `/tmp/tfile-startup-lsan.log` |
| 성능 harness 3회 | 실행 완료, exit 0; 시작이 취소로 대체되지 않음 | build 9.098초 / measurement 2.225초; `/tmp/tfile-startup-performance.json` |
| 문서 링크 / architecture / Python 구문 / diff | PASS | 별도 정적 검사 |

성능 harness의 worker 전환 뒤 부모 directory_opens=0, main sorts=1, FD=6이
정상이다. child 호출 횟수나 실제 PTY 표시 시간으로 해석하지 않는다.
최대 단일 프로세스 RSS는 전체 회귀 155488 KiB, native sanitizer 144884 KiB, terminal sanitizer
72560 KiB였다. 동시에 실행된 프로세스 합산 메모리 peak는 아니다.

기본 LSan은 `LeakSanitizer does not work under ptrace`를 출력했다. 부모 LSan과
기존 worker-body LSan의 최신 로컬 완료를 주장하지 않는다. startup read body 전용
정상 반환 harness도 없으므로 `_exit` worker 내부 전체 누수 통과를 보고하지 않는다.
terminal runner의 기본 누수 실행 전체, ptrace 없는 WSL 최신 재검증, 이번 수정의
원격 CI, 로컬 Clang clean-source(compiler 부재)는 미실행이다. 명시적 대체 명령으로 기본 CI 설정을 바꾸지 않았다.

수정 전 로그는 `/tmp/tfile-startup-before.log`(22 gate),
`/tmp/tfile-startup-normal-before.log`(10 일반)이다. 첫 전체 실패 기록은
`/tmp/tfile-startup-full.log`와 `.time`(150.00초, exit 2)이며 위 검사 모델 수정으로
해결했다. startup_ui_test의 `No such file or directory` 한 줄은 의도한 missing
경로 assertion의 stderr 출력이며 정상 검사 종료 코드는 0이다.

[기준 CI·전후 gate·최종 회귀·sanitizer 핵심 출력](STARTUP_EVIDENCE_2026-10-16.txt)을 보존했다.
검증된 소스와 문서를 로컬 커밋으로 남기며 원격 푸시는 하지 않는다.
