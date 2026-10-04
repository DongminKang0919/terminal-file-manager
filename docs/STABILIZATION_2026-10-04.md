# 로컬 연속 사용 흐름 점검 및 중간 안정화 (2026-10-04)

## 기준과 환경

시작 기준은 master `e30dffb`이며 작업 트리는 깨끗했다. README, REFERENCE, PANELS_VALIDATION, BATCH_VALIDATION, TRANSFER_VALIDATION, 이전 안정화 보고서와 최근 커밋을 확인했다. 반대편 기본 목적지가 적용된 현재 이중 패널을 기준으로 기존 UI → core → platform, 독립 패널 상태와 단일/일괄 작업 엔진을 재사용했다. 기존 사용자 변경은 없었으며 원격 푸시는 하지 않는다.

Linux WSL2 `6.6.87.2-microsoft-standard-WSL2`, x86_64, uid 1000, GCC 13.3.0, ncursesw 6.4.20240113, C.UTF-8, xterm-256color에서 실행했다. 데이터는 전용 `/tmp/tfile-workflow-*`, `/tmp/tfile-recovery-*` 및 기존 테스트의 임시 디렉터리에만 준비했다. 일반 파일·빈 폴더·중첩 폴더·숨김 파일·숨김 항목만 있는 폴더·한글/공백/긴 이름·일반/디렉터리/깨진 심볼릭 링크·바이너리·권한 거부·충돌 데이터를 사용했다. 권한 검사는 root로 생략하지 않았다.

## 변경 전 기록과 발견한 문제

기존 `make -j3 check`는 exit 0으로 통과했다(`/tmp/tfile-stabilization-before-check.log`). 새 정상 연속 흐름은 50×9, 80×24, 160×32 모두 통과했다(`/tmp/tfile-stabilization-workflow-before.log`). 그러나 새 실패·복구 검사 초판은 3개 크기에서 아래 재현 조건을 검출했다. 기존 테스트 통과만으로 실제 연속 사용의 문제가 없다고 판단하지 않았다.

| 우선순위 | 재현 조건과 실제 동작 | 원인 | 수정과 회귀 검사 |
| --- | --- | --- | --- |
| P1: 결과 오안내 | 파일 생성 성공 알림을 남긴 뒤 접근 불가 폴더 Enter, 현재 폴더 권한 제거 후 r, 삭제된 방문 위치 Forward, 누락이 있는 검색 종료. 새 문제 대신 이전 `[Success] Create file`이 계속 보였다. | 탐색/새로고침의 show_failure와 검색의 오류·누락·취소 종료가 status_priority를 설정하지 않아 retained notice가 새 안내를 가렸다. | 새 실패/검색 중단 안내를 우선 표시하고 최근 파일 Result는 보존한다. `recovery_pty`가 네 조건과 이전 상세 유지, 검색 누락 결과 열기를 검사한다. `search_ui_pty`는 기존 성공 뒤 검색 x/resize 취소와 이전 상세 보존도 검사한다. |
| P1: 갱신 실패 표시 누락 | 실제 파일 API 성공 후 목록 갱신 오류를 주입했다. 160열에는 갱신 실패가 보였지만 50×9에는 `[Success] ! detail`만 표시됐다. | 좁은 화면의 retained notice 분기에서 refresh failed를 생략하고, 긴 이중 패널 요약이 안내 공간을 차지했다. | 작업 결과와 갱신 실패를 함께 표시하고 작은 화면의 요약만 축약한다. 갱신 결과는 해당 작업에 보관해 일반 탐색 오류와 구분한다. `panels_ui_test`의 실제 ncurses 셀 검사에서 원본/상대/양쪽 갱신 실패를 50/80/160열로 검사한다. 오류 주입이며 실제 PTY에서 filesystem 갱신 실패를 자연 발생시킨 검사로 표현하지 않는다. |
| P2: 빈 목록의 복구 불가능 폼 | 빈 이중 목록에서 F5/F6를 누르면 Item 1 폼이 열렸지만 원본이 비어 있고 Source는 비활성화였다. | 기존 단일 화면의 Source 선택기 흐름을 이중 화면에도 적용하고 대상 없음의 진입 검사가 없었다. | 이중 목록의 대상 없음은 폼을 열지 않고 안내한다. 기존 단일 화면 Source 선택기는 유지한다. `recovery_pty`가 복사/이동 모두 파일 결과·목록 보존을 확인한다. |
| P2: 리사이즈 취소 전달 누락 | F5 → Paths에서 크기가 원래 값인 SIGWINCH/KEY_RESIZE를 소비하면 Paths만 닫히고 부모 전송 폼은 남았다. | 내부 창의 resize 이벤트를 전달하지 않고, 부모는 LINES/COLS의 최종 숫자 변경만 확인했다. | 기존 resized 전달 경로로 이벤트를 부모까지 전파해 폼 전체를 취소한다. PTY에서 같은 크기의 resize 이벤트를 재현하고, native 검사에서도 12개 패널/크기/대상 조합의 합성 KEY_RESIZE로 창·메모리·FD·선택·마킹·이전 결과·갱신 없음까지 확인한다. 기존 취소 정책을 바꾸지 않는다. |

