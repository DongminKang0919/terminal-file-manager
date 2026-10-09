# 이미지/PDF 화면 재출력 계측과 개선 — 2026-10-09

## 확인한 기준과 측정 방법

수정 전 기준은 로컬 커밋 `997fe9e`이다. 최신 `draw_screen`에서도 매번 `graphics_clear`를 호출하고 curses 전체 재그리기 후 같은 Sixel을 재전송했다. 변환 데이터 캐시는 존재하지만 터미널에 남아 있는 픽셀 상태 캐시는 별개였다. 기준 제품을 `make tfile`로 빌드해 `/tmp/tfile-redraw-baseline-20261009`에 보존했다. 기준 코드에서 실제 아래 입력을 실행해 전송과 지우기를 확인했으며 정적 추정만으로 변경하지 않았다.

`tests/media_redraw_measure.py`는 기존 `media_pty.Terminal`/`GraphicsScreen`과 `media_tools.STUB`를 재사용한다. 각 실행은 새 임시 파일/도구/로그와 격리 HOME/XDG를 사용한다. PTY 입력 조건은 TERM=`xterm-256color`, **100×30**, ioctl 및 확인된 셀 **8×16**이다. GCC 13.3.0, ncursesw 6.4(20240113), 일반 `-O2` 빌드로 실행했다. Sixel 지원과 셀 값은 테스트의 명시적 override로 고정했다. Auto 응답 수명주기는 별도 기존 터미널 검사로 검증한다.

테스트 PNG는 1200×300, JPEG는 300×1200 형상 대역이며 PDF는 첫 페이지 renderer → PNG 변환 대역을 사용한다. 대역은 동일한 argv/제한/validator 경로를 거쳐 유효 Sixel을 생성한다. PNG/JPEG 대역의 raster를 비압축으로 만들어 바이트 비용을 관찰한다(첫 PNG DCS 4,638바이트). 실제 파일 디코딩의 성능 측정이나 최대 2MiB 전송량 실험은 아니다. 실제 ImageMagick/Poppler Fit 검사는 별도로 실행한다.

입력은 첫 PNG 표시 완료 → 0.5초 무입력 → Space 켜기/끄기 → 빈 forward history의 `]`(상태만 변경) → Down으로 JPEG/PDF/텍스트 → Home으로 PNG 복귀 → F1 Help/ESC → 120×32, 140×35, 180×40, 100×30 리사이즈 순서다. resize 간격은 약 25ms이며 최종 크기가 기준 크기로 복귀한다. 첫 이미지 로딩과 모달 전 Home 복귀는 각 측정 구간에서 제외한다. 각 구간은 화면 상태/이미지 출력 acknowledgement 이후 100ms를 추가로 읽어 후속 전송도 센다. 종료 시의 지우기는 제외한다.

기준/수정본을 교대로 **각 3회** 실행했다. 아래 횟수/바이트는 세 번 모두 동일했다. converter_processes는 exec된 도구 기록 수이며 PDF는 renderer와 encoder를 각각 센다. Sixel은 실제 DCS frame 수와 DCS 바이트 길이, 전체 지우기는 PTY에 나타난 **ED2(CSI 2J)** 횟수다. 한 복구에 명시적 ED2와 curses의 clearok repaint가 각각 ED2를 내므로 값 2는 이미지 두 장을 뜻하지 않는다. PTY 전체 바이트는 curses 문자/제어/색/커서 및 Sixel을 모두 포함한다.

## 전후 결과

모든 열은 **변경 전 → 변경 후**이다.

| 입력 구간 | 변환 도구 실행 | Sixel 횟수 | Sixel 바이트 | ED2 횟수 | PTY 전체 바이트 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 유휴 0.5초 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |
| 마킹 켜기 | 0 → 0 | 1 → 0 | 4,638 → 0 | 2 → 0 | 13,981 → 107 |
| 마킹 끄기 | 0 → 0 | 1 → 0 | 4,638 → 0 | 2 → 0 | 13,981 → 107 |
| 상태 안내만 변경 | 0 → 0 | 1 → 0 | 4,638 → 0 | 2 → 0 | 14,048 → 96 |
| PNG → JPEG | 1 → 1 | 1 → 1 | 2,720 → 2,720 | 2 → 2 | 12,315 → 12,315 |
| JPEG → PDF | 2 → 2 | 1 → 1 | 33 → 33 | 2 → 2 | 9,622 → 9,622 |
| PDF → 텍스트 | 0 → 0 | 0 → 0 | 0 → 0 | 2 → 2 | 9,394 → 9,394 |
| Help 모달 열기 | 0 → 0 | 0 → 0 | 0 → 0 | 2 → 2 | 11,635 → 11,635 |
| Help 모달 닫기 | 0 → 0 | 2 → 1 | 9,276 → 4,638 | 2 → 0 | 28,092 → 14,044 |
| 연속 리사이즈 | 0 → 0 | 4 → 4 | 18,552 → 18,552 | 8 → 8 | 61,567 → 61,567 |

