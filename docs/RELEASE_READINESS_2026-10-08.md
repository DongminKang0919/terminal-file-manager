# 설치·첫 사용·실패 복구 점검 — 2026-10-08

## 범위와 환경

기준은 `4b111de`의 최신 구현이며 이번 수정 후 소스로 다시 빌드·검사했다. 이전 보고서의 통과 결과를 이번 결과로 사용하지 않는다. 이미지/PDF Fit·중앙 배치, 자동 지원 감지, 실제 조작 가능한 미리보기만 초점을 받는 정책, 이중 목록·모달 초점 정책과 파일 작업 안전 정책은 유지했다.

- 환경: Ubuntu 24.04.5 LTS, uid/gid 1000의 비 root 사용자.
- 컴파일러: `cc` GCC 13.3.0. make 4.3, ncurses 개발 패키지 6.4.
- 선택 도구: ImageMagick 6.9.12.98, Poppler 24.02.0.
- 빌드: 소스와 Makefile을 새 임시 디렉터리에 복사해 새 실행 파일을 생성. 기존 실행 파일이나 테스트 바이너리를 재사용하지 않았다.
- 빌드 환경: 임시 HOME/XDG, PATH=/usr/bin:/bin, C.UTF-8, TERM=xterm-256color만 전달. 사용자 설정·TFILE 변수·CPPFLAGS/CFLAGS/LDFLAGS 등의 개발 환경은 상속하지 않았다.
- 실행 검사: 또 다른 임시 HOME/XDG, 선택 도구가 없는 빈 PATH, 수동 그래픽 설정 변수 없이 실행. 인자 없는 실행은 현재 작업 디렉터리를 열고 설정 파일을 만들지 않는 것도 확인했다.
- 설치 패키지는 이 머신에 이미 설치된 상태였다. 완전히 새 OS에서 apt 설치를 수행한 결과는 아니다. README의 Ubuntu 명령은 실제 필요 헤더/라이브러리와 로컬 패키지 구성을 대조한 예시다.

필수 의존성은 C11 컴파일러·make·ncursesw 개발 라이브러리이며 실행에는 대화형 터미널과 terminfo가 필요하다. Python은 검사 도구용이다. ImageMagick/Poppler와 Sixel 지원은 선택 사항이다.

## 발견하고 수정한 문제

### 비대화형 실행이 종료하지 않음

수정 전 main으로 빌드한 실행 파일을 `TERM=xterm-256color`와 stdin=/dev/null, stdout=pipe로 실행했다. 0.8초 제한에 걸렸고 7,508바이트의 터미널 출력을 남겼다. 유효한 입력 터미널 없이 ncurses 입력 루프에 들어가 `ERR`를 반복 처리하는 경로였다.

이제 UI 초기화 전에 core → platform 공통 터미널 검사로 stdin/stdout의 TTY 여부를 확인한다. 리다이렉션 실행은 이유를 stderr에 출력하고 exit 1로 끝나며 stdout에는 제어 문자를 출력하지 않는다. TERM 누락·빈 값·dumb도 실행 전에 이유를 안내한다. 일반 터미널의 Sixel 지원 여부는 이 검사와 무관하며 기존 자동 감지를 유지한다. 알려지지 않은 terminfo 이름에 대한 ncurses의 기존 오류 처리도 유지한다.

`tests/first_run.py`는 stdin만/출력만/둘 다 리다이렉션한 경우와 TERM 누락·빈 값·dumb를 검사한다. 기존 startup native 검사는 터미널 검사를 래핑해 목록 1회 읽기/정렬 회귀를 계속 검사한다. OS 의존 코드는 platform 안에 유지한다.

### 전체 검사에 개발용 변수가 유입됨

기존 `isolated_check.py`는 HOME/XDG만 바꾸므로 `TFILE_BINARY=/missing` 같은 값도 상속했다. 이전 wrapper에서 이를 제거했다고 가정하는 자식 검사를 실행하면 exit 1이었다. 그래픽 수동 선언이나 테스트용 바이너리 선택이 개발자의 쉘 상태에 따라 기본 검사 결과를 바꿀 수 있었다.

전체 검사 wrapper가 상속된 `TFILE_*`를 제거하도록 수정했다. 개별 검사 내부에서 필요한 stub/실패 주입/다른 바이너리 변수를 지정하는 방식은 유지한다. make의 jobserver FD도 유지한다. 첫 실행 회귀에 오염된 HOME·TFILE_BINARY·Sixel 선언을 넣고 격리를 확인하는 검사를 추가했다. 다른 바이너리를 검사하려면 wrapper **안에서** 해당 변수를 지정해야 한다.

### 미디어 정리 검사가 다른 실행의 임시 디렉터리까지 비교함

별도 실제 변환 검사와 전체 검사를 동시에 실행했을 때 `media_pty.py`의 `/tmp/tfile-media-*` 전역 snapshot 비교가 실패했다. 다른 검사에서 디렉터리를 만들거나 제거해도 영향을 받는 검사 격리 문제였다.

