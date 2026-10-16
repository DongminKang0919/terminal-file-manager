# 작업 대상·하단 안내·전송 폼 개선 (2026-10-16)

기준은 `bcc9d54`이다. 작업 시작 시 사용자 변경은 없었다. 기존
[시작 응답성](STARTUP_VALIDATION_2026-10-16.md),
[비동기 UI I/O](UI_IO_VALIDATION_2026-10-16.md),
[UI 역할 표시](UI_ROLE_VALIDATION_2026-10-10.md)와 최신 구현을 확인했다.
새 기능·테마 엔진·executor·IPC를 추가하지 않고 UI 렌더링·안내·폼 배치를 수정했다.

## 확인한 기존 구현과 문제

| 영역 | 이미 있던 동작 | 개선 이유 |
| --- | --- | --- |
| 커서 / 마킹 / 초점 | 목록의 `>`·`*`, 비활성 커서 `:`, 제목의 `*`·초점 테두리, 독립 panel 상태 | 마킹한 행은 작은 `*`만 강조됐고 커서 이동 뒤 이름의 강조가 약했다 |
| 작업 대상 | 고정 하단 경계, Shown/Marked와 알림 영역 분리 | `Targets: 1 marked`의 문법, 좁은 긴 목록의 `*marks` 축약에서 대상 수가 사라졌다 |
| 하단 키 안내 | 실제 preview capability에 따른 안내, 완전한 key/label 단위 축약 | 파일 목록의 Copy가 빠지고 자주 쓰지 않는 키까지 나열됐다 |
| 단일 전송 | 목적지 → 이름 → 명시적 Run, 비동기 확인, Browse/Paths/Source, 고정 Base | 최소 크기에서 원본을 가리고 Base·To·Name만 보였다. 제목/Copy now에 대상 수가 없었다 |
| 다중 전송 | 원래 이름, 목적지 폼 → 전체 대상·종류·충돌 정책을 읽는 확인창, 기본 Cancel | Next/Execute가 무엇을 몇 개 실행하는지 설명하지 못했다 |
| 결과·테마 | Success / Partial / Cancelled / Error / Uncertain, 기존 semantic 색·반전 | 구조는 유지하고, 모든 상태에서 대상·조작 안내가 남는지 검증이 필요했다 |

## 유지한 대상 정책

마킹이 없으면 커서 항목, 하나라도 있으면 활성 패널의 마킹만 대상으로 한다.
정확히 한 마킹도 다른 커서 항목보다 우선한다. Vim은 커서 파일만 사용한다.
다중 Rename은 기존대로 허용하지 않는다. 반대편 마킹은 작업에 포함하지 않는다.

core/files.c와 selection.c를 확인했다. **필터 적용·변경·해제는 마킹을 지운다.**
필터 밖 마킹을 숨겨 보유했다가 작업에 포함하는 정책이 아니다. 숨김 표시 변경과
refresh는 현재 보이는 목록과 reconcile하며, 디렉터리 변경은 마킹을 지운다.
성공 대상은 해제하고 실패/취소/미실행 대상은 여전히 보이는 경우 유지하는 기존
정책도 그대로다. core의 대상 선정/파일 변경 코드는 수정하지 않았다.

## 화면과 폼 변경

- 마킹한 비커서 행은 고정 `*`와 **굵은 이름**을 기존 UI_MARK 색으로 표시한다.
  행 전체를 새 강조색으로 칠하지 않는다. 커서 반전/선택 색과 파일 metadata는 구분한다.
- 활성 목록 경계에 `Target: cursor item`, `Target: 1 marked item`,
  `Target: N marked items`를 표시한다. 좁고 긴 목록에서는 items를 먼저 생략하고
  `Target: N marked`와 범위를 남긴다. 극단적으로 공간이 부족한 기존 최종 fallback도
  있으며 상태줄 Marked 수는 유지한다. 이중 패널의 Left*/Right*·Marked와 제목 초점 표시는 유지한다.
- 파일 목록 안내는 Open / Mark / Copy / 가능한 Tab 전환 / Menu를 우선한다.
  Preview는 Esc: Files / Up/Down: Row / PgUp/PgDn: Page 등 실제 키를 표시한다.
  stale·빈 목록에서 Copy를, 스크롤 불가능한 Preview에서 Tab 전환을 안내하지 않는다.
  Vim cursor·나머지 명령은 공간에 따라 생략하며 F1/F9에서 찾을 수 있다.
  좁은 화면에서는 Tab이 필요한 경우 상단에 이미 있는 F9보다 먼저 표시한다.
