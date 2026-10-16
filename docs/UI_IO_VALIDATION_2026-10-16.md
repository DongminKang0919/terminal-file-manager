# UI의 남은 파일시스템 대기 분리 (2026-10-16)

시작 커밋은 `3e0ae70`이며 사용자 변경은 없었다. 직전 sanitizer 자동화는 완료됐고,
부모/worker-body LSan만 ptrace 제한으로 미완료였다. 이번 작업은 기존 검색·목록·
미리보기 본문·변경 executor를 재구현하지 않고 그 요청 종류와 media 수명만 확장한다.
앱 시작 시 최초 디렉터리 읽기는 변경하지 않는다.

## 조사: 호출 시점·캐시·실패

| 영역 | 수정 전 UI 호출과 실제 접근 | 기존 캐시와 실패 처리 | 결정 |
| --- | --- | --- | --- |
| 단독/일괄 복사·이동 폼 | transfer.c의 ui_transfer_context(dual 폼 진입), transfer_entry의 Browse/Paths/Run, batch.c의 Next/Browse → core_resolve_directory → platform_resolve(realpath), platform_directory_open(opendir) | peer 목록 snapshot은 실제 접근 가능성 보증이 아님. 폼 재시도마다 검증, 실패 시 warning과 입력 유지 | dual 진입의 사전 조회 제거; 사용자가 요청한 검증만 기존 ReadJob의 resolve-only 요청으로 처리 |
| Browse | pick_path 초기/Parent/Open → core_list → directory open/readdir/entry metadata. 경로 입력·하위 디렉터리 진입에서 resolve도 동기 | 한 목록을 보유하며 reload 때 버림. 실패·삭제·권한 거부는 빈 디렉터리와 구별 | 기존 directory ReadJob 사용. 로딩 중 Use/Open 차단, Parent/path/취소 가능 |
| 생성 | create_named_entry → core_create → platform_create → mkdir 또는 open(O_CREAT\|O_EXCL), close | 캐시 없음. 이름 검사는 메모리 검사. 생성 후 기존 refresh·notice 경로 | 같은 MutationJob에 create file/directory 종류 추가; FD 기준 parent 순회와 mkdirat/openat 사용 |
| 미디어 원본/변환 | media_prepare → core_media_open → platform_media_open → source open(O_NOFOLLOW\|O_NONBLOCK), fstat, pread; 완료 poll에서 lstat/fstat 재검증 | 선택 파일·geometry·fallback의 결과 캐시, refresh/reset 시 폐기. converter 자체만 이미 별도 프로세스 | 기존 media 슬롯 안의 준비 프로세스가 원본 준비·변환 launch/poll·최종 재검증을 소유 |
| 최초 도구 탐색 | platform_media_open/PDF 두 번째 단계와 image_setup/미확인 terminal의 media_prepare → find_tool → PATH의 access/stat | executable 경로의 영구 캐시는 없음. 설정창 진입마다 재검사, 실제 coder 지원은 변환 결과로 확인 | 같은 media 준비 프로세스의 tools-only 요청. 설정창은 Checking을 표시하며 취소·종료·resize 처리 |

동기라는 이유로 모든 함수를 바꾸지 않았다. 경로 문자열 join/parent/name, UTF-8 편집,
이름 유효성 검사는 파일시스템에 접근하지 않으므로 유지했다. 단독/일괄 폼은 입력의
매 글자마다 실제 디렉터리를 조회하지 않는다. native/비터미널의 동기 core API도
유지하여 기존 호출자와 native 회귀를 보존했다. 일반 로컬 실행 결과는 아래와 별도 로그에 기록한다.

## 수정 전 재현과 관측 기준

`tests/ui_io_gate.c`는 문제 호출에 진입한 **PID와 phase**를 marker에 기록하고,
FIFO의 명시적인 release를 기다린다. 제품 바이너리에는 포함하지 않는다.
목적지 resolve, Browse directory open, 생성 호출 전/후, media source open,
도구 executable access의 여섯 경계를 각 50×9/100×24에서 Esc/F10/resize로 검사했다.
수정 전 36개 모두 gate PID가 UI PID였다. marker 확인 후 입력을 보냈지만
200ms 관측 창 동안 UI가 호출을 벗어나지 못했고 explicit release가 필요했다.
이 200ms는 syscall 소요 시간 추정이나 고정 sleep 기반 준비 가정이 아니라,
진입 사건을 확인한 뒤 **해제하지 않은 gate**를 관측한 최소 정지 구간이다.