마킹 한 번의 PTY 전송은 13,981 → 107바이트(약 99.2% 감소), 상태 안내는 14,048 → 96바이트(약 99.3% 감소)였다. 해당 세 구간은 재변환이 원래도 없었으며, 개선은 재전송과 전체 화면 지우기 생략이다. 모달 복귀는 필요한 한 번의 재출력을 보존하고 뒤따르는 동일 main frame의 중복 전송만 생략했다.

유휴 출력은 원래도 없어서 추가 개선이 없다. 이미지 선택 변경과 텍스트 전환의 지우기, 크기/위치가 바뀌는 리사이즈의 재출력은 유지했다. 이번 resize burst는 같은 최종 크기로 돌아가므로 원래 변환 캐시/120ms 지연 정책으로도 변환이 0회였고 변경 후도 0회다. 다른 최종 크기의 재변환 병합은 기존 `media_fit_pty.py`에서 별도로 검증한다.

### 입력 acknowledgement 시간

단위 ms, **중앙값 (최소–최대)**이다. PTY가 원하는 cell/이미지 상태를 관찰한 시점이며 실제 터미널 픽셀 렌더링이나 사용자의 화면 반영 시간을 측정한 것이 아니다. resize는 burst 전체 소요 시간을 포함한다. acknowledgement read 구간은 1ms이며 select 대기를 남은 구간으로 제한한다. 기존 공용 reader의 고정 10ms 대기가 작은 출력의 계측값을 부풀리지 않도록 계측 전용 subclass를 사용했다. 호스트 부하는 통제하지 않았다. 따라서 아래 값으로 응답 지연 개선/악화를 확정하지 않는다. 전송량 감소와 달리 **지연 개선은 확인하지 못했다**.

| 입력 구간 | 변경 전 | 변경 후 |
| --- | ---: | ---: |
| 유휴 0.5초 | — | — |
| 마킹 켜기 | 4.77 (3.39–6.16) | 1.21 (1.21–1.26) |
| 마킹 끄기 | 4.89 (3.88–5.86) | 1.24 (1.23–1.25) |
| 상태 안내만 변경 | 5.87 (4.22–5.88) | 1.46 (1.34–1.56) |
| PNG → JPEG | 115.95 (85.97–118.55) | 117.88 (87.31–126.38) |
| JPEG → PDF | 198.41 (197.66–199.97) | 198.83 (165.53–199.40) |
| PDF → 텍스트 | 3.89 (3.27–4.98) | 3.79 (3.39–4.73) |
| Help 모달 열기 | 2.44 (1.62–4.33) | 1.91 (1.32–2.06) |
| Help 모달 닫기 | 37.84 (37.77–39.21) | 30.37 (30.20–32.20) |
| 연속 리사이즈 | 101.99 (101.81–102.13) | 101.93 (101.11–102.23) |

원본 계측 로그: `/tmp/tfile-redraw-final-{before,after}-{1,2,3}.json`. 최초 기준 확인 로그는 `/tmp/tfile-redraw-before.json`이다. `/tmp` 산출물은 커밋하지 않는다.

## 구현과 보수적 복구

변환 결과의 세대와 실제 출력한 이미지의 세대를 분리한다. 화면 유지에는 현재 선택/preview path 일치, 완료·검증된 이미지 결과, Auto/확인된 Sixel 지원, preview 모드, 모달 없음, scroll offset=0, 동일 raster 크기/중앙 좌표/셀 픽셀/터미널 행·열과 동일 세대가 모두 필요하다. 이 조건 중 하나라도 달라지면 기존 ED2 → curses repaint → Sixel 경로를 사용한다. 메모리 포인터 재사용을 이미지 identity로 해석하지 않는다.

