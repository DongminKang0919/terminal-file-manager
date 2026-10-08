# 이미지·PDF 미리보기 1단계 검증 (2026-10-04)

## F7 내부 터미널 확인 추가 (2026-10-04)

이 절은 최신 구현이다. 아래 이전 1단계·후속 사용자 실험 기록의 “앱이 질의하지 않음”은 당시 구현 설명이며 현재 F7 명시적 진단에는 해당하지 않는다. 최신 사용자 흐름과 설정 구분은 [README](../README.md#이미지pdf-미리보기)를 참고한다.

- 최신 설정 저장(`94b696a`, `3d4c92c`), graphics_init, ncurses 입력, 미디어 변환·취소/회수, tools/sixel_probe.py와 기존 media/native/PTY 검사를 먼저 확인했다.
- F7 Image display setup은 현재 상태와 ImageMagick/Poppler 실행 파일 발견 여부를 따로 표시한다. 명시적 Enter 질의 또는 실제 측정한 M 수동 입력만 진단을 시작한다. 자동 시작 질의·테스트 출력·설정 저장·패키지 설치는 없다.
- core/terminal 모듈은 파일 작업과 분리된 순수 DA/셀 응답 검증과 platform 호출을 담당한다. native tty/query/ioctl/PATH 접근은 platform, ncurses 입력 프레임 구분과 육안 질문은 UI에 있다. Python은 제품 의존성이 아니다.
- DA `CSI ? class ; features c`의 기능 번호 4를 확인한다. 첫 필드는 terminal class이므로 단독 `?4c`를 Sixel로 해석하지 않는다. 셀 응답은 `CSI 6 ; height ; width t`이고 현재 ioctl 정보도 유효 범위 안에서 사용한다. 128바이트, 숫자 5자리, DA 32필드, 폭 1–64/높이 1–128 제한을 검사한다. 후보에 완전한 유효 응답만 반영한다. 규약은 [xterm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)를 확인했다.
- 질의는 700 ms, 기존 변환기 회수 대기는 1초로 제한하고 입력 호출의 시간·처리량도 제한한다. 응답 없음/잘못된 응답은 자동 확인 미완료로 안내한다. 이후 명시적 수동 측정으로 같은 표시·지우기 검증을 시도할 수 있다.
- 고정 24×12 빨강/파랑 Sixel은 ImageMagick/Poppler 없이 출력하며 기존 graphics_validate로 검증한다. 표시 영역의 `|` 표식, 별도의 지우기 질문, 마지막 세션 활성화 질문을 각각 확인해야 지원·셀 크기·Auto가 적용된다. Esc/n/리사이즈/실패는 이전 유효 값을 보존한다. 리사이즈는 픽셀을 지우고 자식·부모의 오래된 팝업을 닫는다.
- 응답 필터는 최초 명시적 확인 후 종료까지 유지되며 임시 후보 포인터는 질의 종료 때 해제한다. 부분 프레임과 overflow는 팝업·타임아웃을 넘어 보관하고 늦은 응답은 단축키로 실행하지 않는다. 7비트/8비트 CSI를 바이트 단위로 구분하고 UTF-8로 입력한 일반 문자와 혼동하지 않는다. 일반 Unicode/키/SGR 마우스는 프레임 밖에서 전달한다. 일괄 flush나 새 termios 모드 변경으로 응답 혼입을 숨기지 않는다.
- ESC 자체와 느린 응답의 첫 바이트는 근본적으로 모호하므로 질의 중 100 ms, 그 밖에는 25 ms 여유 후 취소 키를 전달하면서 프레임 시작은 계속 기억한다. 끝나지 않은 DA/셀 보고서 본문과 구별할 수 없는 일반 문자는 종료 문자까지 격리한다. Esc/리사이즈/신호 종료는 유지한다. 매우 느린 단독 ESC 분할은 진단을 취소할 수 있으나 나머지는 실행하지 않는다.
- 커서·Sixel scrolling mode를 저장/복원하고 기존 ED2 → curses 강제 재그리기 순서로 지운다. F7 모달 깊이·초점·목록 선택은 유지한다. 변환기는 기존 비차단 취소·회수 경로로 닫으며, 회수가 남으면 입력에서 짧게 poll해 FD·임시 파일 정리를 진행한다.
- 일반 설정 파일에는 Auto/Off만 저장한다. 진단 성공은 파일 쓰기를 하지 않으며 셀·터미널 확인 상태는 새 실행에서 초기화된다. 기존 환경변수 선언은 고급 방식으로 유지하고 F7 재확인으로 현재 실행 값을 갱신할 수 있다. 폰트·배율 변경 뒤에는 재확인이 필요하다.

### 자동 검사와 보장 범위

최종 `make -j4 check` 전체와 terminal/media ASan·UBSan 검사 모두 통과했다. 최종 diff 및 `git diff --check`도 확인했다. 구현은 `0ed1844`(core/platform), `cc83f25`(F7 UI·입력 보호·검사)로 로컬 기록했으며 원격 푸시는 하지 않았다.

`make check-terminal`은 다음을 검사한다. `make check`에도 포함되고 기존 설정 저장·파일 작업·캐시·상한·취소·회수 회귀는 그대로 실행한다.

| 검사 | 확인한 범위 |
| --- | --- |
| terminal_test | ncurses 없는 DA/셀 형식·숫자·길이·불완전 prefix·잘못된 값 및 원자적 후보 갱신 |
| terminal_input_test | 통제된 시간의 분할/늦은/잘못된 프레임, 종료 후 격리, Esc·KEY_RESIZE, 일반 문자·Unicode·방향키/F7/Alt키·SGR 마우스 보존 |
| terminal_pty | 50×9/100×24 실 프로세스의 정상/무/잘못된/분할/늦은 응답, 질의 중 일반 키·Esc·리사이즈, 표시·지우기 확인 실패·재확인·세션 활성화, F7 초점 복원, 수동 측정, ioctl, 도구 누락 구분 |
| terminal_pty 수명주기 | 환경변수 없는 PNG/JPEG/PDF 표시, 다음 실행의 확인 초기화, 진단 중 변환 PID 종료·FD·전용 임시 파일 정리, 기존 환경변수 값 보존, 자동 설정 저장 없음 |
| 기존 media 검사 | 기존 Sixel validator·변환 크기/시간/출력 상한·실패/대체·캐시·선택/모달/최소 크기 취소·FD/자식/임시 파일 정리 |

관련 ASan/UBSan 명령은 `python3 tests/run_terminal_sanitizers.py --disable-leaks`와 `python3 tests/run_media_sanitizers.py --disable-leaks`이다. LSan은 실제 시도에서 ptrace 환경 제한으로 fatal error가 나므로 **미완료**다. 사용자 환경에서 디버거/추적기 없이 `python3 tests/run_terminal_sanitizers.py` 및 `python3 tests/run_media_sanitizers.py`를 실행하면 누수 검사를 포함한다. 로그는 각 `/tmp/tfile-terminal-sanitizers`, `/tmp/tfile-media-sanitizers`에 저장한다.

### 실제 터미널에서의 확인 절차 (자동 PTY와 별도)

이 실행 환경에서는 실제 픽셀 표시·삭제·모달 잔상을 직접 확인하지 못했다. 아래 절차는 아직 사용자 육안 확인이 필요하다.

1. 환경변수 없이 `./tfile`을 열고 F7 → Image display setup → Enter를 선택한다. 자동 확인이 안 되면 실제 측정한 셀 크기로 M을 사용한다.
2. 빨강/파랑 이미지가 `|` 표시 안에 보일 때만 표시 질문에 y, 실제 지워졌을 때만 지우기 질문에 y를 선택한다. 마지막 활성화에서 y를 선택한다. 한 단계라도 실패하면 n으로 이전 상태를 유지한다.
3. PNG/JPEG/PDF를 선택해 실제 표시·파일 정보·경계·지우기를 확인하고 F1/F7의 잔상·초점 복원을 확인한다. 폰트/배율을 변경한 뒤 F7에서 재확인한다. 테스트 이미지 중 리사이즈하면 중단·지우기·부모 팝업 종료를 확인한다.
4. 종료 후 다른 터미널 조건으로 다시 실행해 확인 상태와 셀 크기가 자동 재사용되지 않는지 확인한다. Auto/Off 저장은 별도의 F7 저장 동작이다.

남은 한계는 Sixel만 지원, 터미널 자체의 DA/셀 응답 품질, 매우 느린 ESC/불완전 보고서의 입력 모호성, ED2 지우기의 실제 호환성·깜빡임, multiplexer/HiDPI 차이와 기존 SIGKILL 임시 디렉터리 가능성이다. 터미널별 영구 프로필·자동 시작 감지·패키지 설치는 제공하지 않는다.

---

아래는 이전 구현과 후속 실험의 원래 기록이다.

## 선택한 구성과 사전 실험

출력은 UI가 관리하는 Sixel이고, ImageMagick의 외부 `magick`/`convert`가 이미지 디코딩·축소·64색 양자화·Sixel 인코딩을 담당한다. PDF는 Poppler `pdftoppm -f 1 -l 1 -singlefile -scale-to … -png`로 첫 페이지만 만든 뒤 같은 이미지 경로로 인코딩한다. 대체 텍스트는 `pdftotext -f 1 -l 1 -layout`이다. 새 이미지 디코더나 PDF 렌더러, Windows 전용 API/도구를 구현하지 않았다.

README·core/preview·UI/preview·main 이벤트 루프·panel_key·draw/draw_cached·dialog_open/close·quick_find·옵션과 기존 preview/UI/PTY 검사를 먼저 확인했다. 빈 상태 변경은 `ae79050`에 별도로 기록한 뒤 확장했다.

구현·자동 검사 로컬 커밋: `3c65ce3` (`feat(preview): add bounded asynchronous Sixel image and PDF previews`). 원격으로 푸시하지 않았다.

| 후보 | 배포/호환성과 라이선스 확인 | 결정 |
| --- | --- | --- |
| ImageMagick 6/7 CLI | Linux distro 패키지, PNG/JPEG/SIXEL coder 필요, ImageMagick License | 채택. 명시적 픽셀 축소와 Sixel 출력을 하나의 선택 도구로 수행 |
| Chafa CLI | 설치된 1.14.0은 PNG/JPEG 및 Sixel 지원. upstream CLI 소스 LGPL-3.0-or-later, 번들 decoder별 라이선스 포함 | 미채택. 셀 기반 geometry를 실제 셀 픽셀 크기에 맞추는 추가 조정 대신 이번에는 명시적 픽셀 출력 선택 |
| libsixel/img2sixel | 설치된 1.10.3, MIT 라이선스, 유지되는 fork에서 보안 패치 수용 | 미채택. 두 축을 직접 지정한 실험은 비율을 바꿨으며 비율 유지에 별도 크기 확인/전처리가 필요 |
| Poppler CLI | Linux poppler-utils, 검사한 Ubuntu 패키지 copyright는 GPL-2 또는 GPL-3 | 채택. PDF를 ImageMagick/Ghostscript delegate에 넘기지 않고 첫 페이지/텍스트를 분리 |

라이브러리를 tfile에 새로 링크하거나 제3자 실행 파일을 저장소에 번들하지 않았다. 외부 도구의 배포 라이선스·번들 구성은 해당 upstream/distro 문서에 따른다.

근거: [ImageMagick SIXEL coder 소스](https://github.com/ImageMagick/ImageMagick/blob/main/coders/sixel.c), [ImageMagick License](https://imagemagick.org/license/), [보안·리소스 정책](https://imagemagick.org/security-policy/), [Chafa 기능/문서](https://hpjansson.org/chafa/), [CLI 라이선스 헤더](https://github.com/hpjansson/chafa/blob/master/tools/chafa/chafa.c), [libsixel maintained fork](https://github.com/libsixel/libsixel), [MIT 라이선스](https://github.com/libsixel/libsixel/blob/master/LICENSE), [Poppler upstream](https://poppler.freedesktop.org/), [Poppler COPYING](https://github.com/tsdgeos/poppler_mirror/blob/master/COPYING), [xterm 그래픽/지우기 제어 규약](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).

실험 이미지 120×60을 `/tmp/tfile-sixel-experiment`에서 생성했다. ImageMagick은 `-thumbnail 80x80>`에서 80×40의 raster header를, img2sixel의 두 축 직접 지정은 80×80을 출력했다. Chafa의 비대화형 셀 geometry도 비교했다. 이것들은 **인코딩된 출력 데이터** 확인이며 실제 픽셀 표시를 확인한 결과가 아니다.

## 실제 실행 환경과 지원 확인 정책

- Ubuntu 24.04.5 LTS, WSL2 커널 `6.6.87.2-microsoft-standard-WSL2`, UTF-8, ncursesw, 실행 셸 `TERM=xterm-256color`.
- 셸의 stdin/stdout은 실제 화면 터미널이 아니며 `tty`는 `not a tty`, `/dev/tty`는 ENXIO였다.
- ImageMagick 6.9.12-98 Q16, Chafa 1.14.0, img2sixel 1.10.3을 확인했다.
- Poppler 24.02.0 (`24.02.0-1ubuntu9.9`)와 필요한 NSS/NSPR 패키지를 `/tmp`에 내려받아 풀고 PATH/LD_LIBRARY_PATH로 검사했다. 시스템 설치는 하지 않았다.
- PTY는 `xterm-256color`, 8×16 셀 픽셀이라는 **테스트 입력 조건**으로 실행했다. PTY parser는 Sixel DCS·cursor·erase·bounds·모달 수명주기를 모델링하며 실제 그래픽 디스플레이가 아니다.

앱은 terminal query를 보내지 않는다. Auto는 명시적 사용자 확인 `TFILE_SIXEL=1`과 유효한 셀 크기 (`TFILE_CELL_PIXELS` 또는 ioctl)를 요구한다. `TERM`이나 WSL 여부만으로 지원을 판단하지 않으며, 응답 타임아웃/키 혼입을 발생시키는 자동 탐지는 이번에 넣지 않았다. 별도 `tools/sixel_probe.py`의 300 ms query는 앱 밖의 수동 진단에만 사용하고, 응답과 육안 확인을 구분한다.

## 구조와 수명주기

- UI → core → platform 경계를 유지했다. `src/platform/posix_media.c`만 native 파일·fork/execv/waitpid·signal·rlimit·ioctl을 호출한다. UI는 Sixel 문자열 검증과 화면 출력 순서를 관리한다.
- 정규 파일을 읽기 전용/nonblocking/no-follow FD로 열고 fstat·signature를 확인한다. 원래 이름을 셸에 조합하지 않으며 converter에는 인자 배열과 `/proc/self/fd/N` 또는 private raster 경로를 넘긴다. 공백·한글·따옴표·선행 하이픈·ImageMagick의 `[0]` 문법과 원래 파일명이 충돌하지 않는다.
- 한 변환 자식만 실행한다. 취소하면 process group에 SIGKILL을 보내고 WNOHANG으로 회수한다. 이전 자식이 회수되기 전에는 새 자식을 시작하지 않는다. 입력 처리는 변환 완료를 기다리지 않으며 main loop가 40 ms 간격으로 pending 상태만 확인한다. 일반 입력/폼/텍스트의 기존 blocking 정책은 복원한다.
- main/cached redraw는 이전 그래픽 지우기 → curses repaint/refresh → 검증된 Sixel 출력 순서다. 모달 동안 출력하지 않는다. cursor와 Sixel scrolling mode를 저장/복원한다. 전체 화면 ED2와 curses clearok를 사용하므로 미확인 터미널의 픽셀 삭제 호환성과 깜빡임은 남은 검증 대상이다.
- 캐시는 현재 선택 한 개의 결과만 메모리에 유지한다. path/reader의 dev·inode·size·mtime·ctime 변경, 출력 pixel size, 첫 페이지 고정, text/image 경로를 고려한다. 선택 변경/명시적 새로고침/Off/모드 전환은 폐기한다. 완료 직전에도 열린 FD와 현재 path의 identity를 비교한다. 초점 변경/같은 화면 redraw는 변환을 반복하지 않는다.
- 정상 종료와 SIGINT/SIGTERM/SIGHUP은 입력/모달을 풀고 같은 정리 경로를 거친다. 부모의 비정상 종료는 Linux parent-death SIGKILL로 직접 변환 자식을 종료한다. SIGKILL/시스템 중단에서는 임시 파일 정리 코드를 실행할 수 없으므로 private 디렉터리가 남을 수 있다.

## 상한

| 리소스 | 상한/정책 |
| --- | --- |
| 입력 | 정규 파일, PNG/JPEG/PDF magic, 64 MiB 이하 |
| 이미지 출력 | 1536×1152 pixel 및 1,769,472 pixel 이하, 비율 유지, 래스터 원본 확대 없음, 64색 |
| Sixel 결과/캐시 | 2 MiB 이하, 정확히 한 DCS, 주변 escape/newline·다른 이미지 거부, raster/repeat 실제 경계 검증 |
| PDF 임시 raster | 첫 페이지만, 긴 변 1536 pixel 이하 (중간 raster 최대 2,359,296 pixel), 해당 stage 파일당 8 MiB 제한 |
| PDF 텍스트 | 첫 페이지만, 파일 64 KiB 이하, UI 128행 이하. 초과 시 상한 오류 |
| 변환 시간 | 총 8초. pending loop wall clock + 자식 alarm, 각 프로세스 CPU 5초 |
| 프로세스 메모리 | 일반 빌드 주소 공간 512 MiB. ImageMagick pixel cache 128 MiB, map/disk cache 0, thread 1 |
| 동시 작업 | active 또는 retiring 자식 하나. 큐/다중 파일 디스크 캐시 없음 |

## 검사 결과

- `make check`: 기존 core/platform/UI/PTY 전체와 새 media 대역/PTY 검사 통과. 최종 실행 종료 코드 0.
- `tests/media_tools.py`: PNG/JPEG/PDF 첫 페이지, 제한 텍스트, missing tools, converter failure, 암호화 진단, 지연/시간 초과, 잘못된 DCS/초과 출력, 50회 취소, 늦은 결과와 identity 교체, FIFO 거부, 0바이트/확장자만 이미지인 일반 텍스트, 초점/100회 동일 준비의 캐시 유지, mode/Off, FD·자식·temp 회수.
- `tests/media_pty.py`: ncurses cells와 Sixel 순서/좌표/경계, clear, F1 모달 겹침/복원, 창 크기/50×9 생략, 선택 중 취소/종료, Auto/Off/목록만 전환, PDF 첫 페이지/텍스트 대체, 안전한 오류 표시, 기존 text scroll. 실제 픽셀 표시를 검사했다고 기록하지 않는다.
- `tests/media_real.py`: 실제 ImageMagick PNG/JPEG, 특수문자 파일명, 2048×1024 → 512×256, 깨진 PNG/JPEG·입력 크기 상한·누락 도구. 실제 Poppler의 두 페이지 PDF에서 **첫 페이지만** image/text 생성, 정상 암호화 PDF를 password로 읽을 수 있음을 확인하고 앱의 password 오류 검사, 손상 PDF 분류. 결과 Sixel을 UI validator로 검사했다.
- Sixel validator의 잘못된 raster, repeat, aspect, 주변 terminal controls와 10,000개 변이 입력을 검사한다.
- `git diff --check`, dependency architecture 검사, Python syntax 검사를 실행한다.

### Sanitizer의 범위와 제약

ASan/UBSan 격리 바이너리로 media native·기존 preview native·media PTY를 검사한다. ASan의 거대한 가상 shadow memory가 fork된 자식의 RLIMIT_AS와 충돌해 exec interceptor가 `Failed to mmap`을 출력한 것을 확인했다. **ASan 빌드에서만** 자식 RLIMIT_AS를 제외했다. 일반 빌드 및 ImageMagick 내부 메모리 제한, CPU/파일/시간 상한은 유지된다.

LeakSanitizer 요청 실행은 이 환경의 ptrace 제약으로 `LeakSanitizer does not work under ptrace`를 보고해 완료하지 못했다. 따라서 LSan으로 누수 부재를 확인했다고 기록하지 않는다. `--disable-leaks`를 명시한 ASan/UBSan 검사와 별도의 FD/자식/temp 수명주기 검사는 통과했다. 이 결과를 외부 ImageMagick/Poppler 내부나 프로젝트 전체의 누수 부재로 확대하지 않는다.

```sh
make check
make check-media
python3 tests/media_real.py
python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-final --disable-leaks
# ptrace가 없는 별도 사용자 환경에서는 LSan도 요청:
python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-with-leaks
```

최종 로그: `/tmp/tfile-image-verified-check.log`, `/tmp/tfile-media-sanitizers-verified.log`, `/tmp/tfile-media-sanitizers-verified/{media,preview,media_pty}.log`, `/tmp/tfile-media-real-final.log`, `/tmp/tfile-media-pty-final.log`. 실패한 LSan 요청은 `/tmp/tfile-media-asan-leaks.log`, `/tmp/tfile-media-pty-asan-leaks.log`에 분리했다. `/tmp` 로그는 저장소에 포함하지 않는다.

## 사용자 터미널에서 확인할 절차 / 아직 미검증인 항목

1. Linux/WSL 터미널에서 선택 패키지를 설치하고 `python3 tools/sixel_probe.py`를 실행한다. query 응답과 셀 크기를 확인하되 이것만으로 표시 지원을 판단하지 않는다. 셀 응답이 없으면 실제 측정한 `--cell-pixels WIDTHxHEIGHT`를 지정한다.
2. 작은 red/blue 단독 이미지가 보이고 지워지는지 확인한다. 이어지는 ncurses 패널에서 이미지가 테두리/왼쪽 패널을 침범하지 않는지 확인한다. `m`으로 모달을 겹치고 닫으며 잔상이 없는지, 창을 확대/축소해 제대로 지워지는지 확인한다.
3. 모두 확인되면 도구가 출력한 `TFILE_SIXEL=1 TFILE_CELL_PIXELS=… ./tfile` 명령으로 정상 PNG/JPEG/PDF가 있는 디렉터리를 연다. 정보/로딩/이미지와 PDF 첫 페이지만 보이는지 확인한다.
4. 파일을 빠르게 바꾸고 Tab/마우스 초점, F1/F7/F9 모달, r, 휠/텍스트 스크롤, 50×9 ↔ 100×24 크기 변경, F7 Image Auto/Off, 미리보기/이중 목록/목록만 모드와 변환 중 q 종료를 확인한다.
5. 실제 터미널 이름·버전, 폰트/배율/셀 픽셀, direct/SSH/WSL/multiplexer 여부와 위 육안 결과를 기록한다. 표시/삭제가 다르면 환경 변수를 제거하거나 Off로 사용한다.

**미검증:** 실제 GUI 터미널의 standalone·ncurses 픽셀 표시·삭제·모달 잔상·리사이즈, 사용자의 WSL 터미널 호환성, 다양한 폰트/HiDPI/SSH/multiplexer, 직접 설치하지 않은 ImageMagick 7의 실제 실행, LSan. PTY 통과가 이 항목들을 대체하지 않는다.

**범위 밖:** 다른 이미지 형식, PDF 페이지 이동·모든 페이지 변환·확대·애니메이션, terminal query 자동 탐지, tmux/screen passthrough, 영구 설정/디스크 캐시. 터미널 폰트 크기가 바뀌면 셀 값을 다시 확인하고 재시작해야 한다. 출력은 상한이 있는 동기 terminal write이므로 느린 원격 터미널의 전송/화면 지연까지 없애는 기능은 아니다.

## 후속 정적 검토 수정

최신 `d21d052` 기준에서 네 조건 모두 해당했다. UI → core → platform과 기존 파일 작업의 목적지 재검증·덮어쓰기/경로 교체 방어는 유지하고 다음 부분만 수정했다.

수정·회귀 검사 로컬 커밋: `260ec6d`. 원격 푸시는 하지 않았다.

| 지적 | 수정 전 확인·재현 | 수정과 회귀 검사 |
| --- | --- | --- |
| 최소 화면 미만의 변환 | 느린 PNG 대역 실행 중 49×8로 줄이면 `rows=0` 조기 반환 뒤 자식이 남았다. 새 PTY 검사의 자식 회수 대기가 실패했다 | `preview_prepare`가 표시 불가 시 media 상태를 폐기하고 기존 취소/비차단 회수 경로를 사용한다. active pending은 즉시 해제하고 retiring 동안만 polling한다. native 검사에서 pending/FD/temp/자식 정리와 복원·선택 교체를, PTY에서 변환 중 축소·유지(변환/출력/CPU tick 증가 없음)·복원·다른 파일 선택·작은 화면 종료를 검사한다 |
| 목적지 읽기 실패 | 실제 uid 1000에서 chmod 000 디렉터리와 삭제된 경로를 열었다. 읽기 오류와 함께 기존 빈 상태 문구가 나타났고 Use가 허용됐다 | 목록 조회의 성공 여부를 별도 유지한다. 실패는 `Cannot inspect directory contents`와 `Cannot read directory: 원인`을 표시하고 Use를 차단한다. 실제 비 root native 검사에서 정상 빈 목적지/원본, 오류 상태의 Space 차단, 명시적 Enter path 및 Parent 복구를 확인한다. 경로 자동 대체와 후속 파일 작업 변경은 없다 |
| ASan 컴파일러 감지 | 기존 소스는 GCC의 `__SANITIZE_ADDRESS__`만 검사했다. Clang은 설치되어 있지 않아 기존 Clang 실행 실패를 재현했다고 주장하지 않는다 | GCC 매크로 또는 Clang `__has_feature(address_sanitizer)`를 사용하고 feature query가 없는 컴파일러에는 0 fallback을 둔다. GCC 13.3 일반/ASan 빌드에서 exec된 대역이 직접 `getrlimit`으로 조회: 일반 AS=536870912, ASan AS=-1, 양쪽 CPU=5, PDF text FSIZE=65536, CORE=0. 환경변수 우회 기능은 없다. 기존 alarm/총 시간·stage별 파일 상한·ImageMagick 내부 제한은 유지한다 |
| PDF 공백 출력 | pdftotext 대역의 공백·탭·개행·form feed만 있는 출력이 텍스트 없음 안내를 생략했다. native UI 검사가 실패했다 | 0바이트와 ASCII whitespace만 있으면 `No text on PDF page 1`. 표시할 때 form feed만 제거하며 나머지 들여쓰기/줄바꿈과 안전한 표시 경로·64 KiB/128행 제한은 유지한다. 대역 0바이트/공백/form feed/들여쓰기 있는 정상 텍스트와 오류/timeout/missing tools를 구분해 검사한다. 실제 Poppler의 빈 첫 페이지와 이미지 전용 첫 페이지도 검사한다 |

재현 로그: `/tmp/tfile-review-repro-small.log`, `/tmp/tfile-review-repro-picker.log`, `/tmp/tfile-review-repro-text.log`. 이 로그들은 수정 전 실패를 기록한다.

### 검사 범위와 실행 명령

권한 검사는 **비 root**로 실행한다. 이 환경은 uid 1000, GCC 13.3, Ubuntu 24.04 / WSL2이며 실제 화면 터미널은 연결되지 않았다. Clang은 PATH 및 일반 설치 위치에 없었고 검사를 위해 설치하지 않았다. `tests/run_sanitizers.py`의 기존 17개 native 검사와 `tests/run_media_sanitizers.py`의 media/preview/picker native + media PTY 검사는 별개다.

최종 결과: `make check`와 `make check-media` 종료 코드 0, 실제 ImageMagick/Poppler 변환 검사 통과, 기존 17개와 별도 미디어 4개 ASan/UBSan 검사 통과(누수 검출 제외). `git diff --check`, Python syntax, 의존 경계 검사도 통과했다. 새 검사 추가 후 일반 GCC 빌드의 제한값은 `check-media` 로그에서, GCC ASan 빌드의 제한값은 미디어 sanitizer의 `media.log`에서 직접 확인했다.

아래는 이번 실행의 명령과 로그 위치다. sanitizer의 `--disable-leaks`는 명시적으로 ASan/UBSan만 실행하며 **누수 검사는 미완료**다. 기본 LSan 요청은 실제 실행했고 ptrace 제약으로 실패했다. 보안 설정은 바꾸지 않았다.

```sh
make check > /tmp/tfile-review-check.log 2>&1
make check-media > /tmp/tfile-review-check-media.log 2>&1
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-review --disable-leaks > /tmp/tfile-review-existing-sanitizers.log 2>&1
python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-review --disable-leaks > /tmp/tfile-review-media-sanitizers.log 2>&1
# 기본 누수 검출 요청 (이번 ptrace 환경에서 실패):
python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-review-lsan > /tmp/tfile-review-media-lsan.log 2>&1
# 사용자가 선택 의존성을 설치한 환경:
python3 tests/media_real.py > /tmp/tfile-review-media-real.log 2>&1
```

실제 변환 검사는 시스템 설치 대신 이전 검사에서 `/tmp`에 추출한 Poppler 24.02.0을 사용했다. 이번 실제 실행 명령은 다음과 같다.

```sh
PATH=/tmp/tfile-sixel-experiment/poppler/usr/bin:/usr/bin:/bin \
LD_LIBRARY_PATH=/tmp/tfile-sixel-experiment/poppler/usr/lib/x86_64-linux-gnu \
python3 tests/media_real.py > /tmp/tfile-review-media-real.log 2>&1
```

sanitizer 상세 로그는 기존 검사 `/tmp/tfile-sanitizers-review/*.run.log`, 미디어 검사 `/tmp/tfile-media-sanitizers-review/{media,preview,picker,media_pty}.log`, LSan 실패 `/tmp/tfile-media-sanitizers-review-lsan/*.log`다. `/tmp` 파일은 임시 검증 산출물이며 저장소에 포함하지 않는다. 사용자 환경에서 GCC/Clang이 모두 있으면 미디어 sanitizer 명령에 `CC=gcc` 또는 `CC=clang`을 붙여 별도 출력 디렉터리로 검사할 수 있다. 일반 빌드는 `make CC=gcc` 또는 `make CC=clang`으로 별도 checkout/clean 후 검사한다. 여기서는 **Clang 일반/ASan 빌드 미검증**이다.

### 사용자 수동 확인과 남은 한계

`python3 tools/sixel_probe.py`로 사용자 터미널의 단독 출력·지우기와 ncurses 패널·모달·리사이즈를 육안 확인한 뒤 출력된 실행 명령을 사용한다. 느린 대역의 수명주기는 `make check-media`로 확인할 수 있으며, 실제 앱에서는 이미지/PDF 변환 도중 49×8로 축소하고 유지·복원·종료를 확인한다. F5 Browse에서는 접근 불가 목적지가 빈 상태로 안내되지 않고 Parent/Enter path로 수정할 수 있는지 확인한다. 직접 경로 수정 없이 다른 목적지가 선택되면 안 된다.

PTY는 프로토콜·ncurses cells·자식/입력 수명주기를 확인한다. **실제 터미널 픽셀 표시·삭제·모달 잔상은 여전히 미검증**이다. Clang 실행은 미검증이며 자동 실행 환경의 LSan은 미완료다. 이후 별도 사용자 WSL 실행의 제한된 성공 범위는 아래 기록으로 구분한다. 기존 Sixel 전체 화면 지우기의 깜빡임 가능성, multiplexer/HiDPI 호환성, 급작스러운 SIGKILL 시 임시 디렉터리 잔존 가능성은 이번 수정 범위에서 해결하지 않았다.


### 수정 후 사용자 WSL 터미널의 수동 sanitizer 결과 검토

사용자가 별도 WSL 터미널에서 아래 두 명령을 `--disable-leaks` 없이 실행했다고 보고했다. 기존 17개는 모두 PASS(exit 0)와 누수 검출 활성화 종료 메시지, 미디어의 media/preview/picker/media_pty는 모두 PASS와 `Leak detection requested`를 확인했다. 이 검토에서는 테스트를 재실행하지 않고 기존 산출물을 읽었으며 수동 로그를 덮어쓰지 않았다.

```sh
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-fixes-manual
python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-fixes-manual
```

- `/tmp/tfile-sanitizers-fixes-manual/`: 상세 실행 로그 17개가 모두 PASS이며 빌드 로그 17개는 비어 있다.
- `/tmp/tfile-media-sanitizers-fixes-manual/`: `media.log`, `preview.log`, `picker.log`, `media_pty.log` 모두 PASS다. media 로그의 실제 자식 제한값은 AS=-1, CPU=5, FSIZE=65536, CORE=0이다.
- 전체 상세 로그에서 ptrace/LeakSanitizer fatal error, 검사 생략·누수 검출 비활성화 안내, ASan/UBSan 오류 또는 누수 보고가 발견되지 않았다. PASS 설명의 정상적인 오류/취소 대역 검사는 sanitizer 오류나 검사 실패가 아니다.
- 두 스크립트는 `--disable-leaks`가 없을 때 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`과 UBSan 중단 설정을 적용한다. 하위 대역/PTY 실행도 이 환경을 상속한다. 확인한 17개 기존 바이너리와 미디어 디렉터리의 4개 바이너리는 모두 `libasan`에 연결되어 있다. 소스와 실행 스크립트에서 LSan 비활성화/기본 옵션·suppression 훅을 발견하지 않았다.

상위 명령의 exit 값·종료 메시지·옵션은 사용자 보고를 근거로 하며, 디렉터리의 상세 로그와 스크립트 설정은 직접 검토한 근거다. 이 범위에서 **수정 후 사용자 수동 실행의 ASan/UBSan/LSan 검사 완료 및 누수 미검출**을 확인했다. 이는 해당 17개 native 검사와 별도 media/preview/picker native·media PTY의 실제 실행 경로에 한정한다. 외부 ImageMagick/Poppler 내부, 프로젝트 전체 또는 모든 사용 경로의 누수 부재를 의미하지 않는다. 미디어 수명주기는 변환 도구 대역으로 검사하며 실제 터미널 픽셀 표시·삭제·모달 잔상 검증을 대체하지 않는다.

이전 자동 환경의 `/tmp/tfile-media-sanitizers-review-lsan/` ptrace 실패와 `--disable-leaks` 검사 기록은 그대로 유지한다. 이번 수동 성공이 그 자동 실행을 성공으로 바꾸지는 않는다. Clang 실행 및 실제 그래픽 육안 검증의 미확인 상태도 유지한다.


## 2026-10-08 Fit 배치 변경

미디어 영역은 상단 파일명·메타데이터·구분선 아래 본문에서 계산한다.
본문 양쪽·위·아래에 1셀 여백을 두고 테두리와 하단 위치 안내 행을 제외한다.
메타데이터는 왼쪽 정렬을 유지한다. 이미지/PDF는 검증된 Sixel raster header의
실제 픽셀 크기와 현재 셀 픽셀 크기를 이용해 가로·세로 중앙에 둔다.
마지막 Sixel band는 6픽셀로 올림하여 배치 경계에 포함한다. 중앙 좌표는
셀 단위로 내림하므로 1셀 미만의 오차가 허용된다. 자르기와 비율 왜곡은 없다.

기존 512×384 상한은 넓은 화면에서 가용 영역을 활용하지 못했다. 현재 출력
상한은 위 표와 같이 1536×1152/1,769,472픽셀, Sixel 2MiB이다. 래스터는
`-auto-orient -thumbnail WxH>`로 원본 확대 없이 Fit한다. PDF 첫 페이지는
가용 크기의 긴 변으로 Poppler를 렌더링한 뒤 같은 Fit 경로를 거친다.
Poppler 중간 raster는 최대 1536×1536/2,359,296픽셀, 파일당 8MiB이다.
원본 64MiB, 주소 공간 512MiB, pixel cache 128MiB, map/disk 0, thread 1,
CPU 5초/프로세스, 전체 변환 8초 제한은 유지한다. 출력 한도 초과는 오류로 처리한다.

위치만 바뀌거나 축소되지 않은 작은 원본이 새 영역에 들어가는 경우 변환 결과를
재사용한다. 크기 변경은 이전 진행 중 변환을 취소하고 마지막 변경 후 120ms를
기다린다. 완료된 이전 결과는 현재 영역에 들어갈 때만 재배치한다. 선택 변경,
모달, 최소 화면 미만 축소는 기존 지우기/취소 정책을 유지한다. 초점 정책과
자동 지원 감지는 유지하며 확대/축소·PDF 페이지 이동은 추가하지 않는다.

자동 검증: `media_fit_pty.py`는 세로/가로/정사각/작은/큰 이미지와 PDF의
출력 좌표·비율·경계, 연속 리사이즈 작업 수, 원본 캐시, 모달/최소 화면 지우기를
검사한다. `media_real_fit.py`는 실제 ImageMagick/Poppler로 80×24/260×80에서
출력 크기와 Fit 비율을 검사한다. 이것은 변환 결과 및 프로토콜 검증이다.
실제 Sixel 그래픽 터미널에서 픽셀 표시·색·잔상을 육안 확인하지 않았다.

최종 검증 실행 결과: `make -j4 check-isolated`에서 core/settings/terminal 검사가
통과했다. 새 캐시 단위 테스트의 명시적 셀 픽셀 설정을 보완한 뒤
`make -o check-core -o check-settings -o check-terminal check-isolated`로 나머지
UI/PTY 전체를 재실행하여 종료 코드 0을 확인했다. 위 세 영역은 이미 통과하여
반복하지 않았다. `tests/media_real.py`의 실제 PNG/JPEG/Poppler 변환 검사,
아키텍처 검사, Python 문법 검사와 `git diff --check`도 통과했다.