입력 전송 시각은 PTY write 시각이다. 내부 입력 접수 시각을 직접 계측했다고
주장하지 않는다. 취소 문구/폼 복원/resize frame 또는 프로세스 종료는 그 전에
입력이 처리됐다는 관측 증거다. worker 회수 시각은 `/proc/PID` 소멸로 별도 기록한다.
생성은 취소 접수 frame을 먼저 확인하고 그 시점에도 worker가 살아 있음을 확인한
뒤 release한다. 이후 실제 파일 상태와 결과를 대조한다. 준비·읽기 취소는 release
없이 회수한다. PDF text는 geometry와 독립이므로 resize frame을 먼저 확인한 뒤
선택 변경으로 held worker를 취소한다. resize 자체가 PDF text 작업을 취소했다고
해석하지 않는다.

모든 검사는 새 임시 HOME/XDG config/data/cache와 fixture에서 수행한다.
원본 payload와 기존 충돌 파일·목적지 내용도 확인한다. 실물 저장장치/네트워크
지연 성능 측정이나 물리 터미널의 픽셀 검증은 아니다.

## 요청·취소·반영 정책

- UiPathQuery는 기존 read lane을 사용한다. 취소/입력 변경 시 이전 job과 owned
  snapshot을 닫고, 최신 입력만 남긴다. 이전 worker 회수 중에는 오류 대신 최신
  요청을 대기시키며 한 lane에 여러 worker를 시작하지 않는다. buffered 결과도
  닫힌 요청에서 새 폼으로 넘어오지 않는다. 준비 중 Enter 반복은 실행을 시작하지 않는다.
- 목적지 검증 중에도 필드 편집과 focus 이동은 가능하다. 입력 변경은 pending
  Run/Paths를 무효화한다. Esc는 검증만 취소해 입력·초점을 유지하고, 다음 Esc는
  폼을 닫는다. 검증 실패는 값과 이름을 유지하고 목적지 필드로 돌아간다.
  F10은 shutdown 의도를 전파한다. resize는 기존 정책대로 중첩 폼을 닫는다.
- Browse는 새 목록을 성공적으로 받은 뒤에만 기존 목록·경로를 교체한다. 실패 시
  기존 유효한 탐색 snapshot은 유지한다. 최초 실패에서는 Use가 비활성이고 접근
  실패 문구를 표시한다. Parent/path로 복구할 수 있다. 취소하면 부모 폼 값과
  Browse 초점을 유지한다. 부모 폼이 다시 입력 대기하더라도 retiring 자식을 회수한다.
- 폼의 resolve는 사용성 검사다. 실제 변경 worker는 core_transfer_progress와
  기존 FD 기준 검증·no-clobber·링크/EXDEV 방어를 다시 수행한다. 검증 후 목적지가
  파일로 바뀌거나 이름이 충돌하는 경우 기존 내용을 보존하고 실패한다. 폼 검증이
  실행까지 inode를 예약한다는 의미는 아니다.
- 생성은 메모리상의 단일 이름 검사와 변경을 분리한다. 기존 cooperative mutation
  lane에서 parent FD 확보 전/후 취소를 확인하고 mkdirat 또는 O_EXCL/O_NOFOLLOW
  openat로 생성한다. 완료 뒤 취소가 와도 결과를 CANCELLED로 바꾸거나 지우지 않는다.
  현재 syscall이 끝날 때까지 취소 접수 화면으로 기다리는 기존 변경 정책을 유지한다.
- media 준비 프로세스가 source FD를 열고 identity와 magic을 검사한다. converter에만
  그 FD를 상속하고, 변환 후 열린 FD와 현재 path의 dev/ino/size/mtime/ctime을 모두
  비교한다. UI는 source FD를 받지 않고 output/diagnostic/result 캡처 FD만 가진다.
  준비 정상 반환에서 source FD/tool 문자열을 정리하고, UI는 자식 회수 뒤 캡처와
  private directory를 정리한다. argv/execv를 유지하며 파일명으로 셸 문자열을 만들지 않는다.
