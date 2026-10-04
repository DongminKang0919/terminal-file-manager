# 이중 패널 복사·이동 검증 (2026-10-04)

## 기존 구현과 변경 범위

README, REFERENCE, PANELS_VALIDATION, BATCH_VALIDATION과 UI/core/platform 및 기존 검사를 확인했다. 패널별 AppState/UiFilePanel은 경로·목록·마킹·정렬·방문 기록·커서·스크롤을 이미 독립 소유했다. 활성 패널 선택, modal_depth에 의한 배경 패널 전환 차단, 단일/일괄 작업 엔진, 작업 종료 시 패널당 한 번 갱신, 파일 결과와 양쪽 갱신 결과 분리는 재사용했다.

목록+목록에서만 F5/F6의 To를 반대편 현재 디렉터리로 채운다. 80열 미만의 임시 숨김에도 적용한다. 명시적인 목록만/목록+미리보기는 기존 기본값(단일: 빈 To, 일괄: 원본 디렉터리)을 유지한다. UiTransferContext가 원본 패널 번호·원본 Base·기본 목적지 문자열과 열기 시점의 목적지 진단을 소유한다. 단일 원본 경로/이름은 기존 폼의 소유 버퍼, 일괄 원본들은 기존 BatchJob에 고정한다. 활성 마킹이 있으면 그것만, 없으면 커서 하나를 수집한다. 반대편 마킹은 수집하지 않는다. 이중 목록의 단일 Source 선택기는 잠그되 Name 입력은 유지한다.

목적지를 수정하거나 Browse로 선택해도 패널 탐색 위치는 바뀌지 않는다. 상대/빈 경로는 고정한 원본 Base 기준이다. 단일 Paths에는 검증·해석한 최종 목적지를 표시하고, 일괄은 기존 확인창에 해석한 목적지를 표시한다. 기본값 입력만으로 실행하지 않으며 기존 단일 실행 버튼/일괄 확인 흐름을 유지한다.

삭제·접근 불가 목적지는 폼에 안내하고 입력·선택을 유지한다. 이전 반대편 목록 갱신 실패(STALE)는 별도 안내이며 실행 시 현재 목적지를 다시 검증해 유효하면 진행한다. 유효하지 않은 Browse 시작 경로는 입력 경로 그대로 표시한다. 직접 p/Parent 탐색으로 복구할 수 있고 다른 디렉터리로 자동 대체하지 않는다. 실제 실행에서는 기존 core/platform 검증을 다시 수행한다.

성공 후 원본 커서·스크롤은 이중 모드에서 원본 이름 기준으로 보존한다. 성공 원본 마킹만 해제하고 실패·미실행은 기존 정책대로 유지한다. 반대편 마킹은 갱신에서 실제로 사라진 항목만 정리한다. 덮어쓰기, 자동 이름 변경, 재귀/링크/경로 교체/부분 실패/취소/EXDEV 정책은 변경하지 않았다.

## 회귀 검사 범위

- `panels_ui_test`: 50×9/80×24/160×24에서 양방향 × 복사/이동 × 단일/복수의 실제 폼 24개 조합. 커서 단일, 표시 단일, 복수 표시, 반대편 표시 제외·유지, 기본 목적지 문자열 소유권, 과거 STALE 목적지의 현재 재검증 성공, 활성 패널 유지, 패널별 1회 갱신을 확인한다. 각 조합에서 폼 취소 20회(총 480회)의 명시적 소유 메모리·FD·창 정리와 경로 스냅샷 할당 실패를 검사한다. 기존 동일 디렉터리 독립 상태·부분 실패·취소·양쪽 갱신 실패/결과 상세 검사도 유지했다.
- `dual_transfer_pty`: 50×9/80×24/160×32에서 기본 목적지와 명시적 마우스 실행, 커서 원본, 반대편 마킹·탐색 위치 유지, 상대 경로 편집, 이름 충돌, 폼 열기 후 목적지 삭제와 취소, 폼 열기 전 삭제 오류, Browse의 잘못된 시작 경로 유지와 p 선택 복구, 실제 권한 000 목적지 오류/권한 복구 후 이동, 단일/미리보기 기본값을 검사한다. 이 실행은 uid 1000으로 권한 검사를 생략하지 않았다.
- 기존 `panels_pty`의 기본 목적지 기대값을 새 규칙으로 바꾸고, 목적지 입력/Browse/확인 복귀/resize와 독립 탐색 검사를 유지했다. 기존 단일 전송, 일괄 목적지, 진행·결과, 안전한 표시, core/platform 안전성·취소·EXDEV 주입 검사를 재사용한다.

## 검증 결과와 제한

- `make -j3 check`: 전체 통과(exit 0). 기존 core/platform·UI·PTY 검사와 새 `dual_transfer_pty`를 포함한다. 로그: `/tmp/tfile-transfer-check-final.log`.
- `python3 tests/run_sanitizers.py --disable-leaks --output-directory /tmp/tfile-sanitizers-dual-transfer-final-no-leaks`: ASan/UBSan 17개 검사 모두 통과(exit 0), `detect_leaks=0`, `halt_on_error=1`. 일반 성능 수치나 누수 검사로 해석하지 않는다.
- `git diff --check`: 통과. 구현·테스트·문서 변경을 검토했으며 core/platform 작업 엔진에 변경이나 중복 구현이 없다.

