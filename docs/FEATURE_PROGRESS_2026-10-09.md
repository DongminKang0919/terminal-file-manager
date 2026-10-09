# 로컬 기능 확장 진행 — 2026-10-09

최신 후속 상태: 휴지통 umask `fa7d198` / Vim 부모 종료 `0c63415` 안정화 완료(아래 후속 절). 이전 Vim `8198947`/`82b9134`, 마킹 `bbab3a2`/`949ac61`/`d862b9e` 관련 검증과 로컬 커밋 완료. 최신 전체 및 ASan/UBSan 통합 검사 PASS; 상세 범위와 미검증 수동 항목은 아래 후속 절에 기록한다. 기존 1–5/6단계 기록은 최초 확장의 이력이다.

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
  ./tfile "$tfile_feature_demo/left"
```

1. `b` → `a` 이름 지정, 재시작 후 보존 확인, 이동/방문 back 확인, `d` 후 실제 디렉터리는 남는지 확인. F7의 미저장 변경을 즐겨찾기 저장이 settings.conf로 저장하지 않는지 확인한다.
2. Space로 마킹 후 `f` contains/glob 적용·해제: marks cleared, 필터 표식, no matches를 확인. F9 이중 목록과 Tab으로 패널마다 독립 동작 확인. 반대편 패널을 위 `right`로 열고 두 파일 마킹→F5→Skip this/Skip all/Stop의 파일·결과·마킹 확인.
3. e / F9 Edit with Vim에서 현재 커서(다른 마킹 항목 아님)를 편집/복귀하며 터미널 resize/한글 입력/이미지 잔상·선택·마킹을 확인한다. 실제 xdg-open은 사용 가능할 때 임시 파일 하나로만 확인하며 “requested/returned”가 실제 표시·저장 완료 안내가 아닌지 확인한다.
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

최초 1–5 완료 시 기본 runner는 기존 18개에 favorites/filter/batch_conflict/external/trash 5개를 추가해 23개였다. 후속 Vim/mark_policy native를 추가한 현재 runner는 25개이다. media runner 5개, terminal runner 5개, settings native sanitizer 1개는 서로 별도 범위이며 중복 흐름이 있다. 이 합계를 모든 제품 흐름의 완전한 검사라고 표현하지 않는다.


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
| Vim 명시 편집 | 완료 | `8198947` + `82b9134` | native fixture + 실제 설치 Vim 9.1 PTY + 기존 external 회귀 PASS |
| 마킹/일괄 작업 발견 가능성 | 완료 | `bbab3a2` + `949ac61` + `d862b9e` | native/discovery PTY + batch/panels/controller/filter/Vim 관련 회귀 PASS |

Vim: `e`/F9 Edit with Vim, 커서 일반 텍스트 하나, marks/VISUAL/EDITOR 무시. 현재 파일을 실행 직전 다시 읽고 version 변경을 검사한다. 첫 64KiB(+UTF-8 경계 최대3바이트) UTF-8/ASCII, NUL/일부 control/media signature 거부. empty/extensionless/config/source/BOM 지원. SVG/XML처럼 텍스트 형식은 텍스트로 취급. 샘플 밖 바이너리와 UTF-8으로 우연히 해석 가능한 바이너리를 완전히 판별하지 못하며 다른 encoding 텍스트는 거부될 수 있다. 링크는 항상 거부, 대상 확인 후 편집 경로 없음. readable but not writable/mode write bits 없는 파일은 `-R`; no privilege escalation. `-R`은 :w!로 사용자가 바꿀 수 있는 advisory Vim 옵션이며 filesystem 권한은 별도다. argv `vim [-R] -- absolute-path`, 실행 missing/ENOEXEC/nonzero/signal은 오류, 다른 도구 fallback 없음. 기존 suspend/endwin/media cleanup/waitpid/signal restore/geometry/input/mouse/list/preview 복구를 재사용한다. 성공 종료는 save 완료가 아니다. 최종 경로를 Vim이 다시 열기 때문에 샘플 검사 이후 leaf/내용 교체 레이스는 남는다.

실제 실행 `/tmp/tfile-vim-related.log`: native UTF-8/media/invalid encoding/64KiB 경계·샘플 한계·읽기 전용·변경 감지·link/FIFO/dir 거부·환경변수 무시·argv/NO shell·missing/exit/signal/parent signal 복구 PASS. **실제 설치된 Vim 9.1**을 controlled .vimrc/임시 파일의 PTY에서 실행해 edit/:wq/:q!/SIGKILL/readonly -R/resize/커서-vs-mark/미리보기 갱신/직후 메뉴/termios와 mouse enable/실제 마우스 입력 복구 PASS. 사람이 실제 터미널을 보는 육안 검사는 하지 않았다. 기존 external native/PTY도 PASS. 최초 PTY 실패는 ncurses 전용 판독기가 Vim keyboard/device/style 시퀀스를 해석하지 못한 것이었고 Vim 전용 subclass만 해당 비그리기 시퀀스를 처리했다. 모든 실제 파일/argv/신호/복귀 assertions는 유지했다. PDF 거부 fixture는 image=Off로 별도 미디어 질의와 분리했다. architecture/diff PASS. 다음 행동: Vim 로컬 커밋 후 마킹 안내/정확히 한 마킹의 copy/move/delete/Trash 회귀 보강.


마킹 후속 정책: 고정 `>` cursor / `*` mark는 이미 구현돼 있어 재작성하지 않았다. Space/Marked 개수는 유지, 첫 화면과 마킹/해제 후 대상 정책 안내를 추가하고 active panel 변경에 맞춰 hint를 재계산한다. `a` 전체 visible mark / `u` active clear는 기존 API를 호출하며 직접 키는 Files focus에서만 동작한다. F9 항목의 같은 accelerator로 바로 실행 가능(메뉴 기존 순서 유지). input/preview에서는 해당 키로 마킹하지 않는다. 필터 footer가 기존 키 안내 위에 짧게 겹쳐 잔여 문자를 남기지 않도록 행을 비우고, Files focus에서는 Space/e/filter-clear, 넓은 화면에서는 a/u를 표시한다. Preview focus의 scroll footer는 유지한다.

copy/move/delete/Trash의 exactly-one-mark 우선 정책은 이미 구현되어 있었다. 이를 single-target helper로 재사용하고 목록에서 사라진 marked name을 cursor로 대체하지 않는다. Rename만 cursor를 사용하던 예외를 확인해 단일 mark 우선으로 맞췄다(다중 mark에서는 기존 disabled 유지). Vim은 cursor-only 예외를 그대로 명시한다. 마킹 없는 single-panel transfer form의 명시적 Source picker는 기존 사용자 선택 기능을 유지하며 초기 대상은 cursor다. batch 삭제 확인은 기존 count/default Cancel/전체 경로 paging을 유지하고 파일명 행과 첫 화면의 `Permanent deletion; no Trash` 안내를 추가했다. Trash는 별도 title/body/button, 영구 삭제 자동 fallback 없음.

성공 실제 대상만 unmark, skip/failure/cancel/unexecuted는 남아 있으면 유지, committed Trash의 sync-error는 실제 이동된 항목만 unmark라는 기존 정책을 유지한다. 양쪽 refresh/reconcile도 재사용한다. filter apply/clear가 marks를 전부 해제하고 loaded visible-only list를 사용하므로 감춰진 대상을 batch snapshot에 포함하지 않는다. 좌우 marks 독립, range selection 등 새로운 방식 없음.

직접 관련 검사 PASS: mark_policy native(단일/누락/다중 mark safe selection, filtered visible-only, panel ownership), 새 discovery PTY 50x9/100x24(기호/개수/안내/menu accelerator/direct keys, 실제 exactly-one copy/move/delete/Trash/rename, filter hidden item 보존, default Cancel/대상 목록/영구 label, narrow dual 독립), 기존 batch_ui/panels_ui/controller regressions, batch PTY/filter PTY, 최신 실제 Vim PTY. 로그 `/tmp/tfile-mark-discovery-*.log`. 최초 신규 메뉴 검사에서 화면 밖 Clear 항목을 검사한 입력을 수정했고, 확인 목록의 전체 경로 때문에 파일명이 여러 줄로 갈라지는 문제는 별도 filename 행을 추가해 확인하기 쉽게 만들었다. 실제 파일·대상·Cancel assertions를 유지했다. architecture/diff PASS. 다음: 최종 관련 검사 후 마킹 단위 커밋, 전체/sanitizer 통합.


후속 최초 통합 전체 검사 실패(`/tmp/tfile-vim-marks-final-check.log`): 새 target hint의 공간 예약이 50열에서 `Right*` active panel label을 밀어냈고 기존 settings_pty가 이를 잡았다. 제품 회귀로 인정하고 target hint만 최소 폭의 예약 공간을 줄여 패널과 Shown/Marked를 우선 보존했다. 기존 settings/sort 기대값은 그대로 유지하고 discovery PTY에도 50열 Right*/Left* assertions를 추가했다. settings_pty/discovery_pty/sort_pty PASS(`/tmp/tfile-vim-marks-narrow-*.log`). 최종 전체는 두 번째 로그로 재실행하며 이전 실패를 통과로 덮어쓰지 않는다.


두 번째 전체 검사(`/tmp/tfile-vim-marks-final-check-2.log`)는 navigation_pty에서 최소 폭의 기본 Shown/Marked/Dotfiles 요약이 줄어든 회귀를 잡았다. 최초 기본 target hint는 80열 이상에만 추가하고, 최소 폭에서는 기존 요약을 그대로 보존했다. 명시적 마킹/해제 후 대상 hint는 계속 표시한다. 기존 navigation/settings 기대값 그대로 PASS(`/tmp/tfile-vim-marks-metadata-navigation.log`, `/tmp/tfile-marking-menu-hint-settings.log`). F9 첫 화면의 예약 안내 행에도 a 전체 마킹 / u 해제 / e Vim cursor를 표시해, 아래로 스크롤하지 않아도 찾을 수 있게 했다. 메뉴 순서는 유지한다. 해당 discovery PTY PASS(`/tmp/tfile-marking-menu-hint-pty.log`).


Vim 입력 모드 복귀 보강: 실제 Vim SIGKILL 뒤 mode state를 추가 검사하니 bracketed paste(2004), focus reporting(1004), motion mouse(1002) 등이 남아 있었다(`/tmp/tfile-vim-keyboard-return.log`). termios/화면 복귀만으로 전체 입력 정책이 복구된다는 주장은 부족했다. [xterm 공식 control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)를 확인하고, 복귀 시 modifyOtherKeys=0 및 2004/1004/1002/1003 해제 후 curses의 기존 keypad/SGR mouse를 복구한다. 새 키보드/그래픽 프로토콜을 활성화하거나 파서를 추가한 것이 아니다. PTY 판독기는 기존에 무시하던 비그리기 keyboard/style 시퀀스를 이해하도록 확장했고, 실제 Vim 검사에는 종료 후 모드 상태 assertions를 추가했다(내부 함수 호출 순서 고정 없음). 정상/강제 종료/missing/exec-error에서도 전체 관련 검사 PASS(`/tmp/tfile-vim-keyboard-return-final.log`, `/tmp/tfile-vim-keyboard-external.log`). TERM=xterm-256color/Vim9.1/controlled .vimrc 범위이며, 다른 실제 터미널과 임의 Vim 설정의 모든 모드 변경을 검증한 것은 아니다. 사람이 보는 육안 검사는 여전히 미검증이다.


## 후속 실제 검증·수동 명령

| 검사 | 결과 | 실제 범위 |
| --- | --- | --- |
| 최종 make -j4 check | PASS, exit 0 | 최신 코드의 기존 전체 + Vim/marking/실제 미디어 변환; `/tmp/tfile-vim-marks-final-check-3.log` |
| ASan/UBSan 기본 runner | PASS, 25개 | 최신 코드, --disable-leaks; `/tmp/tfile-vim-marks-sanitizers-3.log` 및 `/tmp/tfile-sanitizers-vim-marks-final-3/*.run.log` |
| ASan/UBSan media runner | PASS, 5개 | media/preview/picker/media_pty/media_redraw, 누수 제외; `/tmp/tfile-vim-marks-media-sanitizers-3.log` |
| ASan/UBSan terminal runner | PASS, 5개 | terminal/input/probe/pty/auto_pty, 누수 제외; `/tmp/tfile-vim-marks-terminal-sanitizers-3.log` |
| ASan/UBSan 실제 Vim/마킹 PTY | PASS, 2 시나리오 묶음 | 최신 sanitized tfile + 실제 Vim 프로세스/VT 출력 모드 상태/파일·termios·mouse/input 복귀 및 marked-target 작업; `/tmp/tfile-vim-marks-vim-sanitized-pty-3.log`, `/tmp/tfile-vim-marks-marking-sanitized-pty-3.log` |
| 계층 / diff / Python AST | PASS | 좁은 architecture guard, whitespace, Python 52개 파일 **문법만** |
| 변경 문서 상대 파일 링크 | 누락 0 | README/진행 기록; anchors/렌더링 미검증 |

sanitizer로 빌드한 것은 tfile이며 시스템 Vim 자체를 sanitizer로 빌드한 것은 아니다. 위 sanitizer는 모두 detect_leaks=0이며 이번 후속에서도 LSan을 실행/반복하지 않았다. 최초 결과 및 실패 전체 로그는 별도로 남기고, 위 최신 실행으로 최종 범위를 구분했다. 설정 전용 sanitizer는 이번 후속에서 재실행하지 않았으며 settings native/PTY는 전체 검사에 포함했다. 실제 desktop GUI 표시·물리 터미널의 폰트/픽셀/깜빡임은 미검증이며 real-Vim **자동 PTY**와 사람이 보는 터미널 검사를 같다고 주장하지 않는다. GUI 기능은 기존 것을 유지했으며 추가 개발하지 않았다.

ptrace가 없는 터미널에서 최신 LSan을 수동 확인하려면 별도 결과 디렉터리를 사용한다(이 명령은 이번 실행 결과가 아니다):

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-vim-marks-manual-lsan
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py \
  --output-directory /tmp/tfile-media-sanitizers-vim-marks-manual-lsan
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py \
  --output-directory /tmp/tfile-terminal-sanitizers-vim-marks-manual-lsan
python3 tests/isolated_check.py env \
  TFILE_BINARY=/tmp/tfile-media-sanitizers-vim-marks-manual-lsan/tfile \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/vim_pty.py
python3 tests/isolated_check.py env \
  TFILE_BINARY=/tmp/tfile-media-sanitizers-vim-marks-manual-lsan/tfile \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/mark_discovery_pty.py
```

최소 육안 확인은 위 임시 데이터 실행 예시로 본인 Vim 설정/실제 Linux·WSL 터미널에서 한다: 다른 파일을 Space로 마킹하고 커서를 텍스트로 옮겨 e 실행 → 한글 입력/resize/:wq → 커서·마킹·내용·마우스·Tab 초점 복귀 확인. F9 첫 화면 a/u 안내와 기본 Cancel/실제 대상 목록도 확인한다. 강제 종료는 실제 사용자 파일 대신 임시 파일의 테스트 Vim에서만 수행한다. Ctrl-C/Shift-Tab/붙여넣기와 이미지 잔상은 실제 사용하는 터미널에서 확인한다.


후속 종료 상태: 요청한 Vim/마킹 두 항목은 코드·관련/전체 회귀·문서·로컬 커밋 완료. 원격 푸시/릴리스/시스템 전역 설정 변경 및 실제 사용자 파일 삭제 테스트 없음. 기존 즐겨찾기/필터/GUI/Trash 구조를 재사용했고 추가 개발은 이 두 항목에 한정했다. Cross-FS는 앞선 보류 설계를 유지한다. 다음 실행에서는 git log/작업 트리와 이 최신 검증 절을 대조한 뒤, 본인 터미널의 최소 육안/수동 LSan 검사를 진행한다. 후속 최종 검증은 `Record Vim and marking integration validation` 문서 커밋으로 남긴다.

## 후속 안정화: 휴지통 umask / 부모 종료 요청

기준 `98546d2`, 시작 작업 트리 clean, uid 1000(비 root). 두 지적은 최신 기준 소스에서도 실행 재현됐다. 이번 범위는 권한/종료 수명 관리이며 신규 기능과 광범위한 분리는 하지 않는다. 원격 푸시는 하지 않는다.

- 휴지통: 기존 정상 Trash + umask 0400에서 이동 후 닫은 메타데이터의 fopen 재열기 실패를 수정 전 HEAD 별도 빌드로 확인했다. 최초 생성 + umask 0200에서는 준비 단계 Permission denied가 재현됐다. 수정 후 새 디렉터리의 mkdirat 한 syscall에만 umask 0을 적용하고 성공/EEXIST/오류 직후 이전 mask와 errno를 복원한다. 현재 단일 스레드 전제가 필요하며 향후 스레드 도입 시 이 전역 mask 변경을 재검토해야 한다. 기존 디렉터리를 chmod하지 않고 NOFOLLOW/소유자/0700 검증을 유지한다. O_EXCL로 확보한 메타데이터 FD만 fchmod(0600)하며 실패 시 원본 이동 전에 본인 메타데이터를 정리한다.
- 종료: 대기하는 fixture Vim을 실행하고 부모 PID만 SIGTERM으로 종료하면 수정 전 부모는 종료되고 자식은 신호를 받지 않고 남았다(회귀 검사 실패 후 테스트에서 자식 정리). 수정 후 SIGTERM/SIGHUP 처리기는 sig_atomic_t 요청만 기록한다. 일반 대기 코드가 해당 자식 PID에 각 종류를 한 번 전달하고 waitpid로 회수한다. 관계없는 PID/프로세스 그룹 신호, 자동 SIGKILL, 기한은 없다. 무시/지연하는 편집기는 부모도 무기한 기다린다. SIGKILL로 부모를 강제 종료하는 경우는 정리 보장 밖이다. 반환 후 터미널을 복원하고 메인 루프에서 종료 요청을 처리해 앱을 정리한다. 종료 코드는 기존 정리 종료와 같은 0이며 저장 성공을 의미하지 않는다.
- 미디어 pause는 취소된 converter 회수만 수행하며 앱 종료 처리기를 유지한다. editor 임시 처리기는 정상/exec 실패/신호 종료/설치 실패 rollback에서 이전 상태로 복원한다. 종료 대기 중 waitpid(WNOHANG)+20ms 대기로 요청 확인과 blocking wait 사이의 lost wakeup을 피한다. 종료 이후 반복 요청은 이미 전달한 종류의 신호를 반복 전송하지 않는다.

회귀: trash_test는 비 root만 허용한다. 0022/0077/0200/0400 각각 기존/최초 Trash, 디렉터리 0700·메타데이터 0600·닫은 뒤 재읽기·umask 복원·mkdir/fchmod 오류 주입·원본/메타데이터 정리·재시도·최종 FD 개수를 확인한다. editor_shutdown_pty는 부모 TERM/HUP 각각을 전달하고 fixture가 정상 종료 기회를 가진 동안 부모가 기다리는지, 종료 후 child /proc 소멸(살아 있는 자식/좀비 없음), canonical/echo 셸 입력 복원을 확인한다. 기존 real Vim 자동 PTY의 저장/취소/읽기 전용/자식 SIGKILL/리사이즈/마우스 복귀를 유지한다. 실제 터미널 육안 확인, 실제 데스크톱 휴지통 복원, 모든 Vim 설정·터미널 에뮬레이터는 미검증이다.

최종 제품 코드 `0c63415` 검사: `make -j4 check` PASS(exit 0), 로그 `/tmp/tfile-stability-full-verified.log`. 이 전체 검사는 후속 terminal_pty 동기화 수정 전이며 그 수정의 최신 관련 결과는 별도 기록한다. 관련 `check-trash/check-vim`, 실행 직전 종료 요청을 추가한 `check-external/check-vim` PASS(`/tmp/tfile-stability-related.log`, `/tmp/tfile-stability-handoff.log`). 기본 ASan/UBSan 25개 PASS(`/tmp/tfile-stability-sanitizers-final.log`, `/tmp/tfile-sanitizers-stability-final`), media 5개 PASS(`/tmp/tfile-stability-media-sanitizers-final.log`), terminal 5개 PASS(`/tmp/tfile-stability-terminal-sanitizers-final.log`), 모두 detect_leaks=0. 최신 media sanitizer tfile로 editor_shutdown_pty 및 실제 Vim vim_pty를 추가 실행해 각각 PASS(`/tmp/tfile-stability-editor-sanitized-final.log`, `/tmp/tfile-stability-vim-sanitized-final.log`); tfile만 계측되며 시스템 Vim은 sanitizer 빌드가 아니다. architecture, Python 53개 AST, diff whitespace 검사 PASS.

로컬 커밋: `fa7d198` 휴지통 신규 생성 권한/umask, `0c63415` Vim 부모 종료/터미널 인계. 사용자 변경은 없었던 clean 기준에서 이번 범위만 변경했으며 원격 푸시는 하지 않았다. LSan은 알려진 ptrace 제약으로 이번에 실행/재시도하지 않는다. 시스템 보안 설정을 변경하지 않는다. 아래 명령을 일반 사용자 WSL/Linux 셸에서 실행하고 개별 로그를 확인한다(최신 코드 기준; 명령 제공은 통과 기록이 아님).

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py --output-directory /tmp/tfile-sanitizers-stability-manual-lsan
python3 tests/isolated_check.py python3 tests/run_media_sanitizers.py --output-directory /tmp/tfile-media-sanitizers-stability-manual-lsan
python3 tests/isolated_check.py python3 tests/run_terminal_sanitizers.py --output-directory /tmp/tfile-terminal-sanitizers-stability-manual-lsan
python3 tests/isolated_check.py env TFILE_BINARY=/tmp/tfile-media-sanitizers-stability-manual-lsan/tfile ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tests/editor_shutdown_pty.py
```

수동 최소 검사: 임시 텍스트에서 e로 Vim 진입→수정/:wq→마우스/메뉴/리사이즈를 확인한다. 다시 Vim 진입 후 별도 셸에서 정확한 tfile PID에 kill -TERM 또는 kill -HUP을 보내 Vim 종료와 셸 입력 복구를 확인한다(저장하지 않은 내용은 Vim의 신호 처리/복구 파일 정책을 따름). 전용 임시 XDG_DATA_HOME에서 umask 0400으로 실행→t 휴지통→종료 후 .trashinfo 읽기와 데스크톱 복원은 별도로 확인한다. 실제 사용자 파일은 검사에 사용하지 않는다.
