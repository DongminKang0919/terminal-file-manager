# 로컬 기능 확장 진행 — 2026-10-09

기준: `85ae0dd`. 최초 작업 트리 clean. Linux/WSL 로컬만 다루며 원격 푸시/릴리스/시스템 설정 변경은 하지 않는다. 모든 테스트 데이터는 전용 임시 디렉터리다.

| 단계 | 상태 | 커밋 | 검증과 제한 |
| --- | --- | --- | --- |
| 1 즐겨찾기 | 완료 | `eea1a18` | native favorites/settings + favorites PTY 통과; 실제 육안 미검증 |
| 2 목록 필터 | 완료 | `3c6b290` | filter native/PTY + batch/search/navigation/favorites 회귀 PASS |
| 3 충돌 건너뛰기 | 완료 | `d83cb11` + `a0a425b` + `5b53d1f` | copy/move native late collision + PTY/기존 batch 진행 회귀 PASS |
| 4 외부 도구 | 완료 | `45cf11d` | parser/process native + cooked terminal/resize restore PTY PASS |
| 5 휴지통 | 완료 | `Move selected items to standards-based local Trash` 커밋 | native/PTY PASS; 실제 별도 mount/desktop 복원 미검증 |
| 6 교차 FS 이동 | 미착수 | — | 삭제 조건/identity/복사 완전성 설계가 먼저; 안전하지 않으면 보류 |

완료는 코드·관련 회귀·문서·로컬 커밋까지 포함한다. 단계별 검사 및 통합/최종 전체 검사를 기록한다. PTY/정적 확인을 실제 터미널 육안 확인과 구분한다. 알려진 ptrace 제약의 LSan은 반복하지 않으며 ASan/UBSan 실제 범위와 최신 수동 명령을 별도로 남긴다.

중단 시 다음 행동: 1–4 통합 전체 검사 결과를 확인하고 5단계 Trash 명세에 따른 안전 범위를 구현한다. 실제 상태는 위 표와 git log 및 작업 트리를 함께 확인한다.

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

## 4단계 정책/검증

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