재현 로그: `/tmp/tfile-stabilization-recovery-before.log`(초판 21개 기대 동작 실패), `/tmp/tfile-stabilization-refresh-before.log`(50×9 갱신 실패 표시 부재), `/tmp/tfile-stabilization-probe.py`, `/tmp/tfile-stabilization-resize-probe.py`. 프로브도 임시 데이터만 사용했다. 수정 전 데이터 손상·잘못된 패널 대상 처리·크래시를 확인하지 못했으며, 추정만으로 엔진이나 구조를 변경하지 않았다.

## 연속 시나리오와 수정 후 결과

`workflow_pty`는 한 터미널 세션에서 아래 동작을 이어서 실행한다. `recovery_pty`는 성공 결과를 보유한 상태에서 실패·수정·재시도·취소를 이어서 검사한다. 모두 50×9, 80×24, 160×32에서 실행한다. 기존 테스트의 독립 검사는 이 연속 검사와 함께 유지한다.

| 시나리오 | 기대 동작 | 실제 확인과 근거 |
| --- | --- | --- |
| Left A / Right B, Tab과 패널 제목 클릭, 정렬·방향·숨김 변경 | 활성 패널만 변경, 원본 이름으로 커서 유지, 정렬은 마킹 유지, 숨김 off는 사라진 숨김 마킹 해제 | PTY 정상 흐름 통과. 넓은 화면의 마우스 활성화와 좁은 화면의 Tab, 기존 panels/sort/layout PTY의 전체 클릭 영역 검사도 최종 전체 실행에서 통과했다. |
| 검색 결과로 하위 폴더 이동 → 빠른 찾기 → 여러 항목 표시 | 원본 패널만 탐색, 다른 디렉터리 이동은 마킹 초기화, 빠른 찾기는 커서만 변경 | PTY에서 중첩 폴더의 두 항목을 수집하고 반대편 peer 마킹을 보존했다. 누락된 검색에서도 이미 찾은 파일을 열었다. |
| 마킹한 항목을 기본 반대편으로 복사 | 명시적 확인 후에만 실행, 반대편 마킹은 원본 아님, 성공 원본 마킹만 해제 | 실제 파일 내용과 양쪽 마킹, 원본 커서/표시 범위를 확인했다. 기존 dual_transfer/panels 검사와 함께 양방향 단일/일괄 복사·이동을 확인한다. |
| 원본 중첩 폴더 기준 `../../C` 입력, Browse의 p로 C 선택 후 이동 | Base는 고정한 원본, 목적지 편집/Browse는 B 탐색을 바꾸지 않음, 이동 후 양쪽 목록 반영 | 실제 C의 파일 내용·원본 소멸·B의 경로/peer 마킹을 확인했다. |
| 실제 일괄 중간 이름 충돌 | 첫 항목만 성공, 충돌 항목 실패, 마지막 미실행, 덮어쓰기 없음, source 성공 표시만 해제 | Size 내림차순의 크기 400/200/100바이트 항목으로 순서를 고정했다. 첫 파일 내용, 기존 충돌 내용, 마지막 목적지 부재, source 2개/peer 1개 마킹을 검사했다. 상세를 끝까지 페이지 이동하여 Partial completion, Success/Failed/Unexecuted를 확인했다. 오류 주입 없이 실제 PTY/파일시스템 충돌이다. |
| Back/Forward → 이름 변경 → 생성 → 테스트 항목 삭제 | 방문 커서/스크롤 복원, 같은 원본 이름 선택, 안전한 파일 변경 | 목록 데이터 행·범위 화면을 저장해 Back/Forward 복원과 비교하고 실제 이름/존재를 확인했다. |
| 목록+미리보기 → 단일 → 다시 이중 | 활성 마킹 유지, 명시적으로 숨긴 상대 마킹 해제, 과거 숨긴 마킹이 원본에 섞이지 않음 | 활성 패널 1개, 다시 표시한 상대 0개 마킹을 확인했다. 리사이즈 임시 숨김은 마킹을 유지한다. |
| 빈 폴더 / 숨김만 있는 폴더 / 링크 / 바이너리 / 읽기 실패 미리보기 | visible 목록 기준 안내, 숨김 설정 복구, 링크·바이너리 안전 표시, 키보드 초점 복원 | 새 흐름의 빈/숨김 안내·미리보기 복귀와 기존 layout/keyboard/display PTY의 상세 셀·본문·오류·가드 검사를 함께 사용한다. |
| 폼 연 뒤 원본 삭제 또는 읽기 권한 제거 | 실제 core/platform 재검증, 다른 항목 처리 없음, 오류 폼 유지 | `recovery_pty`에서 원본 삭제 뒤 목적지 생성 없음, 읽기 권한 제거 뒤 오류와 Name/To 유지, 권한 복구 후 같은 폼 재시도와 원본/복사본 내용을 확인했다. |
| 폼 연 뒤 목적지 삭제·접근 거부, 오류 입력 수정/Browse 복구 | 입력과 원본 선택 유지, 다른 경로로 자동 대체 없음 | 기존 dual_transfer/transfer_form/batch_destination PTY를 현재 바이너리로 재실행해 통과했다. 삭제된 반대편 경로 표시·Browse의 p 선택·권한 복구 후 재시도와 반대편 위치 유지가 포함된다. |
| 같은 디렉터리/이름 충돌, 자기 하위로 복사·이동 | no-overwrite와 자기 내부 전송 금지, 폼 오류 유지 | `recovery_pty`에서 원본 파일 보존, 하위 폴더에 잔여 전송 없음과 폼 오류 후 취소를 확인했다. |
| 진행 중 Esc/마우스/resize, 부분 성공 뒤 취소, 검색 취소 후 결과 이용 | 이미 변경한 내용과 대상별 상태 일치, 미실행/실패 표시 유지, 배경 명령 차단 | 기존 progress/batch_progress/search_ui PTY의 파이프 동기화로 실제 작업 시점을 고정하고 UI 취소 및 실제 잔여물·상세를 검사한다. 취소 시점 동기화와 외부 오류 주입을 구분한다. |
| 폼/도움말/상세 반복, 최소/넓은 화면, 줄임/복원 | 창/FD 해제, 선택·초점·마킹 보존, 화면 가드와 실제 클릭 좌표 일치 | 실제 세션별 warm-up 5회 후 20회 반복의 FD/RSS를 기록했다. native 명시적 소유권·FD 계측과 기존 popup/text_window/geometry 검사도 사용한다. |

