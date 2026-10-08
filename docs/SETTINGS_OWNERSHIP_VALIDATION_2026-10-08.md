# 설정 경로 소유권과 누수 검사 — 2026-10-08

## 사용자 로그와 근본 원인

사용자가 일반 WSL 터미널에서 실행한 다음 검사는 **실제 누수**를 검출했다. 이 결과를 ptrace 제약으로 분류하지 않는다.

```sh
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-manual-20261008
```

첨부된 각 `.run.log`를 직접 확인했다.

| 검사 | 사용자 로그의 누수 | 초기화/종료 횟수 |
| --- | --- | --- |
| batch_ui_test | 126바이트 / 3개 | 화면 크기별 3회 |
| controller_test | 42바이트 / 1개 | 1회 |
| history_ui_test | 42바이트 / 1개 | 1회 |
| search_progress_ui_test | 42바이트 / 1개 | 1회 |

공통 할당 경로는 `platform_path_join → platform_settings_path → settings_load → ui_init`다. 바이트 수는 설정 경로 문자열 길이에 따른다. 네 테스트가 `ui_init()` 뒤에 `app_free()`·`preview_reset()`·`notice_clear()`만 조합해 호출하고, 설정을 포함한 전체 UI 정리 함수인 `ui_free()`를 생략했다. 일괄 UI 검사는 이 누락을 세 번 반복해 같은 소유 객체 세 개를 남겼다.

## 소유권과 제품 코드 영향

1. `platform_settings_path()`는 중간 `base`를 내부에서 해제하고, 최종 경로를 `out`으로 넘긴다.
2. `settings_load()`가 이를 `SettingsStore.path`에 보관한다. 설정 파일이 없거나 읽기/파싱에 실패해도 경로가 만들어졌다면 store가 계속 소유한다. 저장과 재시도에 필요하므로 즉시 해제하면 안 된다.
3. `UiContext.settings`가 이 store의 수명을 관리한다. `ui_free()`는 `settings_free()`를 호출하고, 이 함수가 경로를 한 번 해제한 뒤 store를 초기화한다. `ui_free()`도 전체 context를 초기화한다.
4. 실제 main은 정상 종료와 `ui_init()` 실패 모두에서 `ui_free()`를 호출한다. 터미널 사전 검사 실패는 UI를 초기화하기 전이다.

추적한 제품 종료 경로에서는 이번 설정 경로 해제 누락을 발견하지 않았다. `platform_path_join()`이나 소유 경로에 임의 해제를 넣지 않았으며, 제품의 할당/해제 동작과 구조를 변경하지 않았다. 헤더에는 수명 계약만 명시했다.

`ui_init()`은 새 context 또는 `ui_free()`한 context를 초기화한다. 활성 context에 다시 호출하는 재설정 API가 아니다. `settings_load()`도 새 store 또는 `settings_free()`한 store에 사용한다. 초기화 실패 뒤에도 caller는 `ui_free()`로 정리해야 한다. 새 context가 반드시 0으로 초기화됐다고 가정해 `ui_init()` 앞에 무조건 `ui_free()`를 호출하는 변경은 하지 않았다.

## 수정과 회귀

- 네 테스트의 부분 정리를 `ui_free()`로 교체했다. 별도로 소유한 파일 경로와 ncurses SCREEN/FILE의 기존 정리는 유지했다. UI는 ncurses 화면을 닫기 전에 정리한다.
- 전체 `ui_init()` 호출부를 조사해 `tests/performance.c`의 세 부분 정리 경로도 같은 방식으로 수정했다. 성능 측정의 가짜 main 실행에는 기존 initscr 래핑에 맞춰 터미널 사전 검사도 래핑해, 실제 TTY 없는 측정 환경에서 실행할 수 있게 했다.
- `panels_ui_test`에 8회 초기화/정리/재초기화와 반복 정리, 존재하지 않는 시작 경로로 초기화 실패 후 정리를 추가했다. 실제 설정 경로가 추적되는 할당임을 확인하고, 매 주기마다 애플리케이션 할당 수와 FD 수가 기준으로 돌아오는지 확인한다.
- 실제 main의 실패 분기도 startup 검사에서 실행해 입력 루프 진입 없이 반환하는지 확인했다. 기존 100개 초기/두 번째 패널/재개방 할당 실패 위치 검사도 유지했다. 누수 객체를 전역에 남기거나 suppression을 추가하지 않았다.

## 최신 검사 결과

| 검사 | 결과 |
| --- | --- |
| 관련 native 6개 | 통과: panels, batch UI, controller, history UI, search progress UI, startup main |
| 기본 sanitizer 17개 | **ASan/UBSan/LSan 모두 통과**, exit 0 |
| 성능 측정 harness, 3회 | 실행 통과. 성능 개선 또는 누수 검사를 뜻하는 수치 비교는 하지 않음 |
| 전체 `make -j4 check` | 최종 상태에서 통과, exit 0 |
| 의존 계층 / diff 공백 검사 | 통과 |

LSan 환경은 구분한다. 먼저 샌드박스 안의 작은 malloc/free 진단에서 `LeakSanitizer does not work under ptrace`로 중단됐다(`/tmp/tfile-lsan-ownership-probe.log`). 같은 조건으로 반복하지 않고, 권한을 받아 **샌드박스 밖에서 최신 소스를 새로 빌드한 기본 sanitizer 17개**를 실행했다. 이 실행은 기본 `detect_leaks=1`로 완료했고, 네 문제 검사와 새 생명주기 회귀를 포함해 전부 exit 0이었다. 모든 `.run.log`에서 LeakSanitizer 오류·ASan 오류·UBSan runtime error가 없음을 확인했다. `LSAN_OPTIONS`는 상속되지 않았으며 `--disable-leaks`나 suppression을 사용하지 않았다.

따라서 여기서의 누수 검증 완료는 **위 17개 검사 범위**다. 실제 제품의 모든 사용 흐름, 미디어 전용 검사와 모든 환경의 누수 부재를 뜻하지 않는다. 사용자 WSL 결과와 자동 환경의 제한을 혼동하지 않는다.

실행 명령과 로그:

```sh
# 이 검사만 샌드박스 밖에서 실행: 누수 검출 기본값 유지
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-settings-ownership-final-20261008 \
  > /tmp/tfile-settings-ownership-sanitizers-final.log 2>&1
make -j4 check > /tmp/tfile-settings-ownership-check-final.log 2>&1
python3 tests/isolated_check.py python3 tests/run_performance.py \
  --data /tmp/tfile-perf-settings-ownership-data \
  --output /tmp/tfile-perf-settings-ownership.json --reps 3
```

## 사용자 환경에서 다시 실행할 명령

기존 누수 로그를 보존하도록 새 출력 디렉터리를 사용한다. 임시 HOME/XDG wrapper는 실제 사용자 설정의 영향을 제거하고 기본 sanitizer 17개를 실행한다. 누수 검출은 기본으로 켜져 있다.

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-settings-ownership-manual-20261008
make -j4 check
```

개별 결과는 새 출력 디렉터리의 `<test>.run.log`에서 확인한다. 이번 수정 뒤의 로그를 기준으로 판단하며 이전 버전 결과를 재사용하지 않는다.