이미지가 차지하는 **행 전체**의 문자·색상·속성을 새 stdscr와 curses physical-screen 모델인 curscr 사이에서 비교한다. 픽셀 사각형 왼쪽에서 목록을 편집해도 curses가 EL/ECH를 행 끝까지 낼 수 있으므로 같은 행의 목록 변경은 유지 경로에서 제외한다. 유지하는 갱신 동안만 curses insert/delete line/character 최적화를 비활성화하고 기존 설정으로 복원한다. 물리/가상 화면 clear 요청, pending virtual-screen update, endwin 상태도 보수적으로 무효화한다. 최소 크기 미만 축소, 모달 표시/복귀, 화면 복구 및 위치·크기 변경에서도 필요한 지우기/출력을 유지한다.

지금까지 프로토콜 출력 검증을 한 terminfo 프로필은 `xterm-256color`이므로 유지 경로는 그 프로필에만 적용한다. 다른 프로필은 기존 전체 지우기/재출력을 유지하며 회귀 검사에서는 `xterm`을 사용해 이 fallback을 확인한다. TERM은 Sixel 지원을 의미하지 않으며 기존 자동 감지/수동 확인 조건도 필요하다. 같은 TERM을 사용하는 모든 실제 에뮬레이터의 픽셀 유지 동작까지 확인한 것은 아니다.

curses의 **가상 화면 구성은 여전히 전체 수행**한다. 새 프레임을 실제 terminal 상태와 비교해 안전한 물리 갱신만 생략하는 제한적 최적화이며 가상 화면 CPU 비용을 줄였다고 주장하지 않는다. 같은 이미지라도 수정한 목록 행이 픽셀 점유 행과 겹치면 재전송한다. 리사이즈/선택 변경/모달 열기에는 잔상 방지 전체 지우기가 남는다. 새로운 프로토콜, 스레드, 확대/축소, PDF 페이지 이동은 추가하지 않았다. 변환 자식의 시간/자원/출력 상한, 취소·회수, identity 검증과 Sixel validator는 그대로다.

## 회귀와 검사 범위

- 새 계측의 `--verify`: PNG 및 PDF의 idle/마킹/상태 변경에 변환·Sixel·ED2가 없고 이미지가 유지됨, 전환/모달/리사이즈 시 필요한 출력, 이미지와 같은 행의 마킹에서 보수적인 복구, 명시적 r 새로고침, 다른 terminfo fallback, 파일 목록 초점 유지를 확인한다.
- 기존 PTY parser를 보강해 이미지 영역의 문자 출력, EL/ECH/ED0, insert/delete/scroll로 인한 손상을 추적한다. oracle의 정상 외부 쓰기와 의도적 pixel write/EL/ED0 손상 fixture도 확인한다. 이는 실제 그래픽 표시 엔진이 아니다.
- native media 검사에 상태만 바꿀 때 Sixel stdout이 증가하지 않는지와 stdscr/curscr clearok 복구 시 재출력되는지를 추가했다. 특정 private 함수 순서를 고정하는 검사 대신 출력/화면 상태를 확인한다.
- 기존 media/fit/Auto 검사로 PNG/JPEG/PDF, 세로/가로/정사각/작은/큰 이미지, 빠른 선택 변경/늦은 완료, Off/모달, 이중 목록 및 최소 크기 축소·복원을 검사한다. Auto idle에는 손상 없음 assertion도 추가했다.

### 실행 명령과 sanitizer

```sh
# 관련 검사 (HOME/XDG를 격리):
python3 tests/isolated_check.py make check-media
python3 tests/isolated_check.py python3 tests/media_redraw_measure.py --verify
make -j4 check
# 최종 제품 소스의 ASan/UBSan, 알려진 LSan 제약 때문에 재요청하지 않음:
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-redraw-complete-20261009 --disable-leaks
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-redraw-complete-20261009 --disable-leaks
```