stub 변환기가 해당 앱에서 전달받은 전용 임시 경로를 기록하고, 정리 검사는 이 경로들만 확인하도록 수정했다. 자식 종료·실제 작업 디렉터리 제거 검증을 유지하며, 관계없는 미디어 디렉터리가 존재하는 조건도 회귀에 넣었다. 다른 프로세스의 파일을 읽거나 제거하지 않는다.

### 자동 감지/빠른 찾기 검사의 화면 갱신 타이밍

전체 검사 재실행에서 자동 감지 타임아웃 메시지는 읽었지만 `Find:` 행은 아직 없는 순간을 검사하는 실패가 발생했다. 빠른 찾기는 공통 `draw()`의 refresh 후 자체 입력 행을 덧그려 다시 refresh하므로 PTY는 중간 화면을 읽을 수 있다. 지연 실행 실험에서도 입력 유실 자체는 재현하지 못했으므로 이를 제품 입력 버그로 기록하지 않는다.

검사 도구는 고정된 80ms에 의존하지 않고 초기 화면이 준비될 때까지 기다리도록 보완했다. 빠른 찾기 검사는 타임아웃 안내와 `Find:`가 함께 있는 완성된 상호작용 화면을 기다리며, 빠른 찾기가 실제로 종료된다면 여전히 실패한다. 250ms 지연 launcher 회귀도 추가해 초기 입력·타임아웃 후 늦은 응답·텍스트 선택을 확인한다. 제품 입력 정책은 바꾸지 않았다.

### 첫 사용자 안내가 기술 기록에 묻힘

README의 이미지 프로토콜·설정 형식·검증 설명이 기본 작업보다 앞에 길게 놓여 있었다. README는 필수 의존성 → 빌드/실행 → 기본 파일 작업 → 선택 미리보기 → 설정/안전 정책 순서로 줄였다. 자세한 기존 내용을 [상세 사용·개발 안내](USER_GUIDE.md)에 보존하고 개별 설계·검증 문서로 연결했다.

대표 이미지는 이전에 실제 확보한 화면임을 명시했다. 이번 작업에서 새 그래픽 화면을 확보하지 않았으며 가상의 전후 화면을 만들지 않았다. 미구현 install 명령, PDF 페이지 이동·확대, 교차 파일시스템 이동, 덮어쓰기 등을 지원하는 것처럼 안내하지 않는다.

## 이번 검증

전체 검사 결과는 최종 실행을 기준으로 기록한다.

| 검사 | 결과와 범위 |
| --- | --- |
| 새 임시 환경 빌드 + 새 바이너리의 first_run.py | 통과. 기본 작업은 선택 도구가 없어도 실행됨 |
| 인자 없는 실행의 별도 PTY 검사 | 통과. 현재 디렉터리 표시, 정상 종료, 설정 파일 자동 생성 없음 |
| first_run.py | 통과. 생성·마우스 삭제 취소·명시적 삭제, 기본/빈/바이너리 미리보기, 도구 누락·미지원·Off 구분, 터미널 입력/출력 검증, 검사 환경 격리 |
| 전체 `make -j4 check` | 통과 (exit 0). 관련 native·PTY 검사 전체; 권한 검사 SKIP 없음 |
| ASan/UBSan native 17개 | 통과. LSan 비활성; 누수 검사 통과를 뜻하지 않음 |
| 실제 ImageMagick/Poppler `media_real.py` | 통과. PNG/JPEG·첫 페이지 이미지/텍스트·빈/스캔/암호화/손상 PDF·입력 상한·도구 누락 |
| 의존 계층 검사 / diff 공백 검사 | 통과 |

자동 검사는 실제 실행 파일을 ncurses PTY에 연결해 키보드·SGR 마우스를 입력하고 화면 셀과 파일시스템 결과를 확인한다. **물리/그래픽 터미널 육안 검사와 다르다.** 변환기의 실제 출력 검사도 Sixel의 실제 표시·잔상을 입증하지 않는다. 아래는 최종 전체 검사에서 확인한 흐름이다.