- media supervisor와 converter는 같은 process group에 속하고 parent-death kill도
  사용한다. 선택 변경·Off·모드 전환·모달 진입·종료는 옛 요청을 닫는다. active와
  retiring media가 겹쳐 쌓이지 않으며 회수 뒤 최신 요청을 자동 재시도한다.
  도구 설정은 모달 진입 시 media를 취소한 뒤 같은 슬롯을 사용한다.
- media source/도구 준비도 기존 8초 예산에 포함한다. 지원하지 않는 signature,
  접근 실패, missing tool, converter 실패는 기존 Result/detail로 구별한다.
  실패를 영구 캐시하지 않으며 selection/reset/refresh와 설정창 재진입은 재검사한다.
  terminal 지원 여부와 executable 탐색은 별개 사건이며 coder 지원은 여전히 실제 변환에서 확인한다.

## 슬롯과 우선순위

목록/검색/폼/Browse는 기존 read lane 하나를 공유한다. 폼과 Browse는 모달이므로
main 목록 탐색과 동시에 새 요청을 시작하지 않는다. 입력 변경과 모달 종료가
이전 요청보다 우선하며 retiring read가 정리되면 최신 요청 하나만 시작한다.
미리보기 본문은 독립 preview lane, 실제 생성·복사·이동 등은 기존 mutation lane이다.
mutation은 강제 종료로 취소하지 않으며 사용자 종료도 확정/불명확 결과와 회수를 기다린다.
media는 별도 기존 슬롯 하나이며 준비 프로세스+converter 최대 두 프로세스다.
새 media는 이전 group이 회수되기 전 시작하지 않는다. 추가 tool 전용 lane이나
무제한 worker 풀은 만들지 않았다. ncurses는 모든 경우 UI 프로세스에서만 호출한다.

## 회귀·sanitizer 연결

- `check-ui-io`를 `check-isolated`에 연결했다. held-call 36개, 일반 실행, 경로
  변경·중복 Enter·검증 뒤 충돌/교체·Browse denied/deleted 복구·완료된 옛 media
  캡처 폐기의 lifecycle을 확인한다. 최소/일반 화면, modal focus, resize를 포함한다.
- `path_query_test`: resolve/list/실패, pending/이미 받은 결과 교체, occupied read
  slot 재시도, 40회 반복 정리 후 FD 수와 ECHILD 확인.
- `media_worker_test`: posix_media.c를 직접 포함하고 독립 번역 단위에서 제외한다.
  준비·변환·tools·unsupported·missing/access 실패를 정상 반환시켜 자원을 확인한다.
- 기존 `mutation_worker_test`에 정상/취소 파일 생성과 디렉터리 생성을 추가했다.
  기존 media native 검사의 50회 취소·FD/자식/private temp 정리 assertion을 유지했다.
  원본 교체 검사는 converter가 pinned FD를 읽었다는 사건을 확인한 뒤 교체하도록
  gate를 추가했다. 비동기 open 성공을 원본 준비 완료로 가정하지 않는다.
- native sanitizer runner에 path_query_test와 media_worker_test를 추가했다.
  terminal sanitizer runner는 gated UI binary를 별도 출력 경로에 빌드하고
  세 UI 검사 스크립트를 실행한다. 빌드/실행 로그와 종료 코드, 검사별 요약 및
  GitHub step summary를 남기고 실행 실패 후에도 나머지를 검사한다.
  기존 CI 단계에서 자동 실행되며 terminal JSON 요약도 실패 artifact에 추가했다.
  기본 LSan·보안 설정·timeout·병렬도는 유지했다.

| 범위 | 실제 fork 검사 | 정상 반환 body 검사 | LSan 해석 |
| --- | --- | --- | --- |
| 경로 조회 | path_query_test 및 gated/lifecycle PTY | 전용 read-body harness 없음; 부모의 owned snapshot 해제 검사 | 부모 정상 종료 자원; `_exit` 읽기 worker 전체 누수 보증 아님 |
| 생성 | create-before/after gate, F10/Esc/resize·실제 상태/결과 | 확장 mutation_worker_test | 부모와 본문을 별도 실행; 실제 worker 모든 강제 사망 시점 보증 아님 |
| media 준비/도구 | source/tools gate, 완료 캡처 폐기, 기존 media native/PTY | media_worker_test | 부모/본문 분리; supervisor `_exit`와 외부 exec 도구 내부 누수 보증 아님 |