의도적으로 초기화되는 상태: 다른 디렉터리로의 이동은 해당 패널의 마킹을 해제한다. 명시적인 단일/미리보기 전환은 숨기는 상대 마킹만 해제한다. 숨김 필터로 사라진 표시와 실제로 사라진 항목은 정합성 갱신으로 제거한다. 방문 기록에는 마킹을 저장하지 않는다. 미리보기 파일 변경·새 탐색·성공 새로고침은 기존 미리보기 세션/스크롤 초기화 정책을 따른다. 정상 정렬·동일 디렉터리 갱신·Tab·임시 좁힘·실행 전 취소는 가능한 범위에서 독립 커서/스크롤/마킹을 유지한다.

FD/RSS 관측: 변경 전 각 세션 FD 3 고정, RSS 50×9=2688, 80×24=2944, 160×32=3200 KiB 고정이었다. 부분 실패 상세 순회를 추가한 수정 후 독립 흐름에서는 FD 3 고정, RSS 2944/3072/3200 KiB로 각 반복 구간에서 고정이었다(`/tmp/tfile-stabilization-workflow-final.log`). 서로 다른 테스트 작업량의 RSS를 성능 개선으로 비교하지 않으며, 전체 프로그램 누수 부재의 증명으로 해석하지 않는다.

## 검사 결과 및 방식 구분

최종 `make -j3 check`는 **PASS(exit 0)**로 종료했다. 로그는 `/tmp/tfile-stabilization-verified-check.log`다. 새 연속/복구 PTY와 기존 native·PTY 검사를 모두 포함한다. `git diff --check`와 변경한 Python 파일의 구문 검사도 통과했다. 중간 실행에서 작은 화면 요약 변경에 따른 기존 active 표시 가정과 검색 gate의 64항목 체크포인트를 바꾸는 테스트 fixture를 수정했으며, 최종 전체 재실행으로 확인했다.