- 기본 상태의 넓은 화면은 Focus: Files / Preview도 표시한다. 기존 명시적 상태·알림은
  유지한다. 결과가 대상 경계나 맨 아래 키 안내를 덮지 않는다.
- 단일 폼 제목/실행 버튼은 Copy 1 item / Move 1 item이다. 기본 최소 화면에
  From·To·Name을 함께 배치했다. 일반 크기는 Base·최종 Target·Paths를 함께 제공한다.
  작은 화면의 Browse/Paths/Source 초점에서는 To·Name·버튼을 함께 보이게 스크롤한다.
  전체 경로는 기존 Paths의 안전한 wrapping/paging으로 확인한다.
- 단일 입력란과 경로·이름을 같은 열에 맞추고 `>To:`/`>Name:`으로 현재 입력을 표시한다.
  버튼 앞 `>`도 초점을 뜻하며 버튼 label의 클릭 영역을 넓히지 않는다.
  대괄호 버튼은 클릭 가능, 맨 아래 key/label 안내는 설명이다. 기존 상태행 클릭으로
  결과 열기 기능은 유지한다.
- 다중 폼 제목은 `Copy N items | Batch destination`, 원본 기준은 From/Base이다.
  이름 입력을 추가하지 않고 원래 이름 정책을 명시한다. Review N items(좁으면 Review N)는
  검증·확인 단계이며, 확인창의 실제 실행 버튼은 Copy N items / Move N items이다.
  확인창은 Cancel이 기본이고 대상 종류·원본 목록·정규화 목적지·충돌 정책을 계속 보여준다.
- Unchecked / Checking destination / To checked로 목적지 확인 상태를 구분한다.
  To checked는 과거 조회 상태이며 실행 직전 검증을 보증하지 않는다. 단일 Paths나
  다중 review에서 돌아와도 다음 실제 동작은 다시 검증한다. 오류는 To 근처의 상태행과
  최소 화면 footer에 표시하며 Edit / retry 또는 Edit To / Browse 안내를 제공한다.
- 조회 중 Browse/Paths/Source/실행 버튼의 표시와 hit-test를 같은 enabled 조건으로
  맞추고 초점 이동에서도 건너뛴다. 입력 수정과 Cancel/Esc는 유지한다.
  전체 modal 버튼의 초점 표시에는 기존 반전·색·굵기와 `>`를 함께 쓴다.

색상은 기존 테마의 named pair만 재사용한다. 256색·8색·무색 셀에서 고정 기호·문구·
굵기·반전과 disabled 속성을 검증했다. 성공·부분 완료·취소·오류·불명확의 기존 라벨과
결과 보존, 알림 해제/상세 확인 동작을 바꾸지 않았다.

## 재현된 완료 결과 / Browse 경쟁 수정

기준 바이너리에 기존 `validation-ready` gate를 사용했다. UI가 목적지 조회 완료를
반영하기 직전에 멈춘 marker(PID=UI)를 확인하고 Browse 키를 queue했다. Browse에서
다른 목적지를 골라도 옛 pending Run과 query.directory가 남아 **이전 목적지로 복사**됐다.
`/tmp/tfile-ux-queued-browse-before.log`는 실제 옛 파일의 payload를 포함한 assertion 실패다.
고정 sleep으로 pending 상태를 추측하거나 코어 파일 검증을 생략하지 않았다.

새 Browse/Source 의도가 완성된 Run/조회 결과도 폐기하도록 했다. picker 취소로도 옛
Run이 부활하지 않는다. batch의 오래된 review 의도도 폐기하며, Browse만 취소한 것을
조회 성공으로 표시하지 않는다. 파일 변경 executor나 대상 정책 변경이 아니라 기존
“새 입력이 완료 결과보다 우선” 정책을 지키는 수정이다.

같은 gate 검사에서 50×9/100×24 모두 옛 목적지에 파일이 없고, 사용자가 새 실행 버튼을
선택한 뒤에만 새 목적지에 정확한 payload가 생기는 것을 확인했다. 원본은 보존했다.
필드 편집의 query 취소·이전 결과 폐기, 중복 Enter 억제, FD 기반 실행 직전 검증과
no-clobber·교체 방어는 기존 경로를 유지했다. Esc는 조회만 취소하고 입력·초점을 유지하며,
다음 Esc는 폼을 닫는다. Browse/확인창 취소·modal 종료의 패널 복원도 유지한다.

