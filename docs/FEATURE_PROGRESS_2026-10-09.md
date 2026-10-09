# 로컬 기능 확장 진행 — 2026-10-09

기준: `85ae0dd`. 최초 작업 트리 clean. Linux/WSL 로컬만 다루며 원격 푸시/릴리스/시스템 설정 변경은 하지 않는다. 모든 테스트 데이터는 전용 임시 디렉터리다.

| 단계 | 상태 | 커밋 | 검증과 제한 |
| --- | --- | --- | --- |
| 1 즐겨찾기 | 완료 | `eea1a18` | native favorites/settings + favorites PTY 통과; 실제 육안 미검증 |
| 2 목록 필터 | 완료 | `3c6b290` | filter native/PTY + batch/search/navigation/favorites 회귀 PASS |
| 3 충돌 건너뛰기 | 완료 | `d83cb11` + `a0a425b` + `5b53d1f` | copy/move native late collision + PTY/기존 batch 진행 회귀 PASS |
| 4 외부 도구 | 완료 | `45cf11d` | parser/process native + cooked terminal/resize restore PTY PASS |
| 5 휴지통 | 완료 | `3165cfe` | native/PTY PASS; 실제 별도 mount/desktop 복원 미검증 |
| 6 교차 FS 이동 | 보류 | `a156e10` | [안전 조건과 필요한 결정](CROSS_FILESYSTEM_MOVE_DESIGN_2026-10-09.md); EXDEV 거부 유지 |

완료는 코드·관련 회귀·문서·로컬 커밋까지 포함한다. 단계별 검사 및 통합/최종 전체 검사를 기록한다. PTY/정적 확인을 실제 터미널 육안 확인과 구분한다. 알려진 ptrace 제약의 LSan은 반복하지 않으며 ASan/UBSan 실제 범위와 최신 수동 명령을 별도로 남긴다.

세션 종료 상태: 1–5 코드·회귀·문서·로컬 커밋 완료, 1–5 최종 전체 및 ASan/UBSan PASS. 다음 행동은 위 수동 육안/desktop/LSan 검사이며, 6단계는 보류 설계의 원본 격리·동시 변경·복구 결정을 먼저 받아야 한다. 새 실행에서는 이 기록과 실제 git log/작업 트리를 대조한다. 실제 상태는 위 표와 git log 및 작업 트리를 함께 확인한다.

## 1단계 정책/검증

`b`/F9 Favorites → a 등록, r 이름, d 등록 해제, Enter 활성 패널 이동. 기존 navigate를 사용해 방문 기록/선택 복원을 연결한다. `$XDG_CONFIG_HOME/tfile/favorites.conf` 또는 절대 HOME의 `.config/tfile/favorites.conf`; settings.conf와 별도다. 부모 FD/NOFOLLOW/0600 임시 파일/fsync/원자 rename 저장을 재사용한다. 64개, label 128바이트, path 4095바이트, 전체 64KiB. 버전 헤더와 hex 인코딩으로 newline/raw byte 경로를 보존한다. 중복은 등록 시 canonical path 기준으로 거부한다. 저장 실패는 메모리 목록도 바꾸지 않는다. 불량 저장 데이터는 목록을 비우고 오류를 보존하며 변경 저장을 거부한다(파일 수동 복구 필요). 경로 접근 실패는 패널/방문 위치를 유지한다. 동시 외부 favorites.conf 수정 병합은 지원하지 않으며 마지막 원자 저장이 이긴다.

직접 실행: favorites native(저장 실패 주입·corrupt/symlink·한계·canonical duplicate·설정 분리·history·누락 경로), favorites PTY(등록/rename/restart/unregister/활성 이중 패널/접근 실패), 기존 settings native/core 및 architecture PASS. 로그 `/tmp/tfile-stage1-*.log`. PTY는 실제 터미널 픽셀/육안 확인이 아니다. 다음 행동: 패널별 현재 목록 필터 구현.

## 2단계 정책/검증

