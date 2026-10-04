# 이미지 Auto 감지 및 검증 (2026-10-04)

기준 커밋은 `f53fcbe`, 검증한 구현 커밋은 `6ee6641`이며 Linux 작업 복사본에서 실제 코드와 실행 결과를 확인했다. 기존 미추적 파일 `q`는 변경하거나 커밋에 포함하지 않는다.

## 최종 동작

일반 `./tfile` 실행의 기본 Auto는 이미지가 들어갈 공간이 있는 미리보기에서 처음 PNG/JPEG/PDF가 필요할 때 `terminal_query`의 DA/셀 질의를 재사용한다. 후보는 `UiContext`가 소유하고 최대 700 ms 동안 입력을 비차단으로 처리한다. 응답 파싱 시에도 제한 시간을 검사하므로 UI의 다음 poll이 늦어져도 기한을 지난 응답을 후보에 적용하지 않는다. 지원 응답과 현재 셀 크기가 유효하면 시각 확인 질문 없이 해당 세션에 활성화한다.

지원 여부는 파일마다 다시 조회하지 않는다. 리사이즈에서는 현재 ioctl 셀 정보로 크기를 갱신하고, 없으면 필요한 미디어에서 셀 크기만 다시 조회한다. 측정값을 명시한 F7/환경변수의 셀 크기는 유지한다. 지원/셀 정보는 저장하지 않으며 새 프로세스는 새로 감지한다. Auto/Off 선호 저장 정책은 그대로다.

무응답·잘못된 응답·Sixel 없는 DA·셀 크기 실패·질의 실패·취소는 세션에 기록하고 F7 복구를 안내한다. 파일 변경/새로고침은 자동 재시도를 만들지 않는다. 명시적인 F7 진단 또는 Off → Auto 전환은 재시도 수단이다. PDF는 가능한 경우 텍스트로 대체한다. 도구 누락은 터미널 실패와 별도로 표시한다. Off에서는 자동 질의·변환·이미지 출력이 없으며 PDF 텍스트 대체와 사용자가 직접 시작한 F7 진단은 유지한다.

감지 중 파일 선택 변경은 세션 감지를 유지하고 현재 선택만 변환한다. 모달·작은 화면·목록 모드·Off·종료는 자동 capture를 종료한다. 모달을 열 때 진행 중인 변환을 취소/회수하고, 모달 안에서는 새 변환을 시작하지 않는다. 이전 그래픽 지우기와 선택/크기 검증은 기존 경로를 사용한다. 픽셀 상한이 같은 큰 화면 리사이즈에서도 cached modal 복원은 새 크기 준비가 끝나기 전 이미지를 출력하지 않는다.

## 회귀 검사와 검토

- `graphics_probe_test`: 최초 감지, 세션 재사용, Off, DA 미지원, 무응답, 셀 누락, 질의 쓰기 실패 주입, 모달/최소 화면/종료 취소, ioctl 및 셀 전용 재조회, 잘못된 환경변수 값과 명시적 기존 환경변수 보존.
- `terminal_input_test`: capture 기한 뒤 분할 응답 차단, 미등록 CSI 및 숫자 뒤 ~ 프레임 복구, 잘린 DA 뒤 새 방향키 프레임, Esc·C1·Unicode·마우스와 기존 잘못된 응답 격리.
- `terminal_auto_pty`: 텍스트만 볼 때 무질의, F7/환경변수 없이 PNG/JPEG/PDF 변환 시작, 파일 변경 시 단일 조회, 재시작, 리사이즈/셀 변화와 ioctl, Off 저장/재시작/명시적 Auto 전환, 무응답/미지원/잘못된/늦은 응답/셀 실패, 도구 누락과 PDF 텍스트, 빠른 선택·마우스·빠른 찾기, 모달의 한글/이모지/q 파일명 입력, 자식 회수, 최소 화면, 종료/신호.
- 각 유휴 구간은 1.1초 동안 출력 바이트·이미지 프레임·화면 지우기 횟수가 증가하지 않고 CPU 증가가 2 tick 이하인지 확인한다. 불완전 UTF-8 보관과 Esc 뒤 유휴도 검사한다.
- 검토 중 큰 화면의 모달 리사이즈에서 이전 캐시 프레임이 복원되는 경로를 별도 PTY로 재현했다. 크기 준비 전 출력 차단과 512×384 상한에 도달하는 두 화면 간 리사이즈 회귀를 추가했다. 잘못된 환경변수 셀 값은 자동 감지의 유효한 측정값으로 남지 않도록 초기화했다.

