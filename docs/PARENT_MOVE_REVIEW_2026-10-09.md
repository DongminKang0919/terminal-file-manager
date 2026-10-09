# 이동 부모 경로 방어 및 UI 경로 분리 검증 — 2026-10-09

이번 작업은 이동/이름 변경 부모 경로 방어와 UI 경로 규칙 의존성만 다룬다. 이미지 재전송 최적화, 새 기능, 대규모 리팩터링은 포함하지 않는다.

## 재현과 수정

최신 작업 전 코드의 `platform_move`는 `renameat2(AT_FDCWD, src, AT_FDCWD, dst, RENAME_NOREPLACE)`를 사용했다. 보고서의 정적 분석을 그대로 재현 결과로 간주하지 않고 `tests/operations_test.c`에 실제 syscall 직전의 linker wrapper를 추가했다. 이 시점은 수정 코드가 양쪽 부모 FD를 확보한 뒤다. wrapper는 부모 디렉터리를 다른 이름으로 옮기고 원래 경로에 별도 디렉터리를 향하는 symlink를 둔다. 스케줄링/시간에 의존하지 않으며 실제 파일 이동 syscall을 실행한다.

수정 전 원본 부모 교체 검사에서 이동은 성공했지만 목적지 내용이 `original` 대신 `decoy`가 되어 내용 assertion이 실패했다(exit 1, 자식 SIGABRT). 이것은 해당 제어된 시나리오의 실제 재현이며 모든 가능한 레이스를 재현한 것은 아니다. 수정 후 원본 부모 교체, 목적지 부모 교체, 동일 부모 내 이름 변경의 세 검사가 모두 통과했다. 원본은 고정된 디렉터리에서 이동하고 대체 디렉터리의 decoy는 그대로 유지되며 그곳에 목적지를 만들지 않는다. 각 검사에서 열린 FD 수가 시작값으로 돌아온다.

이동은 기존 `operation_parent`를 재사용해 각 조상을 `O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC`로 열고 양쪽 부모 FD와 검증된 leaf 이름으로 `renameat2`를 호출한다. `RENAME_NOREPLACE`는 그대로다. syscall 직전에 목적지를 만드는 검사에서 `RESULT_EXISTS`, 원본/목적지 내용 보존을 확인했다. `EXDEV` 주입에서는 `RESULT_CROSS_DEVICE`, partial=false, 원본 유지, 목적지 미생성, FD 정리를 확인했다. 실제 다른 파일시스템 이동을 새로 실행한 검사는 아니다. 복사 후 삭제 fallback, rollback, 이동 중 취소를 추가하지 않았다. 기존 syscall 오류 매핑과 일괄 작업의 부분 성공 처리는 유지한다.

## 방어의 한계

부모 FD 확보 **이후** 해당 경로가 이름 변경되거나 링크로 교체돼도 이동은 확보한 디렉터리에서 수행한다. 표시/반환 경로 문자열은 원래 요청 경로이므로 외부 교체 후 실제 디렉터리 위치와 달라질 수 있다. 부모 FD 확보 전 core의 경로/메타데이터 검사와 syscall 사이 전체를 파일시스템 스냅샷으로 만들지는 않는다. `O_NOFOLLOW`는 조상 링크를 거부하지만, FD 확보 전에 다른 실제 디렉터리로 교체된 경우까지 UI가 확인한 inode와 원자적으로 연결하지 않는다.

최종 원본 leaf는 이전 메타데이터 조회의 inode에 원자적으로 묶이지 않는다. 다른 프로세스가 그 이름의 항목을 syscall 전에 교체하면 교체된 leaf가 이동할 수 있다. 목적지 leaf의 늦은 생성은 `RENAME_NOREPLACE`가 거부한다. 이 두 보장을 혼동하지 않는다. 원본 symlink 자체의 이동과 부모 symlink를 따라가는 동작도 구별한다. 조상 symlink를 통한 직접 platform 호출은 기존 복사/삭제와 같이 거부한다.

## UI와 계층 검사

`controller.c`의 이름 변경/전송은 `core_path_name`으로 원래 이름을 작업 **전**에 확보한다. 삭제는 선택한 항목의 이름을 미리 복사한다. 확보 실패는 파일 작업 전에 반환한다. 성공 후 마킹 해제에는 확보한 이름을 사용하며 `app_unmark` 자체도 할당하지 않는다. 이후 목록 갱신이 실패해도 마킹 해제는 이미 끝난다. 실패/취소/이름 변경 no-op 경로에서 이름 메모리를 해제한다.