f/F9 Filter → Name contains 또는 full-name glob, Clear. 부분 일치는 기존 text_contains의 ASCII 대소문자 무시(Unicode 문자열 바이트 보존, 전체 Unicode case-fold 없음); glob은 platform의 POSIX fnmatch, 대소문자 구분 및 locale 문자 범위, `* ? []`/backslash escape. 길이 4095바이트 이하. 필터 입력은 shell로 전달하지 않는다. 필터 apply/clear/reapply 성공 시 모든 마킹을 제거한다. 읽기/할당 실패는 filter/list/marks를 유지한다. 앱 목록에는 결과만 남으므로 select-all/batch snapshot도 보이는 항목만 대상으로 한다. 패널 filter는 navigation/refresh/history에 유지하며 history 위치는 현재 필터 목록에서 선택 이름 우선/유효 index로 복원한다. 검색 결과 직접 열기는 성공 시 필터 해제(마킹 해제) 및 필요시 hidden 표시, 실패 시 기존 상태 유지. empty-directory/hidden으로 빈 목록과 filter no-match를 구분한다. 필터는 시작 설정으로 저장하지 않는다.

직접 실행: filter native(contains/glob/escaped bracket/한글, mark/batch 제한, 두 AppState, hidden/sort/navigation/history, 실제 비 root 권한 거부/실패 원자성, empty/no-match, search reveal), filter PTY(50x9/100x24, 입력/clear/marks/refresh/dual), 기존 batch native/search regressions/navigation/favorites PTY 및 architecture/diff PASS. 로그 `/tmp/tfile-stage2-*.log`. 다음 행동: 3단계 충돌 선택과 상태 모델 추가 후 1–3 통합 전체 검사.

## 3단계 정책/검증

복사/이동의 RESULT_EXISTS이며 해당 target.partial=false일 때만 Stop(default)/Skip this target/Skip all later name collisions. skip-all은 한 실행에만 적용한다. core preflight 충돌뿐 아니라 검증 뒤 native 실제 NOREPLACE 호출 직전 목적지 생성도 검사했다. 권한/I/O/부분 복사의 nested collision은 건너뛰지 않는다. 취소/Esc/resize는 충돌 대상 Cancelled, 나머지 Unexecuted, 완료한 성공/부분 변경은 유지. Stop은 충돌 target Failed, 나머지 Unexecuted. 성공만 unmark, skipped는 그대로 유지(목록에서 사라졌으면 refresh reconcile). 결과에는 Success/Skipped/Failed/Cancelled/Unexecuted 및 skipped 합계를 표시하고 모두 skip되어도 복사/이동됐다고 표시하지 않는다. frozen job과 destination은 선택 창에서 바뀌지 않으며 input을 progress callback/modal에서만 소비하고 종료 후 queued commands를 버린다. core callback 없는 호출의 기존 stop 정책도 유지한다.

직접 실행: batch_conflict native(각 copy/move, 사전 충돌·실제 늦은 no-clobber 충돌, skip/skip-all/stop/cancel, marks, 권한/partial failure), conflict PTY(copy/move 선택/Esc/resize/결과/marks), 기존 batch_test/batch_ui_test/batch_progress_pty 및 architecture/diff PASS. 로그 `/tmp/tfile-stage3-*.log`. 다음 행동: 1–3 통합 전체 검사 후 외부 editor/viewer.

1–3 최초 통합 전체 검사는 기존 panels_ui의 충돌 검사에서 실패했다. 기존 mock가 새 collision modal에 Esc를 보내 Cancelled로 끝난 것이 원인이었으며 기대 결과를 바꾸지 않았다. panels_ui/batch_ui/batch PTY가 새 창에서 Stop을 명시적으로 선택하도록 입력을 보완한 뒤 원래 RESULT_EXISTS/marks/partial/refresh assertions 그대로 모두 통과했다. 전체 검사 재실행 로그는 `/tmp/tfile-features-integration-1-3-final.log`로 분리한다.