- `workflow_pty.py`: 50×9·80×24·160×32에서 탐색/방문 위치, 검색 결과 열기, 여러 항목 마킹, 이중 목록 복사·이동, 결과 상세, 생성·삭제, 여러 모드와 리사이즈/모달 복귀.
- `history_pty.py`, `search_ui_pty.py`, `navigation_pty.py`: 방문 커서/스크롤 복원, 검색/목록 입력, 키보드·마우스 탐색. 마킹은 디렉터리 이동 시 해제되는 기존 정책을 유지한다.
- `settings_pty.py`: 옵션 변경만으로 저장하지 않음, 명시적 저장과 재시작, 잘못된 설정 경고와 확인 후 교체. 50×9·100×24.
- `recovery_pty.py`, `dual_transfer_pty.py`, `batch_destination_pty.py`: 실제 비 root 권한 거부, 사라진 원본/목적지, 이름 충돌, 입력 유지·수정 후 재시도, 자체 하위 전송 거부, 모달/리사이즈 취소.
- `batch_pty.py`, `batch_progress_pty.py`, native operations: 실패/취소/부분 처리·미실행 결과, 이미 완료된 변경과 마킹 정책, 삭제/복사 안전성. 사용자 파일을 사용하지 않음.
- `terminal_auto_pty.py`, `terminal_pty.py`, `media_tools.py`, `media_pty.py`: 자동 지원 감지·무응답·도구 누락·Off·변환 실패·늦은 결과 취소, 선택 전환·팝업·최소 화면 이하 축소.
- `preview_focus_pty.py`, `ux_consistency_pty.py`: 실제 스크롤이 가능한 경우에만 초점/안내, 정적 상태 위 클릭/휠과 목록 보존, 긴 오류, 리사이즈 초점 복귀, 이중 목록/모달 입력.
- `media_fit_pty.py`, `media_real_fit.py`: 세로·가로·정사각·작은/큰 이미지·PDF, 좁고 넓은 화면과 연속 리사이즈. 후자는 실제 변환 출력 크기와 중앙 좌표를 확인하며 육안 결과는 아님.

실행 명령과 로그:

```sh
make -j4 check > /tmp/tfile-release-check-final-20261008.log 2>&1
python3 tests/isolated_check.py python3 tests/media_real.py \
  > /tmp/tfile-release-real-media-20261008.log 2>&1
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-first-use-20261008 --disable-leaks \
  > /tmp/tfile-release-sanitizers-20261008.log 2>&1
```

최초 전체 검사 로그는 `/tmp/tfile-release-check-20261008.log`다. 별도 실제 변환 검사를 동시에 실행해 `media_pty.py`의 `/tmp/tfile-media-*` 전역 목록 정리 비교가 간섭받았다. 이를 제품 정리 누수로 판정하지 않았으며, 이어 재실행에서 발견한 빠른 찾기 중간 화면 샘플링 문제를 검사 도구에서 보완하고, 다른 변환 작업 없이 전체 검사를 새로 실행한 최종 로그는 `/tmp/tfile-release-check-final-20261008.log`다. 개별 검사의 과거 통과를 합쳐 전체 통과로 대신하지 않는다.

새 소스 빌드 로그는 `/tmp/tfile-clean-build.log`다. 이번 LSan은 기존 문서에 기록된 ptrace 환경 제약 때문에 같은 조건으로 재실행하지 않았다. 최신 소스의 누수 검사는 미완료다. ptrace가 없는 환경에서 다음 명령을 실행하고 LSan 결과를 별도로 확인해야 한다.

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-release-manual-20261008
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-release-manual-20261008
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py \
  --output-directory /tmp/tfile-terminal-sanitizers-release-manual-20261008
```

## 남은 문제와 배포 판단

### 배포 전 필수 수정/확인

- 발견한 실행/검사 환경·화면 샘플링·임시 디렉터리 격리 문제는 수정했고, 최종 전체 검사를 통과했다. 이번 자동 검사와 정적 확인에서 추가 필수 코드 수정 사항은 발견하지 않았다. 아래 미검증 범위까지 배포 검증 완료로 확대하지 않는다.
- 지원을 표방할 주력 그래픽 터미널에서 육안 확인이 필요하다. 한글/긴 이름, 50×9/100×24, 실제 키보드/마우스, 이미지 전체 표시·중앙 배치·리사이즈·모달 이후 잔상을 점검한다. F7 이미지 진단의 표시/삭제 확인도 수행한다. 자동 PTY로 대체할 수 없다.
- 다른 Linux 배포판, 실제 WSL, SSH·tmux/screen·각종 그래픽 터미널은 이번 호환성 검증 완료 대상이 아니다. 이를 지원 검증 완료로 광고하지 않는다.

### 이후 개선


- 설치 패키지/`make install`은 제공하지 않는다. 현재 배포 방식은 문서의 소스 빌드 + 실행 파일이며 패키징은 후속 작업이다.
- Sixel만 지원하며 그래픽 passthrough·PDF 페이지 이동·확대/축소는 제공하지 않는다. 별도 기능 없이 현 안내를 정확하게 유지했다.
- 교차 파일시스템 이동·덮어쓰기/병합·실패 롤백은 지원하지 않는다. 이번에는 기존 안전 정책을 유지하고 첫 사용자 안내에 제한을 앞당겨 명시했다.
- 긴 진단 필드에는 기존 길이 제한이 있어 이미 잘린 내용은 상세 창에서도 복원하지 못한다. 자동 재시도/잔여물 제거도 없으므로 부분 실패는 상세 결과와 파일 목록에서 확인한다.