## 화면 기록과 확인의 한계

[같은 조건의 전후 PTY 기록 28쌍과 폼 이미지](UX_TARGETS_COMPARISON_2026-10-16.md)를 제공한다.
원본은 전후 각각 76개다. 단일 목록·Preview·dual, 0/1/2 마킹, 커서가 다른 상태,
단일/다중 Copy·Move, 목적지 오류·확인·조회 취소를 같은 fixture/mtime/크기로 실행했다.
HOME/XDG와 모든 source/destination은 임시 데이터다. 어떤 캡처도 파일 작업을 실행하지 않았다.
실제 작업 효과는 별도 회귀에서 검증했다.

PTY cell로 만든 단색 이미지를 열어 배치와 텍스트를 확인했다. 경로 치환이 원본 열 폭을
줄이는 것을 발견해 **치환 없는 원본 셀**로 다시 생성했다. 이미지에는 색상·굵기·반전이
없으며 실제 그래픽 터미널 스크린샷이나 색상·Sixel·깜빡임의 육안 검증이 아니다.
한글은 원본 텍스트 기록과 native wide-cell 검사로 확인하며, 렌더러의 CJK 폰트 지원을
제품의 실제 글꼴 확인으로 대신하지 않는다.

부분 완료·취소·불명확은 popup_style_test에서 실제 Result 형식을 주입한 native 셀
검사로 라벨과 대상/안내 보존을 검증한다. 이를 실제 파일 시스템 실패 장면의 PTY 캡처라고
보고하지 않는다. 실제 충돌·부분 결과·worker 통신 실패/불명확 정책은 기존 batch/mutation/
통합 회귀를 유지하여 확인한다.

## 검사 연결과 실행 기록

새 `check-ux-targets`를 전체 check-isolated에 연결했다. 세 모드 × 두 크기의 실제
copy/move 결과·마킹 우선·명시적 버튼·기본 Cancel·Tab/Shift+Tab·resize 6개와
완료 결과/Browse 경쟁 2개를 실행한다. popup_style_test는 기존 3종 터미널 × 4크기에
마킹 이름의 속성, 버튼 초점 기호, Success/Partial/Cancelled/Uncertain의 고정 대상과
키 안내 보존을 추가했다. 기존 assertion은 유지했다.

Makefile과 terminal sanitizer runner에 같은 두 PTY를 연결했다. runner는 기존 별도
일반/게이트 sanitizer binary를 사용하고 15개 필수 suite를 집계한다. native runner의
기존 popup_style_test/mark_policy_test 등도 유지한다. 기존 CI 단계가 두 runner와
전체 check를 실행하며 build/exit/PASS/FAIL 로그 및 failure artifact 경로를 그대로 사용한다.
기본 LSan, 보안·timeout·병렬도는 바꾸지 않았다.

중간 smoke 실패는 이전 Name 입력 좌표를 클릭한 것이 원인이었다. Browse/Source 행과
Name 입력 행의 새 좌표를 반영하고 파일 내용·충돌·원본 보존 assertion은 그대로 유지했다.
기존 Copy now/Move now/Execute 문자열과 대상 경계의 정확한 border 기대값도 새 문구에만
맞췄다. assertion 삭제나 timeout 증가는 하지 않았다. 최종 전체 회귀도 재실행해 통과했다.

LSan 기본 실행은 이 환경에서 ptrace fatal로 실패하여 누수 검사는 **미완료**다.
명시적 --disable-leaks의 ASan/UBSan 성공과 구분한다. 부모 정상 종료의 누수 범위와
`_exit` 읽기 worker 본문/외부 도구 내부의 전체 누수 검증은 동일하지 않다. 기존 정상
반환 preview/mutation/media worker-body harness는 native runner에 유지한다.
원격 푸시·이번 수정의 원격 CI, 실제 터미널 색상·픽셀 검사는 미실행이다.

## 재현과 수동 확인

```sh
make -j2 all
make check-ux-targets
make -j2 check
python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-ux-wsl
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-ux-wsl
# ptrace 환경에서만 명시적 대체 실행 (LSan 미완료)
python3 tests/run_sanitizers.py --disable-leaks --output-directory /tmp/tfile-sanitizers-ux-fallback
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --disable-leaks --output-directory /tmp/tfile-terminal-sanitizers-ux-fallback
```