controller 회귀 검사에 복사/삭제/이름 변경 성공 및 목록 갱신 실패 시 마킹 해제 assertion을 추가했다. 원래 raw byte 이름 검사도 유지한다. 계층 검사의 좁은 `strchr/strrchr` separator 및 `PATH_MAX` 규칙을 core와 UI 양쪽에 적용하고 separator 검출/경로 API 허용 fixture를 추가했다. 이는 모든 가능한 경로 구문 의존성을 증명하는 검사는 아니다.

새 이동 회귀 검사는 `make check`에 포함된다. 기본 sanitizer runner에도 operations 검사를 추가해 기존 17개와 구분되는 18개 범위로 실행한다.

## 수동 누수 검사

자동 실행에서 LSan을 요청했으나 `LeakSanitizer does not work under ptrace` fatal error가 발생했다. 누수 검사 성공으로 간주하지 않는다. 디버거/추적기가 없는 사용자 환경에서 아래 명령을 실행한다. 출력 디렉터리를 구별하고 과거 통과 기록을 이번 결과로 대체하지 않는다.

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-parent-manual-20261009
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-parent-manual-20261009
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-parent-manual-20261009
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 make check-settings-sanitize
```

## 이번 직접 실행 결과

- 관련 native 회귀: `make tests/operations_test tests/controller_test`, `python3 tests/run_operations.py`, `python3 tests/run_regressions.py controller` 통과. 수정 전 원본 부모 교체는 위에 기록한 대로 실패했다.
- `python3 tests/check_architecture.py` 통과. 별도 임시 src/ui fixture에 원래 `strrchr(source,'/')`를 넣었을 때 새 검사가 `native path rule in ui`로 거부함을 직접 확인했다.
- tests/tools Python AST 문법 검사 및 `git diff --check` 통과. AST 검사는 동작 검사가 아니다.
- 기본 sanitizer: `python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-parent-fallback-20261009 --disable-leaks` — **18개 ASan/UBSan 통과**. 기존 17개에 새 operations 검사를 추가한 범위다.
- 미디어 sanitizer: `python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-parent-fallback-20261009 --disable-leaks` — media/preview/picker/media_pty **4개 통과**.
- 터미널 sanitizer: `python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-parent-fallback-20261009 --disable-leaks` — terminal/input/probe/pty/auto_pty **5개 통과**.
- 설정 sanitizer: `ASAN_OPTIONS=detect_leaks=0 make check-settings-sanitize` — settings_test **통과**. settings_core_test나 settings PTY를 sanitizer로 실행한 결과로 확대하지 않는다.

LSan 최초 요청은 기본 17개에서 모두 fatal error였고, 새 operations 검사도 별도 ASan/UBSan 바이너리(`/tmp/tfile-operations-sanitize-20261009`)로 detect_leaks=1을 요청해 같은 ptrace fatal error를 확인했다. 미디어 4개도 같은 제약으로 실패했다. 터미널 runner는 첫 terminal 검사에서 같은 오류로 중단돼 나머지 LSan 경로는 실행하지 못했다. 설정도 같은 ptrace fatal error였다. 그 뒤 위의 누수 검출 비활성화 실행을 별도로 했다. 보안 설정이나 suppression을 변경하지 않았다.

전체 검사 로그는 `/tmp/tfile-review-check.log`, sanitizer 요약 로그는 `/tmp/tfile-review-{san,media-san,terminal-san,settings-san}{,-fallback}.log`, operations LSan 로그는 `/tmp/tfile-review-operations-lsan.log`에 있다. 상세 로그는 각 output-directory 안에 있다. `/tmp` 산출물은 저장소에 포함하지 않는다. 실제 터미널의 이미지 잔상/한글/모달 육안 검사, Windows Terminal 검사는 이번에 수행하지 않았다.

`make check` 전체 검사도 **종료 코드 0으로 통과**했다. 기본 native/계층 검사, 설정, 터미널, 미디어(실제 변환 포함), UI native 및 일반 PTY, workflow/복구/일괄 작업 검사를 실행한 결과다. 일반 PTY 통과를 sanitizer 실행 또는 실제 그래픽 육안 검증으로 확대하지 않는다.