추가 통합 검사에서 workflow의 실제 middle-collision 시나리오도 새 선택 창에서 Stop을 명시적으로 선택하지 않아 실패했다. 새 창의 존재를 확인하고 Stop을 누르게 보완했으며 기존 성공/실패/미실행/partial/마킹/원본 및 목적지 내용 검증은 유지했다. 50x9/80x24/160x32 workflow와 recovery 관련 검사는 PASS(`/tmp/tfile-stage3-workflow.log`, `/tmp/tfile-stage3-recovery.log`). 재실행은 1–4 통합으로 묶어 수행한다.

## 4단계 최초 정책/검증 (후속 Vim 요구로 변경)

F9 append-only Edit in editor / Open externally(커서 일반 파일; marks 무시). Enter unchanged. VISUAL→EDITOR→vi: 선택한 비어 있지 않은 설정이 불량/누락 도구이면 명확히 실패하며 몰래 하위 설정으로 바꾸지 않는다. 최대 4095바이트/32 args, whitespace 분리, single/double quotes, single quote 밖 backslash escape, quotes 안 셸처럼 보이는 문자는 literal. 따옴표 밖 `| & ; < > $ backtick ( )`, raw newline/CR, 끝 escape/미완료 quote를 거부한다. 변수/tilde/glob/치환/파이프 확장 없음. 비어 있는 인수는 허용하지만 argv[0]는 비어 있으면 거부. PATH는 절대 항목만 탐색; 명시적 editor 경로는 지원. 마지막 인자는 별도 absolute file argv; `execv`이며 ENOEXEC shell fallback이 없다. 외부 도구는 파일 경로를 다시 여므로 동시 외부 leaf 교체까지 inode snapshot으로 고정하지 않는다.

Editor: graphics/probe/preview/converter를 정리하고 curses program mode 저장/endwin; child 기본 신호, parent INT/QUIT 임시 ignore; 직접 waitpid(사용자 제어 수명, no deadline/RLIMIT/parent-death kill). 복귀 시 실제 ioctl geometry와 curses/input/queued resize를 복구하고 선택 이름 우선·marks·초점 정책·필터를 보존하며 active/peer refresh와 preview reset. 반환/refresh 실패는 별도 안내, 기존 retained file-operation notice는 덮어쓰지 않는다. GUI launcher: /dev/null stdin/out/err, exec ack + WNOHANG polling, 최대 하나, 종료 상태 안내. launcher가 계속 실행 중이어도 입력을 받을 수 있고 GUI-only polling은 main draw를 반복하지 않는다. 닫을 때 살아 있는 launcher는 죽이지 않고 nonblocking reap 추적; 앱 종료 후는 OS가 reparent한다. GUI 자체의 자식 프로세스 완료/문서 save/display는 추적하지 않는다. 기존 converter cleanup과 분리한다.

직접 실행: native parser/actual fork+exec(quoted argv/priority/vi, raw shell-looking executable filename, 실제 ENOEXEC script shell 미실행, parent INT restoration, inherited user AS limit, 추가 FD 없음, GUI exec/exit failure/live close→reap, FD baseline), PTY cooked editor/100x24→120x30 resize/직후 메뉴 입력/다른 marked file 보존/actual edited content/xdg request PASS. 복귀 후 남은 SIGWINCH의 KEY_RESIZE가 다음 F9를 닫던 문제를 재현하고 non-dispatch input drain으로 수정했다. 로그 `/tmp/tfile-stage4-*.log`. architecture/diff PASS. 실제 vi/desktop 문서 표시/실제 화면 육안 검사는 미검증. 다음 행동: 1–4 통합 검사와 5단계 Trash 지원 범위 구현.

## 5단계 구현 전 지원 범위 결정