관련 `check-media`, native 화면 복구 검사, 전후 계측 `--verify`가 통과했다. 최종 제품 소스의 기본 ASan/UBSan **18개**와 미디어 **5개(media/preview/picker/media_pty/media_redraw)**가 모두 통과했다. 시간 reader 보정 후 새 계측 검사를 같은 최종 sanitizer 제품으로 다시 실행해 통과했다(`/tmp/tfile-redraw-measure-san-final.log`). 이번에는 설정/터미널 전용 sanitizer runner를 별도로 실행하지 않았다. 일반 `make check`의 터미널/설정/PTY 통과를 그 전용 sanitizer 결과로 확대하지 않는다. LSan은 이번 요청에서 **실행하지 않았고 미검증**이다. 지난 작업에서 확인한 ptrace 제약 때문에 반복 요청하거나 환경 보안을 변경하지 않았다.

최초 전체 검사 `/tmp/tfile-redraw-check.log`는 exit 0이었다. 전체 재실행 `/tmp/tfile-redraw-check-final.log`도 exit 0으로 통과했다. 마지막 문자 비교 버퍼 보강은 최대 결합 문자 셀을 사용하는 native 회귀(`/tmp/tfile-redraw-combining-test.log`), PTY 재출력 검사(`/tmp/tfile-redraw-combining-pty.json`)와 미디어 ASan/UBSan 5개(`/tmp/tfile-redraw-media-san-unicode.log`, `/tmp/tfile-media-sanitizers-redraw-unicode-20261009/`)로 별도 확인했다. 기본 sanitizer 로그는 `/tmp/tfile-redraw-san-complete.log`, 미디어는 `/tmp/tfile-redraw-media-san-complete.log`와 각 output-directory의 상세 로그에 있다. 아키텍처 검사, tests/tools Python AST 문법 검사 및 `git diff --check`도 실행한다. AST 문법 검사는 동작 검사가 아니다.

디버거/추적기가 없는 사용자 환경의 수동 LSan 명령:

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-redraw-manual-20261009
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-redraw-manual-20261009
```

### 전후 재실행

기준 바이너리가 없으면 별도 임시 디렉터리에 `git archive 997fe9e`를 풀고 그곳에서 `make tfile`로 빌드한다. 현재 테스트 harness를 양쪽 제품에 그대로 사용한다. baseline에는 `--verify`를 붙이지 않는다(재전송 생략 assertion은 수정본에만 적용).

```sh
redraw_base=$(mktemp -d /tmp/tfile-redraw-base-XXXXXX)
git archive 997fe9e | tar -x -C "$redraw_base"
make -C "$redraw_base" tfile
python3 tests/isolated_check.py env TFILE_BINARY="$redraw_base/tfile" \
  python3 tests/media_redraw_measure.py > /tmp/tfile-redraw-before-repeat.json
make tfile
python3 tests/isolated_check.py python3 tests/media_redraw_measure.py --verify \
  > /tmp/tfile-redraw-after-repeat.json
```

## 실제 터미널에서 남은 확인

이번 환경에는 실제 그래픽 터미널이 없어 **픽셀 잔상, 문자와 이미지의 실제 겹침, 색, 육안 깜빡임, Windows Terminal/WSL 및 다른 에뮬레이터 호환성은 미검증**이다. PTY는 출력 프로토콜과 문자 모델/자식 수명주기를 검사할 뿐 실제 렌더링 엔진이 아니다. 다른 ncurses/terminfo 버전, 같은 TERM을 보고하는 에뮬레이터, 느린 SSH/HiDPI/multiplexer에서의 실제 지연을 측정하지 않았다.

사용할 터미널에서 `python3 tools/sixel_probe.py`로 출력·지우기·모달 복원을 확인한 뒤 PNG/JPEG/PDF를 연다. 파일 목록 초점에서 Space 두 번과 빈 history `]`, 이미지/텍스트 빠른 선택, F1/F7 열기·닫기, Auto/Off, 이중 목록 전환, 연속 리사이즈, 49×8 축소·복원, r 새로고침을 확인한다. 같은 이미지의 상태 안내/마킹에서 이미지가 사라지지 않는지, 변경 시 옛 이미지가 남지 않는지, 텍스트와 테두리가 덮이지 않는지 확인한다. 특히 목록의 마킹 행이 이미지와 같은 높이에 놓이는 경우도 확인한다. 예기치 않은 소실·잔상은 r로 전체 복구하고 필요하면 기존 Off 설정을 사용한다. 터미널 이름/버전/TERM/셀 픽셀/폰트 배율과 direct/WSL/SSH 여부를 기록해야 실제 호환성 결과로 해석할 수 있다.
