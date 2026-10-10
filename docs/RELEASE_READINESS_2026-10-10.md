# 배포 준비 — 2026-10-10

## 기준과 보존한 구현

시작 소스 `73f63aa`, 작업 트리 clean. 프로젝트/상위 디렉터리에 AGENTS.md와 기존 GitHub workflow는 없었다. README, Makefile, USER_GUIDE, REFERENCE, 설치·설정 소유권·기능 진행·터미널 idle 기록과 실제 src/tests를 대조했다. 신규 제품 기능·광범위한 리팩터링·패키지 설치·전역 설정 변경·원격 푸시·릴리스는 하지 않는다.

이미 완료된 항목은 다시 구현하지 않았다:

- 휴지통 umask 수정 `fa7d198`: tests/trash_test.c의 0022/0077/0777 및 권한 실패 검사 → check-trash → check-isolated → make check.
- Vim 부모 종료 `0c63415`: editor_shutdown_pty.py의 부모 SIGTERM/SIGHUP, 편집기 신호·회수·termios 복원 검사 → check-vim → make check.
- idle 동기화 `73f63aa`: terminal_pty.py의 정상/지연 모달 복원 이후 idle 출력/CPU assertions → check-terminal → make check.
- 즐겨찾기·필터·마킹·충돌 건너뛰기는 기존 native/PTY 검사와 main/help/core 정책으로 확인했다. 교차 파일시스템 이동·덮어쓰기·롤백은 추가하지 않았다. 이전 PASS와 이번 직접 결과는 구분한다.

## 수정

README의 외부 편집/뷰어 미제공 설명, REFERENCE의 휴지통 부재 설명과 깨진 README 앵커를 수정했다. 자체 편집기와 외부 Vim, F8/Delete 영구 삭제와 t/F9 휴지통을 구분했다. 선택 의존성과 누락 안내, b/f 키를 추가하고 Vim의 상세 설계는 USER_GUIDE로 옮겼다. USER_GUIDE의 일괄 첫 오류 정책에 이름 충돌 선택 예외를 반영했다. 로컬 문서 링크와 주요 헤딩 앵커는 check-docs로 검사한다.

isolated_check.py는 기존 HOME/config 및 TFILE_* 제거에 data/cache XDG 경로도 격리한다. first_run.py는 빈 PATH에서 Vim/xdg-open 누락 안내까지 검사한다. clean_source_check.py는 git 추적 소스(.c/.h/.py와 Makefile)만 임시 디렉터리에 복사한다. 기존 tfile, native 바이너리, 설정, .git은 복사하지 않는다. 빌드는 호스트의 컴파일러/ncurses/terminfo를 사용하고 앱 실행은 선택 도구 없는 PATH로 검사한다. 이는 새 OS/컨테이너 설치 검증이 아니다.

기본 native sanitizer runner만으로 editor_shutdown_pty.py는 실행되지 않았다. terminal sanitizer runner가 앱을 빌드한 뒤 기존 terminal 5개에 Vim 정상 복귀·부모 종료·마킹 PTY 3개를 추가 실행하고 개별 로그를 남긴다. assertion 제거·재시도·실패 무시는 없다.

## 직접 검증

기존 Ubuntu 24.04.5 LTS x86_64, uid 1000 비 root, GCC 13.3.0, make 4.3, ncurses-dev 6.4, Python 3.12.3. Clang은 PATH에서 찾지 못해 로컬 미검증이며 시스템 패키지를 변경하지 않았다. 실제 WSL·새 OS/컨테이너·다른 배포판은 실행하지 않았다. 알려진 ptrace 제약 때문에 LSan은 반복하지 않고 미완료로 남긴다.

제품 src는 이번에 변경하지 않았다. 최신 문서/검사 변경에 대해 직접 실행한 결과는 다음과 같다. 전체 실행 중 추가한 인자 없는 시작 검사와 깨끗한 빌드 환경 축소는 최종 clean_source_check에서 별도 재검사했다.

