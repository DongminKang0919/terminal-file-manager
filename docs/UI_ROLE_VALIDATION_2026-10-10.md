# UI 역할 표시 검증 — 2026-10-10

## 범위와 유지한 정책

README, 사용자 안내·구조 참고서, 기존 UI/PTY 검사와 CI 검사 명령을 확인했습니다. 저장소와 상위 디렉터리에서 별도의 AGENTS.md는 발견되지 않았으며 .agents/.codex에도 추가 지침 파일이 없었습니다. 작업 시작 시 git 작업 트리는 깨끗했습니다.

UI → core → platform 의존 방향과 파일 작업 안전성 정책을 유지합니다. 변경은 UI 테마·렌더링·안내 문구, 해당 검사와 문서에 한정합니다. 신규 파일 관리 기능, 비동기/멀티스레드, 긴 작업의 입력 반응·취소 지연 측정과 병목 개선은 이번 범위에 포함하지 않습니다.

## 표시 규칙

- 명령 키: 청록·굵기, 활성 패널: 파랑 테두리·제목의 기존 `*`, 커서: 파랑 행과 `>`, 마킹: 노랑·굵은 고정 `*`, 주의: 노랑, 오류: 빨강. 선택 행은 파일 종류 색보다 대비를 우선합니다. 8색/무색에서는 기호·문구·굵기·반전을 유지합니다. 무색 기본 명령줄 전체의 반전은 제거했습니다.
- 상단 명령은 탐색 / 파일 작업 / 설정·종료 묶음으로 표시하고 좁은 화면에서는 명령 전체를 생략합니다. Help·Search·F9 Menu·Quit는 최소 크기에서도 표시합니다. 같은 action_bounds 계산으로 출력과 클릭 판정을 합니다.
- 경로 생략과 비활성 방문 버튼, 목록 보조 열, 미리보기 메타데이터 구획, 팝업 공통 프레임·버튼 hit-test는 기존 구현을 재사용합니다.
- 작업 대상은 활성 목록 아래 경계에 고정합니다. 좁은 긴 목록에서는 `>cursor` / `*marks`와 범위로 줄이고 상태줄의 Marked 개수를 유지합니다. 알림 중에도 대상 경계와 목록 수를 남깁니다. e/Vim은 커서 대상임을 표시하고, 디렉터리·빈 목록·판별된 바이너리/미디어에서는 해당 하단 안내를 생략합니다. 필터 모드도 공통 키·설명 렌더링을 사용합니다. 빈 목록의 마킹/해제 안내는 Target: none으로 일치시킵니다.
- 디렉터리는 Directory / Enter to open, 빈 디렉터리·빈 파일은 별도 안내, 미디어 변환은 Converting preview, 도구 누락은 Missing preview tool로 구분합니다. 미지원 이유와 실제 실패 이유도 구분하며 상태 안내를 새로 초점 대상으로 만들지 않습니다.
- 팝업 공통 제목은 초점과 같은 파랑 계열을 사용하고 8색 주의 색을 노랑으로 맞췄습니다. 기존 여백·경계·배경 흐림·Cancel 기본값·입력 보존·복원은 유지합니다.

## 자동 검사

자동 PTY는 실제 터미널의 색상·Sixel 픽셀·깜빡임에 대한 육안 검증이 아닙니다.

| 검사 | 결과와 로그 |
| --- | --- |
| 전체 회귀 `make -j4 check` | PASS; `/tmp/tfile-ui-check-final-verified.log` |
| ASan/UBSan 기존 25개 대상 | PASS (`--disable-leaks`); `/tmp/tfile-ui-sanitizers-final.log` |
| 256색/8색/무색 UI 셀 sanitizer | PASS; `/tmp/tfile-ui-sanitizers-style.log` |
| 미디어 ASan/UBSan | preview/picker/media PTY/redraw PASS; media native 단독 재실행 PASS. `/tmp/tfile-ui-media-sanitizers.log`, `/tmp/tfile-ui-media-sanitizers-serial.log` |
| LSan | 미완료: 요청 실행에서 ptrace 제약으로 fatal error. `/tmp/tfile-ui-sanitizers.log`, `/tmp/tfile-sanitizers-ui-20261010/*.run.log` |
| 문서 링크·아키텍처·공백 | PASS: `tests/check_docs.py`, `tests/check_architecture.py`, `git diff --check` |