로그는 /tmp의 격리 디렉터리에 두며 저장소에는 바이너리/임시 사용자 데이터를 추가하지 않는다.

자동 실행 환경에서 LeakSanitizer를 켠 17개 검사 실행은 모두 `LeakSanitizer does not work under ptrace` 오류로 완료하지 못했다. 누수 검사 통과로 간주하지 않는다. 로그: `/tmp/tfile-sanitizers-dual-transfer/`. 별도로 누수 검출을 끈 ASan/UBSan 검사와 명시적 메모리/FD 계측을 실행했다. 이 계측은 libc/ncurses 내부 전체 누수 증명이 아니다.

### 별도 WSL 터미널의 수동 누수 검사 검토

자동 실행 환경에서는 ptrace 제약으로 미완료였으나, 별도 WSL 터미널에서 해당 17개 테스트의 누수 검사 완료 및 누수 미검출을 확인했다. 사용자가 실행한 명령은 `python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-manual`이며, 17개 모두 PASS(exit 0)와 `Leak detection requested and enabled; inspect logs for runtime limitations. No suppressions are used.` 종료 출력을 보고했다. 테스트를 재실행하지 않고 해당 디렉터리의 산출물을 검토했다.

- 실행 로그 17개는 모두 PASS이고 ptrace/LeakSanitizer fatal error, 검사 생략·비활성화 안내, 누수 보고 또는 sanitizer 오류가 없다. 빌드 로그 17개는 모두 비어 있다.
- 스크립트는 `--disable-leaks` 없는 실행에서 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`을 설정한다. search/controller 하위 실행도 이 환경을 상속하며 스크립트는 suppression을 지정하지 않는다.
- 17개 바이너리 모두 `libasan` 연결을 확인했다. 바이너리 심볼과 코드에서 누수 검사를 끄거나 객체를 제외하는 훅, 기본 옵션·suppression 훅을 발견하지 않았다.

이 결과는 위 수동 실행에 포함된 17개 테스트와 실제 실행 경로에 한정한다. 프로젝트 전체, 모든 PTY 실행 또는 모든 사용 경로에 누수가 없다는 증명이 아니다. 자동 실행 환경의 미완료 기록은 별도로 유지한다. 수동 검토 로그 경로: `/tmp/tfile-sanitizers-manual/*.build.log`, `/tmp/tfile-sanitizers-manual/*.run.log`.

입력 상한 4,095바이트, 작은 화면의 한 줄 생략/초점 기반 스크롤, 동기 호출과 개별 이동 중 취소 제한은 유지한다. 경로 고정은 파일시스템 스냅샷이 아니며 모든 외부 교체 경쟁을 재현하지 않았다. 실제 다른 파일시스템 사이 이동은 새로 실행하지 않고 기존 EXDEV 주입 검사를 유지했다. 모든 PTY를 sanitizer 바이너리로 실행한 것은 아니다.

### 목적지 선택창의 읽기 실패 후속 수정

목록 조회 실패와 정상 빈 상태를 구분하고, 실패 시 원인 표시와 Use 차단을 적용했다. Parent/Enter path 복구와 실제 파일 작업의 재검증은 유지한다. 새 `tests/picker_test.c`는 uid 1000에서 실제 권한 거부·삭제 경로·빈 디렉터리·Space 차단·경로 수정/Parent 복구를 확인하며 `make check`, `make check-media`, 미디어 전용 sanitizer에 포함된다.

이번 명령·로그·미완료 LSan 기록은 [미디어 후속 검증](MEDIA_PREVIEW_VALIDATION.md#후속-정적-검토-수정)에 있다. 위의 과거 수동 17개 검사에서 확인한 누수 미검출 결과를 새 picker/media native 또는 media PTY 검사의 누수 검증으로 확대하지 않는다.


수정 후 별도 사용자 WSL 실행의 `/tmp/tfile-sanitizers-fixes-manual/` 17개 실행/빌드 로그와 `/tmp/tfile-media-sanitizers-fixes-manual/`의 media/preview/picker/media_pty 로그를 재실행 없이 검토했다. 모두 PASS이며 ptrace 오류·검사 생략·sanitizer 오류·누수 보고가 없다. 사용자는 두 명령을 `--disable-leaks` 없이 실행하고 누수 검출 요청/활성화 메시지를 확인했다고 보고했으며, 스크립트의 `detect_leaks=1` 설정과 바이너리의 libasan 연결도 확인했다. 이번 새 picker/media 실행 경로의 제한된 누수 미검출 결과는 [수동 검토 기록](MEDIA_PREVIEW_VALIDATION.md#수정-후-사용자-wsl-터미널의-수동-sanitizer-결과-검토)에 별도로 기록한다. 기존 자동 환경의 LSan 미완료 기록은 유지하며 외부 변환 도구 내부나 프로젝트 전체로 확대하지 않는다.