기본 LSan이 ptrace 오류를 내면 실패로 유지하고 미완료로 기록한다.
`--disable-leaks`는 명시적인 ASan/UBSan 대체 결과이며 LSan 성공이 아니다.

## 유지한 경로와 한계

앱 시작 디렉터리 읽기와 startup dual 초기화는 후속 작업이다. 설정/즐겨찾기 저장,
Vim 실행 전 파일 확인 등 이번 네 영역 밖의 동기 접근도 남아 있다. media 캡처용
로컬 `/tmp` 디렉터리·파일 생성/읽기/삭제는 UI에 남아 있다. 사용자 source/PATH의
느린 접근과 구별하며, 이번 gate로 로컬 `/tmp` 지연을 재현했다고 주장하지 않는다.

kill된 프로세스도 커널의 uninterruptible I/O가 반환하기 전에는 사라지지 않을 수 있다.
media editor handoff/exit는 이를 무한 wait하지 않고 기존 read shutdown과 같이
제한된 회수를 시도한다. 살아 있는 retiring media의 캡처는 조기에 삭제/재사용하지
않으며 새 요청은 기다린다. 실제 D-state/전원 상실/부모 강제 사망에서 private
임시 디렉터리의 사후 회수까지 보장하지 않는다. 정상적인 반복 취소 누적 검사는
이 한계와 별개다. 실제 장시간 네트워크/디스크 실험과 물리 Sixel 잔상 확인은 미실행이다.

## 수동 명령

```sh
make -j2 all
make check-ui-io
make -j2 check
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-ui-io-wsl
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-ui-io-wsl
# ptrace 환경에서만 명시적으로 누수를 제외한 별도 결과를 얻는다.
python3 tests/run_sanitizers.py --disable-leaks --output-directory /tmp/tfile-sanitizers-ui-io-fallback-wsl
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --disable-leaks --output-directory /tmp/tfile-terminal-sanitizers-ui-io-fallback-wsl
```

sanitizer는 일반 앱/테스트 바이너리를 덮어쓰지 않는다. 위 기본 명령을 ptrace 없는
실제 WSL 터미널에서 실행해야 부모·본문 LSan 미완료를 해소할 수 있다.
이전 바이너리 `/tmp/tfile-ui-io-before`가 있으면 일반/지연 baseline도 실행할 수 있다.
재생성하려면 별도 `/tmp` 디렉터리에 `git archive 3e0ae70`을 풀고 현재
`tests/ui_io_gate.c`를 복사한 뒤 `-DTFILE_IO_BASELINE`으로 그 트리의 model/core/
posix/UI 소스를 함께 컴파일한다(기존 6개 wrapper만 링크). baseline 스크립트는
`ui_io_latency_pty.py --baseline`, `ui_io_normal_pty.py --baseline`이며 현재 소스를
되돌리거나 일반 테스트 바이너리를 sanitizer로 덮어쓸 필요가 없다.

## 수정 전후 측정

동일 gate 조건의 각 6개 조합(Esc/F10/resize × 두 크기)에서 얻은 범위다.
단위는 ms이며 성공/회수를 확인한 사건까지의 PTY 관측 시간이다.

| 경계 | 수정 전 UI 정지 관측(최소) | 수정 후 frame 또는 exit | 수정 후 worker 회수 |
| --- | --- | --- | --- |
| 목적지 resolve | 201.9–205.5 | 0.8–66.4 | 0.7–66.4 |
| Browse open | 202.6–206.3 | 1.1–26.8 | 0.4–26.3 |
| 생성 전 | 201.9–204.3 | 9.8–35.3 | 19.8–45.3 |
| 생성 후 | 202.9–205.6 | 10.0–35.3 | 20.3–45.3 |
| media source open | 203.1–209.1 | 0.6–30.9 | 0.4–42.1 |
| executable access | 200.6–204.0 | 0.9–26.0 | 0.3–25.7 |

생성 회수는 **취소 접수 frame을 본 뒤 gate를 해제한 경우**의 값이다.
실제 느린 syscall 완료 시간을 단축했다는 뜻이 아니다. media source resize는
resize frame과 선택 변경 후 worker 회수를 구분한다. F10의 frame/exit 열은
프로세스 종료 관측이며 내부 입력 접수의 정확한 시각을 뜻하지 않는다.