- 실제 PTY: 새 workflow/recovery와 기존 탐색·표시·검색·전송·일괄·도움말·이력·미리보기·취소 검사. 일반 제품 바이너리와, 취소 시점을 동기화하는 테스트 전용 gate 바이너리를 구분한다.
- native 오류 주입·경쟁 동기화·계측: 양쪽 목록 갱신 실패, 명시적 할당 실패, 파일 경로 교체 경쟁, EXDEV, 일부 진행·취소 경계와 FD/소유 메모리. 작업 성공과 갱신 실패는 독립 Result로 확인했다. 이를 자연 발생한 실제 PTY 장애로 표현하지 않는다.
- 수정 후 ASan/UBSan: `/tmp/tfile-sanitizers-stabilization-final-no-leaks/`의 17개 격리 검사 모두 PASS(exit 0), `detect_leaks=0`, `halt_on_error=1`. 새 native Paths/갱신 안내 검사 포함. 로그: `/tmp/tfile-stabilization-sanitizers-final-no-leaks.log`.
- 수정 후 자동 LSan: `/tmp/tfile-sanitizers-stabilization-2026-10-04/`에서 17개 모두 `LeakSanitizer does not work under ptrace` fatal error로 미완료. 보안 설정을 변경하거나 suppression으로 숨기지 않았다. 로그: `/tmp/tfile-stabilization-sanitizers.log`. ASan/UBSan fallback을 누수 검사 성공으로 기록하지 않는다.

### 기존 수동 누수 검사 산출물 재검토

`/tmp/tfile-sanitizers-manual`에 실행/빌드 로그와 바이너리가 남아 있었다. 전체 산출물 mtime 범위는 **2026-10-04 08:45:45.725–08:46:05.501 KST**(UTC 2026-10-03 23:45:45.725–23:46:05.501)다. 실행 대상은 panels_ui, batch, batch_ui, sort, progress, search, controller, history_ui, keyboard_ui, popup_style, progress_ui, preview, text, text_window, startup_ui, search_progress_ui, ownership의 17개다.

17개 실행 로그 모두 PASS이고 빌드 로그는 비어 있다. ptrace fatal error·검사 생략/비활성화 안내·sanitizer 오류·누수 보고가 없다. 스크립트는 사용자가 보고한 `--disable-leaks` 없는 명령에서 `detect_leaks=1:halt_on_error=1`을 명시하고 search/controller 하위 실행도 상속한다. 17개 바이너리의 libasan 연결과 누수 검사 비활성화/객체 무시/default option/suppression 훅 부재를 다시 확인했다. UI의 “disabled attributes”와 같은 정상 PASS 설명은 검사 생략으로 오인하지 않았다.

자동 환경에서는 ptrace 제약으로 미완료였으나, 별도 WSL 터미널의 **당시 해당 17개 테스트는 누수 검사 완료·누수 미검출**로 기록한다. 산출물은 이번 수정 전이며 git 해시·전체 실행 환경 manifest가 없어 정확한 소스 커밋의 증명으로 사용하지 않는다. 이번 수정 후 코드의 누수 검사 결과나 프로젝트 전체의 누수 부재로 확대하지 않는다. 이 기존 디렉터리는 덮어쓰거나 재실행하지 않았다.

이번 수정 후 버전은 프로젝트 루트의 별도 WSL 터미널에서 다음 명령으로 검사할 수 있다. 기존 수동 로그와 다른 출력 디렉터리를 사용하고 `--disable-leaks`를 붙이지 않는다.

```sh
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-stabilization-manual-2026-10-04
```

## 남은 제한 및 다음 작업 후보

- 실제 다른 파일시스템 간 이동은 새로 수행하지 않고 기존 EXDEV 주입 검사를 유지했다. 모든 외부 동시 파일 교체 경쟁, 느린 원격 파일시스템, 모든 터미널 에뮬레이터를 재현한 것은 아니다. 모든 PTY를 sanitizer 바이너리로 실행하지 않았다.
- 한 줄 진단/경로의 생략, 4,095바이트 입력 상한, Result 진단 상한, 동기 호출·개별 이동 중 취소 제한, resize 시 입력 취소, 부분 잔여물 보존은 기존 정책이다. 정책을 새 기능으로 바꾸지 않았다.
- 긴 진단을 폼에서 더 쉽게 확인하는 방법, 작업 알림과 임시 안내의 수명 표현, 빈 미리보기의 설명을 개선하는 방안은 UX 후보다. 이번에는 새로운 알림 큐/타이머/설정·비동기 작업을 넣지 않았다.
- 다음 작업은 위 정확한 명령으로 수정 후 LSan을 별도 터미널에서 확인하는 것이 우선이다. 그 후 실제 사용에서 재현된 긴 진단·초점 문제를 수집해 범위를 좁혀 검토한다. 성능 최적화나 구조 변경은 새 측정 근거가 있을 때만 별도 작업으로 진행한다.