## 실행 결과

| 실행 | 최종 결과 | 로그 |
| --- | --- | --- |
| `make check` | PASS, exit 0. 격리 HOME/XDG에서 설정·터미널/Auto·core/platform·UI·PTY·연속 작업·실패 복구·일괄 작업 실행 | `/tmp/tfile-auto-full-final.log` |
| `ASAN_OPTIONS=detect_leaks=1 make check-settings-sanitize` | PASS, ASan/UBSan/LSan | `/tmp/tfile-auto-settings-sanitizers-complete.log` |
| `python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-auto-terminal-sanitizers-complete` | terminal/input/probe/F7 PTY/Auto PTY 모두 PASS, ASan/UBSan/LSan | 해당 디렉터리의 `terminal.log`, `input.log`, `probe.log`, `pty.log`, `auto_pty.log` |
| `python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-auto-media-sanitizers-complete` | media/preview/picker/media PTY 모두 PASS, ASan/UBSan/LSan | 해당 디렉터리의 검사별 로그 |
| `python3 tests/media_real.py` | 실제 ImageMagick PNG/JPEG 및 Poppler PDF 이미지/텍스트 변환 PASS. 손상/암호화/빈 페이지/도구 누락/상한의 기존 검사 포함 | `/tmp/tfile-auto-media-real.log` |
| diff 검토 및 `git diff --check` | PASS | 로컬 staged/final diff |

앞선 검토에서 확인한 샌드박스의 ptrace/LSan 제한을 피하려고 최종 sanitizer는 승인된 샌드박스 밖 실행으로 수행했다. `detect_leaks=1`을 유지했으며 `--disable-leaks`를 사용하지 않았다. 최종 각 로그에서 ASan/UBSan/LSan 오류와 검사 생략이 없음을 확인했다. 중간 검토에서 추가한 캐시 리사이즈 방어와 환경변수 초기화까지 반영한 코드로 전체 검사와 전용 sanitizer를 다시 수행한 결과다.

## 한계 및 수동 확인

지원 응답은 실제 픽셀 렌더링·삭제, 모든 터미널·폰트·배율 조합의 호환성을 입증하지 않는다. PTY는 프로토콜/입력/자식/출력 경계를 확인하며 실제 픽셀·잔상을 보지 않는다. 이번 실행은 Linux의 PTY이며 실제 사용자 터미널 및 WSL 터미널의 육안 확인은 수행하지 않았다. Windows 기능에 의존하는 구현은 추가하지 않았다.

입력 필터는 완료되거나 늦은 DA/셀 프레임을 소비하고, 잘린 보고서 본문을 일반 키로 재생하지 않는다. 불완전하거나 잘못된 보고서 뒤 응답 꼬리와 구별할 수 없는 일반 문자는 격리될 수 있다. 종료 문자 또는 새 ESC [/O·C1 CSI 프레임은 명시적 복구 경계이며, 방향키/F7 등 정상 제어 프레임 뒤 일반 입력을 처리한다. 새 프레임 경계를 가로질러 분리된 임의 꼬리나 표식 없는 잘못된 바이트를 완벽히 구별할 수는 없다. 이 정책은 자동 복구와 응답 격리의 모호성을 숨기지 않는다.

실제 터미널에서는 `./tfile /path/to/png-jpeg-pdf`로 기본 Auto 표시·파일 변경·리사이즈·모달 복원·종료 후 잔상을 확인한다. 표시가 없거나 잘못되면 F7의 Image display setup에서 재조회/측정값 입력과 표시·지우기·활성화 확인을 수행하거나 Off로 전환한다.

누수 검사 수동 명령(LSan이 실행 환경의 ptrace 제한으로 중단되면 누수 검사 통과가 아니다):

```sh
ASAN_OPTIONS=detect_leaks=1 make check-settings-sanitize
python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-auto-terminal-manual
python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-auto-media-manual
```

전용 스크립트는 기본적으로 `detect_leaks=1`을 요청한다. `--disable-leaks` 실행은 LSan 완료로 기록하지 않는다. 과거 환경변수 기반/F7 육안 확인 기록은 이번 Auto 검증과 구분하며 기존 변환기·리소스 검증은 [미디어 검증 기록](MEDIA_PREVIEW_VALIDATION.md)을 참고한다.