일반 실행(100×24)의 before/after 관측은 transfer 11.4/52.5,
Browse 1.1/41.9, create 1.7/1.9, media 122.7/126.7,
tools 2.5/53.0ms였다. 50×9도 별도 실행했다. local syscall은 원래 빨랐고,
요청/결과 polling으로 약 40–50ms의 완료 지연이 추가될 수 있다. 입력 편집 자체에는
파일시스템 검증을 넣지 않았다. 일반 실행과 held-call 개선을 섞어 순수 I/O 처리량
향상으로 주장하지 않는다. 동시에 다른 회귀/빌드가 돌고 있었으며 CI 성능 보증이 아니다.

## 실행 중 드러난 회귀

첫 전체 회귀는 terminal_auto_pty의 터미널 응답과 missing-tool 결과 동시 완료
가정에서 실패했다. 같은 제한 시간 안에서 도구 결과 사건을 별도로 기다리게 하고
missing tool·이미지 없음·idle assertion은 유지했다. 별도 Auto 전체 검사도 통과했다.
두 번째 전체 회귀는 생성 충돌 안내에서 기존 `already exists` 문구가 사라진 것을
잡았다. worker의 자세한 경로·결과는 notice에 보존하면서 폼의 친절한 이름 충돌
안내를 복원했다. smoke assertion을 삭제/완화하지 않았다. 최종 전체 회귀를 재실행한다.

후속 sanitizer에서 tools 조회 Esc 뒤 Options 창은 복원됐지만 자식 회수가
늦어지는 문제가 재현됐다. terminal query parser를 사용하지 않는 모달에서도
retiring media가 있으면 기존 40ms poll과 reap을 적용했다. 600ms 회수 assertion은
그대로 유지했다. 정상 모달은 cleanup이 끝나면 다시 blocking 입력을 사용한다.

검증 결과 적용 직전에도 이미 queued된 편집/취소를 먼저 소비하도록 했다.
별도 `validation-ready` gate는 **결과 반영 직전의 UI**를 붙잡아 이 경쟁을
결정적으로 검사한다. 이것은 처음 여섯 파일시스템 gate와 다른 검사이며 UI PID가
marker에 기록되는 것이 의도다. 조회가 끝났어도 queued 목적지 편집이 이전 Run을
무효화해야 한다. 이 우선순위만 되돌린 `/tmp` 비교 바이너리에서는 이전 목적지에
작업이 성공하고 새 경로 반영 assertion이 실패했다. 수정 후 두 크기에서 통과했다.
추가 worker나 시간 제한 변경 없이 결과/입력 순서만 수정했다.

동시 빌드 중 기존 Vim 실패 PTY도 120ms send 구간에 오류 안내 도착을 가정해
실패했다. 기존 5초 event-wait helper로 정확한 오류 문구를 기다리고 원래 문구와
메뉴 복원 assertion을 유지했다. Vim 기능이나 그 동기 검증은 이번에 바꾸지 않았다.


도구 조회 gate를 유지한 채 Enter로 terminal 확인을 시작하는 검사도 추가했다.
수정 전에는 조회를 취소하고 완료된 것으로 표시해 `Image tool: missing`을 잘못
출력했다. terminal 확인과 executable 조회의 수명을 분리해, 조회가 끝나기 전에는
Checking을 유지한다. 같은 설정창/프로세스에서 도구 설치·제거·재설치 후 재검사가
각각 found/missing/found로 반영되는 것을 확인했다. 취소나 미완료를 negative cache로
바꾸지 않는다. 이 경계의 수정 전 실패/수정 후 통과도 증거에 보존한다.

전체 회귀의 기존 keyboard PTY는 Browse 경로 Enter 직후 같은 입력 묶음에 Space를
넣어, 새 목록 확인 전에도 Use가 실행된다고 가정했다. 로딩 중 Use 비활성 정책을
유지하고, 2초의 유한 조건 대기 안에서 실제 `No subdirectories` frame을 본 뒤 Space를
보내도록 바꿨다. 원래 Copy 폼 복원·입력 오류·초점 assertion은 유지했다.