공식 [freedesktop.org Trash Specification v1.0](https://specifications.freedesktop.org/trash/1.0/)을 2026-10-09 확인했다. 구현은 home Trash(절대 XDG_DATA_HOME, 아니면 HOME/.local/share)와 다른 device의 mount-top `.Trash/uid`(sticky/no-symlink 검사) → `.Trash-uid`를 다룬다. info의 O_EXCL 예약/Path percent encoding/local DeletionDate를 파일 이동보다 먼저 기록한다. source/files/info 부모 FD를 고정하고 NOREPLACE 이동; EXDEV는 copy-delete로 대체하지 않는다. UID/0700와 local filesystem 확인 실패는 원본을 보존하고 거부한다. 사용자별 생성된 setup 디렉터리는 실패 후에도 남을 수 있다. metadata 잔여물/이동 후 sync 실패는 부분 상태와 복구 위치를 보고한다. Crash-atomic한 2파일 transaction, in-app Trash explorer/restore, directorysizes cache는 이번 범위 밖이다. 기존 F8 영구 삭제는 그대로 별도 경고/확인을 유지하고 새 t/F9 Trash는 default Cancel 확인으로 구분한다.

## 5단계 정책/검증 및 복구 한계

`t`/F9 Move to Trash는 기본 Cancel 확인, F8/Delete는 기존 별도 영구 삭제 경고/확인이다. 마킹이 있으면 마킹 대상, 없으면 커서 항목; batch는 목록/경로를 고정한다. home Trash가 유효하고 동일 device이면 이를 사용한다. 다른 device이면 mount-top의 sticky/non-symlink `.Trash/uid`를 먼저 시도하고 `.Trash-uid`로 대체한다. home Trash가 불안전하거나 접근 불가능하면 자동 대체하지 않고 거부한다. mode 0700/user-owned root/files/info, 0600 info, NOFOLLOW ancestor, 같은 device를 확인한다. Linux ext4/btrfs/xfs/tmpfs/overlay만 허용하며 unknown/remote/DrvFS는 거부한다. regular/directory/symlink만 지원하고 mount-root/special type은 거부한다. 디렉터리는 inode 이동이므로 하위 내용을 복사/삭제하지 않는다. 링크 자체를 이동하며 링크 대상을 따라가지 않는다.

고유 tfile-time-pid-sequence 이름(원래 이름은 메타데이터에 보존), info O_EXCL 예약/완전 기록/file+info-dir fsync 뒤 검증된 부모 FD와 NOREPLACE rename. 같은 이름을 다시 버려도 덮어쓰지 않는다. Path는 canonical parent+original leaf, home는 absolute/mount-top은 relative, 바이트 percent encoding, local DeletionDate. source/files/info FD를 고정한 후 부모 경로 교체 테스트는 원래 디렉터리에서 작업하고 decoy를 보존했다. 최종 leaf identity 확인과 rename 사이에는 같은 UID의 교체 레이스가 남는다. home/top 디렉터리의 외부 이름 변경 뒤 보고된 문자열 경로가 낡을 수 있다. FD 고정이 모든 leaf 원자 검증이나 hostile same-UID 방어를 보장한다고 주장하지 않는다.

이동 전 기록/검증/취소 실패: 원본 보존, 자신이 예약한 info만 inode 확인 후 제거. 제거 실패는 orphan info 경로를 partial로 보고. metadata 기록 후 프로세스가 죽으면 원본과 orphan info가 함께 남을 수 있다. 이동 후 최종 fsync 실패는 completed_items=1/partial/error, 목적지+info 유지, 원본 위치에는 없다고 정확히 기록하며 마킹을 제거한다. payload와 info의 2-file crash transaction, 전원 장애 내구성, 자동 복구/rollback은 보장하지 않는다. directorysizes 캐시는 만들지 않으며(용량 탐색 기능 없음), 앱 내 Trash explorer/restore는 제외한다.

수동 복구: 먼저 `!`에서 completed/partial와 원본·목적지·diagnostic 경로를 확인한다. Original kept/orphan info이면 원본 존재와 대응 payload 부재를 직접 확인한 후 해당 info만 정리한다. Moved to Trash/final sync failed이면 payload와 대응 `.trashinfo`를 보존하고 데스크톱 복원을 사용한다. 같은 이름의 새 원본을 덮어쓰는 복원은 하지 않는다. setup 디렉터리는 실패 후 남을 수 있다. 실제 desktop restore/실제 별도 device mount-top/DrvFS 거부의 실환경 육안 검사는 미검증이다.

5단계 직접 검사: native 실제 파일/디렉터리/링크 이동·원래 inode/mode·percent metadata/date·동일 원래 이름 재등록·권한/링크/unknown FS 거부·기록/metadata fsync/최종 fsync/cleanup 오류 주입·NOREPLACE 늦은 충돌·취소·metadata-first 프로세스 crash·원본 부모 경로 교체·최종 leaf 사전 교체 거부·FD baseline·commit 후 sync-error mark 제거. filesystem별 shared/private fallback은 전용 임시 top FD를 주입한 **모의 routing 검사**이며 실제 다른 device 통과가 아니다. PTY 50x9/100x24: 기본 Cancel/마킹 batch/cancel/실제 payload+info/실패 후 원본 보존/별도 영구 삭제/resize 취소/활성 right panel. PASS(`/tmp/tfile-stage5-related-final.log`). 최초 새 테스트의 time.h 누락과 마킹 입력/작은 화면 알림 확인 오류는 테스트 코드에서 수정했고, actual filesystem assertions와 상세 결과 이유 검사를 유지했다. architecture/diff PASS. 다음: 로컬 5단계 커밋 후 6단계 보류 설계 및 최종 전체/sanitizer 검사.

1–4 통합 전체 검사 PASS: `/tmp/tfile-features-integration-1-4.log`. 앞선 두 실패를 위 3단계에 원인/수정 근거로 남겼다.


## 6단계 보류

[교차 파일시스템 이동 설계](CROSS_FILESYSTEM_MOVE_DESIGN_2026-10-09.md)에 원본 삭제 조건, 복사 검증, 메타데이터 범위, 복사/정리 취소, staging 복구 충돌, narrow regular-only 후보를 기록했다. 현재 copy+delete의 identity 확인은 최종 leaf 삭제와 원자 결합이 아니어서 복사하지 않은 대체 항목 삭제를 막는 보장이 부족하다. 원본 staging 격리와 재시작 recovery는 사용자 데이터 위치/복구 정책의 미해결 결정이므로 이 기능 구현을 멈춘다. 기존 EXDEV 거부와 원본 보존을 유지하며 실제 두 filesystem 통과를 주장하지 않는다. 다른 1–5 작업/검증은 독립적으로 완료한다.

## 사용자 최소 수동 확인 (미실행)

실제 사용할 Linux/WSL 터미널에서 **전용 임시 데이터/설정/휴지통**으로 실행한다. shell에서 만든 변수는 터미널을 닫기 전까지만 쓰며 시스템 설정은 바꾸지 않는다.

```sh
tfile_feature_demo=$(mktemp -d /tmp/tfile-feature-demo-XXXXXX)
mkdir -p "$tfile_feature_demo"/{left,right,config,data}
printf 'demo\n' > "$tfile_feature_demo/left/한글.txt"
printf 'conflict\n' > "$tfile_feature_demo/right/한글.txt"
printf 'second\n' > "$tfile_feature_demo/left/second.txt"
XDG_CONFIG_HOME="$tfile_feature_demo/config" XDG_DATA_HOME="$tfile_feature_demo/data" \
  VISUAL=vi ./tfile "$tfile_feature_demo/left"
```

1. `b` → `a` 이름 지정, 재시작 후 보존 확인, 이동/방문 back 확인, `d` 후 실제 디렉터리는 남는지 확인. F7의 미저장 변경을 즐겨찾기 저장이 settings.conf로 저장하지 않는지 확인한다.
2. Space로 마킹 후 `f` contains/glob 적용·해제: marks cleared, 필터 표식, no matches를 확인. F9 이중 목록과 Tab으로 패널마다 독립 동작 확인. 반대편 패널을 위 `right`로 열고 두 파일 마킹→F5→Skip this/Skip all/Stop의 파일·결과·마킹 확인.
3. F9 Edit in editor에서 현재 커서(다른 마킹 항목 아님)를 편집/복귀하며 터미널 resize/한글 입력/이미지 잔상·선택·마킹을 확인한다. 실제 xdg-open은 사용 가능할 때 임시 파일 하나로만 확인하며 “requested/returned”가 실제 표시·저장 완료 안내가 아닌지 확인한다.
4. `t` 기본 Cancel, 실제 Trash 이동, `!`의 위치/메타데이터를 확인. `F8`은 별도 permanent warning인지 확인. 데스크톱 복원은 일반 사용자 Trash에서 하는 별도 수동 환경 확인이 필요하며 이 임시 XDG Trash를 자동 인식한다고 가정하지 않는다. 앱 내 복원은 없다.

실제 terminal pixel/깜빡임/GUI 표시/vi 상호 작용, desktop Trash restore, 실제 별도 device mount-top 및 WSL/DrvFS에서의 거부 동작은 자동 PTY 결과로 대체하지 않는다. 교차 FS 이동은 보류여서 성공을 기대하는 수동 이동 검사는 하지 않는다.

## 최신 수동 LSan 명령 (이번 실행 아님)

이번 실행은 알려진 ptrace 제약에 따라 누수 검사를 끈 ASan/UBSan으로만 수행한다. 동일한 LSan 실패를 반복하지 않는다. ptrace가 없는 사용자의 터미널에서 아래 명령을 실행한 뒤 개별 run 로그를 확인해야 LSan 결과를 주장할 수 있다. suppressions는 사용하지 않는다. output 디렉터리는 기존 결과와 섞지 않게 별도 이름을 쓴다.

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-features-manual-lsan
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-features-manual-lsan
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py \
  --output-directory /tmp/tfile-terminal-sanitizers-features-manual-lsan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  python3 tests/isolated_check.py make check-settings-sanitize
```

기본 runner는 기존 18개에 favorites/filter/batch_conflict/external/trash 5개를 추가해 총 23개이다. media runner 5개, terminal runner 5개, settings native sanitizer 1개는 서로 별도 범위이며 중복 흐름이 있다. 이 합계를 모든 제품 흐름의 완전한 검사라고 표현하지 않는다.


## 최종 검증 (실제 실행)

| 검사 | 결과 | 실제 범위 / 로그 |
| --- | --- | --- |
| 최종 `make -j4 check` | PASS, exit 0 | 최신 1–5 native/PTY·기존 전체·실제 ImageMagick/Poppler fitting 포함; `/tmp/tfile-features-final-check.log` |
| 기본+새 기능 ASan/UBSan | PASS, 23개 | `--disable-leaks`; `/tmp/tfile-features-sanitizers.log`, `/tmp/tfile-sanitizers-features-final/*.run.log` |
| 미디어 ASan/UBSan | PASS, 5개 | media/preview/picker/media_pty/media_redraw, 누수 검사 제외; `/tmp/tfile-features-media-sanitizers.log` |
| 터미널 ASan/UBSan | PASS, 5개 | terminal/input/probe/pty/auto_pty, 누수 검사 제외; `/tmp/tfile-features-terminal-sanitizers.log` |
| 설정 ASan/UBSan | PASS, 1개 | settings_test, detect_leaks=0; `/tmp/tfile-features-settings-sanitizers.log` |
| Python AST | PASS, 49 파일 | tests/tools 문법만; 동작/모든 분기 증명 아님 |
| 계층 검사 / git diff --check | PASS | UI → core → platform, 좁은 native path guard, whitespace |
| 변경 문서 상대 파일 링크 | 누락 0 | README/진행/교차 FS 설계; 앵커·외부 URL·렌더링 미검증 |

LSan은 이번 세션에서 실행/재시도하지 않았다. 위 ASan/UBSan 결과를 LSan 통과로 확대하지 않는다. 실제 터미널 육안 확인은 하지 않았고 PTY는 출력/입력/파일 상태 검사다.


로컬 커밋: `eea1a18` favorites, `3c6b290` filter, `d83cb11` collision skip, `a0a425b`/`5b53d1f` 기존 collision 입력 회귀 보강, `45cf11d` external tools, `3165cfe` Trash, `a156e10` cross-FS 보류 설계. 최종 검증과 README의 삭제 정책 정리는 별도 `Record final feature validation and manual checks` 문서 커밋으로 남긴다. 원격 푸시/릴리스/전역 설정 변경 없음. 전체 완료가 아니라 **1–5 완료 / 6 보류**다.


## 후속 우선순위 조정 — Vim → 마킹 발견 가능성

추가 요구 시 기준 `d13e957`, 작업 트리 clean. 기존 즐겨찾기/필터/Trash/GUI 경로는 재작성하지 않는다. 새 우선순위 1 Vim을 먼저 검증·문서·커밋하고, 2 기존 마킹/일괄 작업 안내를 보완한다. GUI 추가 작업은 하지 않는다. 6단계 cross-FS 보류는 유지한다.

| 후속 작업 | 상태 | 커밋 | 검증 |
| --- | --- | --- | --- |
| Vim 명시 편집 | 완료 | `Edit cursor text files explicitly with Vim` 커밋 | native fixture + 실제 설치 Vim 9.1 PTY + 기존 external 회귀 PASS |
| 마킹/일괄 작업 발견 가능성 | 미착수 | — | 기존 `>`/`*`, panel marks, exactly-one-mark 경로를 재사용하고 안내/회귀 보강 |

Vim: `e`/F9 Edit with Vim, 커서 일반 텍스트 하나, marks/VISUAL/EDITOR 무시. 현재 파일을 실행 직전 다시 읽고 version 변경을 검사한다. 첫 64KiB(+UTF-8 경계 최대3바이트) UTF-8/ASCII, NUL/일부 control/media signature 거부. empty/extensionless/config/source/BOM 지원. SVG/XML처럼 텍스트 형식은 텍스트로 취급. 샘플 밖 바이너리와 UTF-8으로 우연히 해석 가능한 바이너리를 완전히 판별하지 못하며 다른 encoding 텍스트는 거부될 수 있다. 링크는 항상 거부, 대상 확인 후 편집 경로 없음. readable but not writable/mode write bits 없는 파일은 `-R`; no privilege escalation. `-R`은 :w!로 사용자가 바꿀 수 있는 advisory Vim 옵션이며 filesystem 권한은 별도다. argv `vim [-R] -- absolute-path`, 실행 missing/ENOEXEC/nonzero/signal은 오류, 다른 도구 fallback 없음. 기존 suspend/endwin/media cleanup/waitpid/signal restore/geometry/input/mouse/list/preview 복구를 재사용한다. 성공 종료는 save 완료가 아니다. 최종 경로를 Vim이 다시 열기 때문에 샘플 검사 이후 leaf/내용 교체 레이스는 남는다.

실제 실행 `/tmp/tfile-vim-related.log`: native UTF-8/media/invalid encoding/64KiB 경계·샘플 한계·읽기 전용·변경 감지·link/FIFO/dir 거부·환경변수 무시·argv/NO shell·missing/exit/signal/parent signal 복구 PASS. **실제 설치된 Vim 9.1**을 controlled .vimrc/임시 파일의 PTY에서 실행해 edit/:wq/:q!/SIGKILL/readonly -R/resize/커서-vs-mark/미리보기 갱신/직후 메뉴/termios와 mouse enable/실제 마우스 입력 복구 PASS. 사람이 실제 터미널을 보는 육안 검사는 하지 않았다. 기존 external native/PTY도 PASS. 최초 PTY 실패는 ncurses 전용 판독기가 Vim keyboard/device/style 시퀀스를 해석하지 못한 것이었고 Vim 전용 subclass만 해당 비그리기 시퀀스를 처리했다. 모든 실제 파일/argv/신호/복귀 assertions는 유지했다. PDF 거부 fixture는 image=Off로 별도 미디어 질의와 분리했다. architecture/diff PASS. 다음 행동: Vim 로컬 커밋 후 마킹 안내/정확히 한 마킹의 copy/move/delete/Trash 회귀 보강.