8색/무색 셀 검사와 50×9·80×24·100×24·160×32 PTY 검사는 표시 범위, 명령·팝업 클릭 범위, 키 안내·대상 경계의 비클릭 동작, 선택된 숨김 파일의 대비, 커서 이동 후 고정 마킹, 알림 중 대상 경계, 이미지/PDF, 리사이즈와 복원을 포함합니다. 기존 terminal_pty/terminal_auto_pty 유휴 검사와 media_redraw_measure 검사를 유지했습니다. 100×30 프로토콜 계측에서 idle의 PTY 출력·Sixel 전송·변환 프로세스는 모두 0이며, 마킹 on/off와 상태줄만 바뀐 경우에도 추가 Sixel 전송·변환은 0이었습니다. 이 수치는 긴 작업 입력 반응이나 취소 지연 측정이 아닙니다.

중간 실행에서는 새 상태 제목과 상단 순서·좌표에 대한 기존 기대값을 수정했습니다. stale 패널 검사에서는 상단 Copy/Move 명령을 팝업으로 오인하지 않도록 제목이 존재할 수 있는 모든 행(1행 이후)을 계속 검사합니다. 미디어 native sanitizer의 `/tmp` 정리 개수 검사는 다른 미디어 검사와 겹쳐 한 번 실패했고, 모든 다른 실행이 종료된 뒤 같은 바이너리·조건으로 단독 재실행하여 PASS했습니다. 연속 작업의 복사 완료 화면은 마킹 해제에 맞춰 Target: cursor로 바뀐 경계를 정확히 검증하고, 기존 파일 행과 모든 테두리 셀의 비교를 유지합니다. 정리·복원·초점·클릭·대상 확인 조건을 완화하지 않았습니다. LSan 환경 실패와 ASan/UBSan 성공은 별도 결과입니다.

재현 명령:

```sh
make -j4 check
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --disable-leaks --output-directory /tmp/tfile-sanitizers-ui-repeat
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --disable-leaks --output-directory /tmp/tfile-media-sanitizers-ui-repeat
```

sanitizer와 일반 미디어 검사는 `/tmp` 개수 검사의 실행 간섭을 피하도록 순서대로 실행합니다. 실제 누수 검사에는 ptrace 없는 환경에서 `--disable-leaks`를 빼고 실행해야 합니다. `/tmp` 로그는 저장소에 포함하지 않습니다.

## 실제 터미널에서 확인할 최소 항목

1. 50×9와 100×24에서 명령 키·설명, 파랑 커서·활성 패널, 노랑 마킹을 확인하고 8색/무색에서도 `>`와 `*`가 구분되는지 확인합니다.
2. 긴 한글 이름·경로, 이중 패널 Tab, 마킹 0/1/여러 개에서 작업 대상과 e/Vim 커서 예외를 확인합니다. 알림이 떠도 대상 경계와 Marked 수가 남아야 합니다.
3. PNG/PDF 정상 미리보기 → 도움말/옵션/복사 팝업 → 닫기·리사이즈를 반복해 실제 이미지 복원·잔상·깜빡임을 확인합니다. 유휴 상태의 불필요한 재전송도 실제 터미널에서 확인합니다.
4. 디렉터리·빈 파일·도구 누락 안내, 오류 후 입력 보존, 영구 삭제/Trash 문구와 기본 Cancel을 확인합니다.

[동일 fixture의 실제 PTY 변경 전후](UI_ROLE_COMPARISON_2026-10-10.md)를 제공합니다. 색상과 픽셀은 기록에 표현하지 않았습니다.