화면 기록은 `python3 tests/ux_capture.py /tmp/tfile-ux-records`로 재생성한다.
비교에는 동일한 별도 fixture와 기준 바이너리를 사용했다. 단색 렌더러는 선택적인
ImageMagick 개발 도구이며 제품/CI 의존성에 추가하지 않았다.

실제 터미널에서 격리 연습 데이터를 만드는 짧은 명령:

```sh
python3 tests/isolated_check.py python3 -c 'import os,pathlib,subprocess; h=pathlib.Path(os.environ["HOME"]); s=h/"source"; s.mkdir(); (h/"dest").mkdir(); [(s/n).write_text("Preview line\n"*60) for n in ["a.txt","b-한글.txt","c.txt"]]; subprocess.run(["./tfile",str(s)],check=True)'
```

수동으로 확인할 항목:

1. 50×9와 100×24에서 커서를 옮겨도 `*`·굵은 마킹 이름과 대상 수가 남는지,
   실제 터미널의 256/8색/무색 대비와 한글·긴 경로가 읽히는지 확인한다.
2. Tab/Shift+Tab으로 Preview/dual/폼 초점을 옮기고 제목·입력·버튼의 표시와
   마우스 클릭 범위, 축소/복원 후 원래 선택·초점을 확인한다.
3. 마킹 하나를 다른 커서 항목과 구분해 Copy/Move와 e/Vim 예외를 확인한다.
   단일 폼의 Paths, 다중 Review의 원본/목적지·개수·Cancel 기본값을 확인한다.
4. 임시 destination 오류를 수정하고, 조회 취소·작업 알림/상세를 열어도 대상 경계와
   맨 아래 안내가 남는지 확인한다. 실제 Sixel·깜빡임·글꼴 확인은 별도 수동 결과가 필요하다.

## 최종 결과와 로그

| 검사 | 실제 결과 | 시간 / 로그 |
| --- | --- | --- |
| `make -j2 check` | 전체 PASS, exit 0 | 611.12초; `/tmp/tfile-ux-full-confirmed.log`와 `.time` |
| 새 대상/폼 PTY + queued Browse | 6 + 2 사례 PASS; 일반/최종 sanitizer 양쪽 실행 | 각 전체 로그 및 sanitizer 개별 suite 로그 |
| native ASan/UBSan `--disable-leaks` | 33/33 PASS, exit 0; 256/8/mono 셀 검사 포함 | wall 87.07초 / runner 81.318초; `/tmp/tfile-sanitizers-ux-final/` |
| terminal ASan/UBSan `--disable-leaks` | 15/15 PASS, exit 0 | wall 307.50초 / runner 288.386초; `/tmp/tfile-terminal-sanitizers-ux-final/` |
| 기본 popup-style LSan | exit 1, ptrace fatal; **미완료** | `/tmp/tfile-ux-lsan-final.log` |
| 비교 캡처 / 원본 셀 폭 | 전후 76개씩, 폭 유지 확인; 파일 작업 0회 | `/tmp/tfile-ux-before-records/`, `/tmp/tfile-ux-after-records/` |
| 키보드·연속 작업·레이아웃 별도 확인 | 모두 PASS | `/tmp/tfile-ux-tail-targeted.log` |
| 문서 / architecture / Python 구문 / diff 공백 | PASS | 최종 정적 확인 |

전체 회귀 최대 단일 프로세스 RSS는 155440 KiB, native sanitizer 144824 KiB,
terminal sanitizer 74092 KiB였다. 동시 프로세스 합산 peak는 아니다.

중간 `full-final` 실행은 옛 Name 클릭 좌표로 실패했다(318.56초, exit 2).
`full-verified` 실행에서는 기존 media_pty의 UI 종료 직후 converter PID 부재 assertion이
한 번 실패했다(174.00초, exit 2). 같은 검사를 기준/수정 바이너리에 각각 재실행한
`/tmp/tfile-ux-media-probe.log`는 둘 다 통과했다. 원인은 미확정이며 미디어 회수 코드나
assertion·timeout은 변경하지 않았다. 최종 전체 재실행은 위와 같이 통과했다. 이 한 번의
실패를 이번 UI 변경으로 해결했다고 주장하지 않는다. 새 Browse 경쟁을 발견하기 전의
검증은 수정 반영을 위해 중단했으며 최종 성공으로 집계하지 않았다.

[핵심 검사 출력·기준 경쟁 재현·최종 결과 증거](UX_TARGETS_EVIDENCE_2026-10-16.txt)를 보존했다.
검증한 변경과 문서는 로컬 커밋으로 남기며 원격 푸시는 하지 않는다.