후반 dual-transfer 회귀의 “폼 진입 즉시 목적지 오류” 기대값도 새 정책과 맞지
않았다. 반대편 경로 snapshot은 폼에서 보존하되 실제 조회는 Run/Paths 또는 Browse
요청 때 시작한다. 경고 assertion을 실제 Run 요청 뒤로 옮겼으며, 목적지 삭제·권한
거부·원본 보존·입력 수정·복구 assertion은 그대로다. dual/workflow의 Browse 경로
Enter+Space 묶음도 실제 목록 완료 뒤 Use하도록 수정했다. 로딩 중 Use를 허용하거나
오류 검사를 삭제해 기존 테스트를 통과시키지 않았다.


## 최종 실행 결과와 로그

| 검사 | 실제 결과 | 시간 |
| --- | --- | --- |
| `make -j2 check` | 전체 PASS, exit 0. 새 36개 gate·16개 lifecycle·10개 일반 실행 포함 | 11분 09.01초 |
| `make check-ui-io` | 두 native + 위 62개 PTY 사례 PASS, exit 0 | 57.23초 |
| native `--disable-leaks` | 33/33 PASS, exit 0 (ASan/UBSan) | wall 129.44초; runner 122.897초 |
| 기본 native LSan | 33/33 빌드 성공, 33/33 실행 ptrace fatal, exit 1 | wall 132.82초; runner 124.545초 |
| terminal/UI `--disable-leaks` | 12/12 suite PASS, exit 0. queued-edit·완료 media 캡처 폐기·tools-query 포함 | runner 243.178초 |
| media `--disable-leaks` | media/preview/picker/media_pty/media_redraw 5/5 PASS, exit 0 | 별도 wall 시간 미계측 |
| architecture / 문서 링크 / Python 구문 / diff 공백 | PASS | 정적 검사 로그 참조 |

native 기본 실패 33개의 각 로그에 `LeakSanitizer does not work under ptrace`가
있음을 확인했다. **부모 LSan 및 media/mutation/preview worker-body LSan 모두
미완료**다. terminal/UI 기본 LSan 별도 전체 실행, ptrace 없는 실제 WSL 재실행,
원격 CI, Clang clean-source(로컬 clang 부재)는 미실행이다. 기본 CI 누수 검사를
끄거나 suppressions를 추가하지 않았다. 원격 push는 하지 않는다.

실패한 전체 시도와 그 원인·수정은 위에 기록했고, 그 뒤 동일 전체 명령이 exit 0으로
끝났다. 마지막 FIFO harness는 reader 등록 뒤 marker를 공개해 controller의 release가
reader 등록보다 먼저 도착할 수 없게 했다. 같은 gate 36개와 queued-edit/tools-query를
직접 재실행해 통과했다. 임시 exit 9 컴파일러로 terminal runner를 실행한 오류 처리
검사도 통과했다. 재사용 디렉터리의 옛 PASS 요약을 INCOMPLETE/NOT RUN 실패 요약으로
교체하고 exit 9를 보존한다. 빌드 실패나 미실행이 성공으로 집계되지 않는다.

로그는 `/tmp/tfile-ui-io-final-regression.log`와 `.time`,
`/tmp/tfile-ui-io-check-complete.log`와 `.time`,
`/tmp/tfile-sanitizers-ui-io-complete/`, `/tmp/tfile-sanitizers-ui-io-lsan/`,
`/tmp/tfile-terminal-sanitizers-ui-io-complete/`, `/tmp/tfile-media-sanitizers-ui-io/`에 있다.
각 sanitizer 출력 디렉터리에 빌드/실행 로그가 있고 native/terminal은 JSON 및 log
요약도 남긴다. terminal runner는 직접 실행할 때도 HOME과 XDG config/data/cache를
모두 격리한다. 전체 회귀 최대 단일 프로세스 RSS는 155480 KiB,
native fallback 144700 KiB, 기본 LSan 145448 KiB였다. 동시 프로세스 합산 peak가 아니며
CI 메모리 사용 보증으로 확대하지 않는다. 기존 병렬도와 job timeout은 유지했다.

[핵심 실행 출력·실패 재현·최종 전체 회귀 증거](UI_IO_EVIDENCE_2026-10-16.txt)를
저장했다. 실물 디스크/네트워크 지연, D-state/전원 상실, 물리 터미널 Sixel 잔상은
자동 gate/native/PTY 통과로 대체하지 않는다.