| 검사 | 결과 | 로그 |
| --- | --- | --- |
| 격리 추적 소스 GCC 빌드 + first_run | PASS (exit 0). 빈 PATH, 임시 HOME/config/data/cache, 탐색·텍스트·생성/복사/이동/삭제, Vim/xdg-open/미디어 누락, 기본 인자 없는 실행 | `/tmp/tfile-release-clean-final-20261010.log` |
| 최신 전체 `make -j4 check` | PASS (exit 0), 권한 SKIP/경고/실패 없음. 휴지통 umask·Vim 부모 종료·idle 회귀 및 계층/문서 검사 포함 | `/tmp/tfile-release-check-final-20261010.log`, 종료 코드 `.exit` |
| native ASan/UBSan 25개 | PASS (exit 0), `--disable-leaks` | `/tmp/tfile-release-native-san-20261010.log`, `/tmp/tfile-sanitizers-release-20261010/*.log` |
| 설정 저장 ASan/UBSan | PASS (exit 0), 기본 detect_leaks=0 | `/tmp/tfile-release-settings-san-20261010.log` |
| media ASan/UBSan 5개 | PASS (exit 0), `--disable-leaks` | `/tmp/tfile-release-media-san-20261010.log`, `/tmp/tfile-media-sanitizers-release-20261010/*.log` |
| terminal/editor ASan/UBSan 8개 | PASS (exit 0), `--disable-leaks`; 정상/지연 idle, Vim 복귀/부모 종료, 마킹 포함 | `/tmp/tfile-release-terminal-san-20261010.log`, `/tmp/tfile-terminal-sanitizers-release-20261010/*.log` |
| 로컬 문서 링크·Python AST·계층·diff 공백·문서 shell 예시 | PASS | `check-docs`, `check_architecture.py`, AST, `git diff --check`, `bash -n` |
| workflow 로컬 설정 | PASS: PyYAML BaseLoader 파싱, trigger/permissions/job timeout/SHA 형식 검사, 모든 run에 bash -n | 원격 실행/actionlint 결과는 아님 |
| Clang | 미검증: PATH에서 없음 (검사 경로 exit 1) | `/tmp/tfile-release-clang-20261010.log` |

일반 전체 검사에는 실제 설치된 Vim의 편집/저장/복귀 PTY 및 ImageMagick/Poppler 변환 크기·배치 검사도 PASS로 기록됐다. 외부 도구 자체를 sanitizer로 계측한 결과나 실제 픽셀 표시 확인이 아니다. 이번 LSan 결과는 미완료이며 과거 사용자 LSan PASS를 최신 결과로 대체하지 않았다.

첫 전체 실행 `/tmp/tfile-release-check-20261010.log`는 사용자 turn 중단 뒤 종료 코드를 확인할 수 없어 PASS로 기록하지 않았다. 중단 후 새 전체 실행의 final 로그와 exit 0만 채택했다. assertion 실패를 숨기기 위한 재시도가 아니다. ASan/UBSan에서는 미디어 PTY의 전역 임시 경로 관찰이 서로 간섭하지 않도록 전체 검사 → media → terminal 순서로 실행한다.

재현 명령(이번 환경의 LSan 제외):

```sh
python3 tests/clean_source_check.py gcc
make -j4 check
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-release-20261010 --disable-leaks
python3 tests/isolated_check.py make check-settings-sanitize
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-release-20261010 --disable-leaks
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py \
  --output-directory /tmp/tfile-terminal-sanitizers-release-20261010 --disable-leaks
```

새 재현 시 출력 디렉터리는 새 이름으로 지정해 기존 로그를 보존한다.

## Linux CI

