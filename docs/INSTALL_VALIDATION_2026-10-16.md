# 설치·제거·첫 실행 검증 (2026-10-16)

## 기준과 기존 흐름

기준 커밋은 `1ce2ecf`이며 시작 시 작업 트리는 깨끗했다.
해당 커밋의 [Linux CI 38866794112](https://github.com/DongminKang0919/terminal-file-manager/actions/runs/38866794112)는
GitHub API로 completed/success를 확인했다. 이는 이번 설치 변경의 원격 결과가 아니다. Makefile에는 제품 빌드와
개발 검사만 있었고 install/uninstall은 없었다. README도 `make install` 미제공을 명시했다.
실행 파일은 동적으로 libncursesw.so.6, libtinfo.so.6, libc/loader를 사용한다.
도움말은 src/ui/help.c에 포함되고 설정·즐겨찾기는 HOME/XDG에서 찾는다.
미디어 도구와 Vim/xdg-open은 PATH에서 탐색한다. 제품이 저장소의 문서·리소스를 여는 경로는 없다.

기존 first_run은 임시 HOME/XDG와 빈 PATH에서 도구 부재·기본 작업·정상 시작·non-TTY/TERM
거부를 검사했다. settings_pty, media_pty, terminal_auto_pty도 이미 있어 이를 재사용했다.
first_run 마지막의 테스트 드라이버 호출만 cwd 상대 경로였으므로 `__file__` 기준으로 고쳤다.
이는 테스트의 저장소 의존성이며 제품 의존성은 아니다. README의 연습용 Copy now는 현재
버튼 Copy 1 item으로 정정했다. 제품의 파일 작업·비동기·오류 처리 정책은 변경하지 않았다.

## 설치 계약

- `PREFIX ?= /usr/local`, `DESTDIR ?=`, `INSTALL ?= install`.
- `make install`은 빌드한 tfile 하나를 `$(DESTDIR)$(PREFIX)/bin/tfile`로 설치한다.
  디렉터리와 실행 파일은 명시적 0755. 공백이 있는 경로는 shell quoting한다.
- `make uninstall`은 같은 경로의 파일 하나만 `rm -f --`로 제거한다. 빌드/소스가 필요 없다.
  Makefile은 필요하지만 bin·PREFIX 전체·사용자 설정을 지우거나 디렉터리를 재귀 삭제하지 않는다.
- 재설치와 이미 제거된 파일의 재제거를 지원한다. PREFIX/DESTDIR은 설치 때와 같게 지정한다.
- 설정·즐겨찾기·문서·아이콘·man page·외부 도구·셸 설정을 설치하거나 변경하지 않는다.
  내장 F1 도움말은 별도 설치 없이 사용할 수 있다. 외부 상세 문서는 저장소/온라인에서 읽는다.
- 앱 실행 시 ncursesw 런타임, 맞는 terminfo, Linux 대화형 터미널이 필요하다.
  실행 위치에 따라 목록의 기본 경로는 cwd이며 상대 경로도 cwd를 기준으로 한다.

## 재현 가능한 설치 검사

`make check-install`은 `tests/clean_source_check.py $(CC)`를 실행한다. 기존 CI의 GCC/Clang
clean-source 단계에도 설치 검사가 이어지며 별도 중복 빌드 job을 추가하지 않았다.
기존 로그 `/tmp/tfile-ci-logs/clean-gcc.log`, `clean-clang.log`와 실패 artifact 수집을 사용한다.
CI 단계 이름에 install/uninstall을 명시했다. 보안·권한·timeout·병렬도는 유지한다.

clean-source는 현재 소스 스냅샷에서 Makefile, src/tests의 C·헤더·Python만 복사한다.
추적·비무시 미추적 소스를 포함해 커밋 전 새 검사도 검증하며 빌드 산출물·.git·설정은 복사하지 않는다.
새 환경을 부팅한 검사는 아니며 호스트의 컴파일러·헤더·라이브러리·terminfo를 사용한다.

`tests/install_check.py`는 clean-source가 소유한 임시 복사본 안에서만 다음을 수행한다.

1. README의 `make`, `./tfile`에 해당하는 빌드와 기존 first_run 실행.
2. 임시 HOME의 공백 포함 `.local` PREFIX 설치. bin/tfile만 생성됐는지, restrictive umask 077에서도
   명시적 실행 권한 0755인지, 내용 SHA256이 빌드 파일과 같은지 검사.
3. 연속 재설치, 제거 후 재설치. 기본 `/usr/local`은 DESTDIR의 stage root 아래에만 설치.
   별도 PREFIX `/opt/tfile test`와 DESTDIR의 조합도 정확한 파일 경로로 검사.
4. 검사용 Python 드라이버와 제거용 Makefile만 외부에 보존하고 **소스·빌드 디렉터리를 삭제**.
   저장소 밖의 별도 cwd에서 설치 바이너리로 first_run/settings_pty/media_pty/terminal_auto_pty 실행.
   설치 bin을 PATH에 추가한 환경에서도 tfile 명령이 그 파일을 찾는지 검사.
5. staged 바이너리도 절대 경로로 first_run을 실행. DESTDIR을 실행 시 환경으로 요구하지 않음.
6. 보존한 Makefile로 소스 없이 user-prefix/default-stage 각각 두 번 제거.
   설정·즐겨찾기·XDG data/cache·PREFIX/bin의 무관한 파일·stage 표식이 바이트 그대로 남는지 검사.
   설치 바이너리만 사라지고 디렉터리는 남아야 함.

기존 first_run의 assertion은 유지하고 F1 내장 도움말, 잘못된 경로/일반 파일 경로의 exit 1과
terminal mode 복원, 누락 도구별 Install 안내를 추가했다. 새 검사는 자동화된 PTY 검사이며
실제 사람이 터미널의 색·폰트·Sixel 픽셀·잔상을 확인한 결과가 아니다.

## 첫 실행 정책과 선택 의존성

| 조건 | 유지·검증한 동작 |
| --- | --- |
| 설정 없음 | 기본 목록+미리보기로 시작, 설정 경고/저장/마법사 없음. 명시적 저장 때만 설정 생성 |
| Vim 없음 | Vim unavailable/install vim 안내. 대체 편집기/셸 실행 없이 탐색 계속 |
| xdg-open 없음 | Missing xdg-open/install xdg-utils. 외부 열기만 불가, 기본 작업 유지 |
| ImageMagick 없음 | Missing preview tool과 Install ImageMagick. 손상 파일/변환 실패와 구별 |
| Poppler 없음 | Missing pdftotext/pdftoppm, Install Poppler. PDF 이미지/텍스트 가능 여부를 구별 |
| 지원 미확인/응답 없음/no Sixel | 자동 감지 상태·F7 진단/재검사 안내. 도구 발견과 Sixel 지원을 별도 취급 |
| 도구 있음 | 모의 magick/Poppler로 PNG/JPEG/PDF·정상 완료·취소·실패·외부 프로세스 정리 검사 |
| 잘못된 시작 경로/일반 파일 경로 | 다른 디렉터리로 대체하지 않고 endwin 후 stderr 원인, exit 1, termios 복원 |
| non-TTY/잘못된 TERM/인자 초과 | 거부, exit 1. 정상 종료는 0 |

도구가 있어도 coder/정책/데스크톱·뷰어 설정 때문에 실제 기능이 실패할 수 있다.
Sixel 프로토콜 응답을 테스트가 제공한 것을 실제 터미널 호환성으로 확대하지 않는다.

## 실제 실행 환경과 결과

Ubuntu 24.04.5 LTS의 기존 작업 환경, GCC/cc 13.3.0, Python 3, 호스트 ncursesw·terminfo 사용.
로컬에 Vim/convert/Poppler는 있고 xdg-open/magick/Clang은 없었다.
설치 검사는 optional-tool 부재를 빈 PATH로, 도구 존재를 모의 실행 파일로 제어한다.
이전 실제 도구 검사 결과를 최신 설치 바이너리의 실제 이미지 표시 확인으로 확대하지 않는다.
시스템 패키지를 설치·제거하거나 사용자 실제 HOME/XDG·PREFIX를 변경하지 않았다.
새 OS/컨테이너/클린 배포판 설치와 sudo 시스템 설치는 미실행이다.

| 명령 / 구분 | 실제 결과 | 시간·로그 |
| --- | --- | --- |
| `make check-install` (cc/GCC 13.3.0) | PASS, exit 0; 소스 빌드와 설치 후 4개 PTY suite, staged first_run | 98.56초, 최대 단일 RSS 47876 KiB; `/tmp/tfile-install-verified.log`, `.time` |
| `python3 tests/clean_source_check.py gcc` (최종 코드) | PASS, exit 0; HOME 기본 설정과 XDG 설정 표식 모두 보존 | 98.46초, 최대 단일 RSS 47852 KiB; `/tmp/tfile-install-gcc.log`, `.time` |
| `python3 tests/clean_source_check.py clang` | 미실행: 로컬 Clang 없음, 명시적 오류로 종료하며 성공 처리하지 않음 | `/tmp/tfile-install-clang.log` |
| 문서 링크·architecture·Python 구문·diff | PASS | `check_docs.py`, `check_architecture.py`, `py_compile`, `git diff --check` |
| 새 OS 패키지 설치 / sudo 시스템 설치 / 실제 WSL·Sixel 픽셀 | 미실행 | 임시 경로·자동 PTY 결과와 구분 |
| 이 변경의 원격 CI / 전체 회귀 / sanitizer 재실행 | 미실행 | 설치·Makefile·테스트·문서 변경 범위에 맞춰 관련 검사 실행 |

선택 도구가 없는 경로와 모의 도구가 있는 경로를 모두 통과했다. 기존 settings_pty는
50×9/100×24 설정 저장·재시작을 검사하고, media_pty는 PNG/JPEG/PDF와 취소·실패·임시 파일 정리,
terminal_auto_pty는 지원 미확인/응답 없음/no Sixel·재검사 등을 검사했다.
전체 설치 검사 시간은 약99초이며 기존 CI의 컴파일러별 단계에 통합했다. 새 job·병렬도 조정이나
timeout 증가는 하지 않았다. 위 RSS는 동시 프로세스 합산 peak가 아니다.

[실행 출력·검사 소스 SHA256](INSTALL_EVIDENCE_2026-10-16.txt)을 보존했다.

제품 C 코드·파일 정책·worker 구조에 변경이 없으므로 새 메모리 경로는 없다.
이번 설치 검사를 위해 전체 회귀와 sanitizer를 다시 실행했다고 주장하지 않는다.
기존 수동 WSL sanitizer 기록은 당시 전달된 산출물 범위이며 현재 커밋의 검증으로 옮겨 쓰지 않았다.
CI에 연결한 것과 이 변경의 원격 CI가 실제 실행된 것은 구별한다.

## 검증한 명령과 수동 사용성 점검

```sh
make
make install PREFIX="$HOME/.local"
"$HOME/.local/bin/tfile" /path/to/directory
make uninstall PREFIX="$HOME/.local"
make install PREFIX=/usr/local DESTDIR="/tmp/tfile-example-stage"
make uninstall PREFIX=/usr/local DESTDIR="/tmp/tfile-example-stage"
make check-install
# CI는 각각 동일한 clean-source + install 검사를 실행
python3 tests/clean_source_check.py gcc
python3 tests/clean_source_check.py clang
```

위 PREFIX의 HOME과 DESTDIR은 자동 검사에서 전부 임시 경로로 치환했다.
README의 apt/sudo 명령은 Ubuntu 설치 예시이며 이번에 실행하지 않았다.
실제 사용자는 다음을 짧게 확인해 주면 된다.

- 사용자 PREFIX 설치 후 저장소를 벗어나 절대 경로로 실행하고 F1 도움말·F10 종료 확인.
- PATH에 bin이 없다면 절대 경로 안내만으로 실행할 수 있는지 확인.
- 도구 없는 상태의 안내로 다음 행동을 이해할 수 있는지, 필요한 도구 설치 후 F7 재검사가 가능한지 확인.
- 자신이 사용하는 터미널에서 한글·마우스·이미지의 실제 표시/지우기 확인.
- 같은 PREFIX로 제거한 뒤 실행 파일만 사라지고 기존 설정·즐겨찾기가 유지되는지 확인.
