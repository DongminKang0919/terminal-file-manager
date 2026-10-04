# 이미지·PDF 미리보기 1단계 검증 (2026-10-04)

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
| 이미지 출력 | 512×384 pixel 이하, 비율 유지, 이미지 확대 없음, 64색 |
| Sixel 결과/캐시 | 256 KiB 이하, 정확히 한 DCS, 주변 escape/newline·다른 이미지 거부, raster/repeat 실제 경계 검증 |
| PDF 임시 raster | 첫 페이지만, 긴 변 512 pixel 이하, 해당 stage 파일당 8 MiB 제한 |
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