.github/workflows/linux.yml은 Ubuntu 24.04에서 push/pull_request/수동 실행에 연결한다. 기본 permissions는 contents: read, checkout은 persist-credentials: false. 배포·릴리스는 없고 별도 secrets를 요구하지 않는다. 일반 job(20분)은 GCC/Clang 격리 소스 빌드/first_run, 일반 빌드 및 전체 make check(계층/문서 검사 포함)를 수행한다. sanitizer job(20분)은 native, settings persistence, media, terminal/editor PTY를 각각 별도 로그로 실행하며 기본 LSan을 활성화한다. CI의 LSan 실패도 실패로 남긴다. 두 job 모두 비 root를 확인하고 sudo는 선언한 검사 의존성 설치에만 사용한다. Bash의 기본 -e -o pipefail로 tee 이전 실패도 전달한다. 실패 시 로그 artifact를 14일 보존한다. 선택 미디어 도구는 필요하지 않으며 fixture 검사와 실제 도구가 있는 경우의 조건부 검사는 로그로 구분한다.

외부 action 고정 정책은 기존에 없었다. 공식 [checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)의 커밋 `3d3c42e5aac5ba805825da76410c181273ba90b1`, [upload-artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2)의 커밋 `cf430e030ddbb5b0abf93d22962f4752f3646cd9`를 공식 latest 릴리스·커밋 페이지에서 대조해 SHA로 고정했다. 두 action.yml의 Node 24 런타임과 사용하는 입력(persist-credentials, path, retention-days)도 확인했다. 로컬 YAML/구조 및 run 스크립트 문법 검사를 수행한다. actionlint·GitHub runner 원격 실행은 아직 하지 않았다. GitHub CI 통과라고 주장하지 않는다. PTY/변환 출력은 실제 터미널 픽셀·잔상이나 데스크톱 휴지통 복원을 검증하지 않는다.

## 수동 확인

일반 WSL/Linux 셸에서 최신 체크아웃을 사용한다. 다음은 기본 LSan을 요청하며 시스템 보안 설정을 변경하지 않는다. 출력 디렉터리는 기존 결과와 섞이지 않도록 새 이름을 사용한다. 첫 명령 실패 시 로그를 확인하고 후속 단계는 중단한다.

```sh
(
set -e
make -j4 check
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-release-manual-20261010
python3 tests/isolated_check.py env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 make check-settings-sanitize
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-release-manual-20261010
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py \
  --output-directory /tmp/tfile-terminal-sanitizers-release-manual-20261010
)
```

마지막 runner는 sanitizer tfile을 생성하고 terminal/auto/Vim/부모 종료/marking PTY를 순서대로 실행한다. 부모 종료 검사만 명시적으로 추가 실행하려면 위 terminal runner로 바이너리를 생성한 후 다음 명령을 사용한다(isolated wrapper 뒤에서 TFILE_BINARY를 지정한다).

```sh
python3 tests/isolated_check.py env \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  TFILE_BINARY=/tmp/tfile-terminal-sanitizers-release-manual-20261010/tfile \
  python3 tests/editor_shutdown_pty.py
```

로그의 Python assertion 실패, sanitizer 오류, LSan 런타임/ptrace 제한을 구분한다. `--disable-leaks`로 실행하면 ASan/UBSan 확인이며 누수 검사는 아니다.

최소 육안 확인은 사용자 파일 대신 임시 디렉터리를 사용한다:

```sh
make
practice_dir=$(mktemp -d /tmp/tfile-practice-XXXXXX)
printf 'temporary text\n' > "$practice_dir/text"
./tfile "$practice_dir"
```

텍스트 미리보기·한글/긴 이름·50×9와 100×24·키보드/마우스, e로 실제 Vim 편집/복귀, b/f 및 Space/a/u 마킹을 확인한다. 별도 임시 파일로 F8 확인 취소와 t 휴지통 이동을 구분하고 데스크톱 도구에서 복원한다. Sixel 터미널과 선택 도구가 있다면 F7 Image display setup의 실제 표시/삭제, 리사이즈와 모달 복귀 후 잔상을 확인한다. 실제 WSL 설치·terminfo·선택 도구 설치부터의 검증은 아직 별도 필요하다.
