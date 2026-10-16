CLI/README evidence, baseline 03b19db, 2026-10-16.
Ubuntu 24.04.5 host / GCC 13.3.0. No package changes, new OS, remote publish, tags or Release writes.

Read-only version discovery: git tag -n: empty; git ls-remote --tags origin: exit 0 empty; gh release list --limit 10 --json tagName,name,isDraft,isPrerelease: exit 0 [].

=== env PATH=; no HOME/XDG/TERM: tfile --help ===
exit: 0
stdout:
tfile - interactive terminal file manager

Usage: tfile [--] [directory]
       tfile --help | -h | --version

  --help, -h  Show this help and exit
  --version   Show program version and exit
  --          Treat the following argument as a directory

Without a directory, start in the current working directory.
Interactive use requires a TTY, valid TERM/terminfo and at least 50x9 cells.
After launch: F1 Help, F9 Menu. Help/version options must be used alone.
Documentation: https://github.com/DongminKang0919/terminal-file-manager#readme
stderr:

=== env PATH=; no HOME/XDG/TERM: tfile --version ===
exit: 0
stdout:
tfile 0.1.0-dev
stderr:

=== env PATH=; no HOME/XDG/TERM: tfile --bad ===
exit: 1
stdout:
stderr:
tfile: unknown option; use -- before a directory starting with '-'
Usage: tfile [--] [directory] | --help | -h | --version
Try 'tfile --help' for help.

=== env PATH=; no HOME/XDG/TERM: tfile --help path ===
exit: 1
stdout:
stderr:
tfile: use one directory or a single standalone option
Usage: tfile [--] [directory] | --help | -h | --version
Try 'tfile --help' for help.

=== /tmp/tfile-cli-install-final.log ===
sha256 1d9006501e051e69ed141c64d404d09fa42acc6fd8668bd2b8e604fe439a45cd
python3 tests/clean_source_check.py cc
cc  -D_XOPEN_SOURCE=700 -O2 -Wall -Wextra -Wpedantic -std=c11 -o tfile src/model.c src/core/batch.c src/core/external.c src/core/favorites.c src/core/files.c src/core/media.c src/core/mutation_job.c src/core/operations.c src/core/preview.c src/core/preview_job.c src/core/read_job.c src/core/search.c src/core/selection.c src/core/settings.c src/core/sort.c src/core/terminal.c src/platform/posix.c src/platform/posix_media.c src/platform/posix_external.c src/ui/batch.c src/ui/controller.c src/ui/dialogs.c src/ui/external.c src/ui/favorites.c src/ui/field.c src/ui/filter.c src/ui/graphics_probe.c src/ui/help.c src/ui/image_setup.c src/ui/input.c src/ui/loading.c src/ui/main.c src/ui/media.c src/ui/notice.c src/ui/operation.c src/ui/panels.c src/ui/path_query.c src/ui/preview.c src/ui/quick_find.c src/ui/search.c src/ui/terminal_input.c src/ui/text.c src/ui/text_window.c src/ui/theme.c src/ui/transfer.c src/ui/ui.c -lncursesw
PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, invalid combinations, real TTY -- paths/current directory and terminal restoration
cc  -D_XOPEN_SOURCE=700 -O2 -Wall -Wextra -Wpedantic -std=c11 -o tfile src/model.c src/core/batch.c src/core/external.c src/core/favorites.c src/core/files.c src/core/media.c src/core/mutation_job.c src/core/operations.c src/core/preview.c src/core/preview_job.c src/core/read_job.c src/core/search.c src/core/selection.c src/core/settings.c src/core/sort.c src/core/terminal.c src/platform/posix.c src/platform/posix_media.c src/platform/posix_external.c src/ui/batch.c src/ui/controller.c src/ui/dialogs.c src/ui/external.c src/ui/favorites.c src/ui/field.c src/ui/filter.c src/ui/graphics_probe.c src/ui/help.c src/ui/image_setup.c src/ui/input.c src/ui/loading.c src/ui/main.c src/ui/media.c src/ui/notice.c src/ui/operation.c src/ui/panels.c src/ui/path_query.c src/ui/preview.c src/ui/quick_find.c src/ui/search.c src/ui/terminal_input.c src/ui/text.c src/ui/text_window.c src/ui/theme.c src/ui/transfer.c src/ui/ui.c -lncursesw
cc  -D_XOPEN_SOURCE=700 -O2 -Wall -Wextra -Wpedantic -std=c11 -o tfile src/model.c src/core/batch.c src/core/external.c src/core/favorites.c src/core/files.c src/core/media.c src/core/mutation_job.c src/core/operations.c src/core/preview.c src/core/preview_job.c src/core/read_job.c src/core/search.c src/core/selection.c src/core/settings.c src/core/sort.c src/core/terminal.c src/platform/posix.c src/platform/posix_media.c src/platform/posix_external.c src/ui/batch.c src/ui/controller.c src/ui/dialogs.c src/ui/external.c src/ui/favorites.c src/ui/field.c src/ui/filter.c src/ui/graphics_probe.c src/ui/help.c src/ui/image_setup.c src/ui/input.c src/ui/loading.c src/ui/main.c src/ui/media.c src/ui/notice.c src/ui/operation.c src/ui/panels.c src/ui/path_query.c src/ui/preview.c src/ui/quick_find.c src/ui/search.c src/ui/terminal_input.c src/ui/text.c src/ui/text_window.c src/ui/theme.c src/ui/transfer.c src/ui/ui.c -lncursesw
PASS: Git-free version output, version-only dependency rebuild and restoration
PASS: clean HOME/XDG and no optional tools; create/delete/cancel, media reasons, Vim/xdg-open missing guidance, navigation/copy/move, no-argument startup, no unsolicited preview focus; built-in Help; non-TTY/TERM/startup errors and termios restoration; suite environment isolation
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
install -d -m 755 -- "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin"
install -m 755 -- tfile "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
install -d -m 755 -- "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin"
install -m 755 -- tfile "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
rm -f -- "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
install -d -m 755 -- "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin"
install -m 755 -- tfile "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
install -d -m 755 -- "/tmp/tfile-clean-source-vw6emdw6/stage root/usr/local/bin"
install -m 755 -- tfile "/tmp/tfile-clean-source-vw6emdw6/stage root/usr/local/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
install -d -m 755 -- "/tmp/tfile-clean-source-vw6emdw6/stage root/opt/tfile test/bin"
install -m 755 -- tfile "/tmp/tfile-clean-source-vw6emdw6/stage root/opt/tfile test/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
make: Entering directory '/tmp/tfile-clean-source-vw6emdw6/source'
rm -f -- "/tmp/tfile-clean-source-vw6emdw6/stage root/opt/tfile test/bin/tfile"
make: Leaving directory '/tmp/tfile-clean-source-vw6emdw6/source'
PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, invalid combinations, real TTY -- paths/current directory and terminal restoration
PASS: installed, source removed, unrelated cwd: cli_test.py
PASS: clean HOME/XDG and no optional tools; create/delete/cancel, media reasons, Vim/xdg-open missing guidance, navigation/copy/move, no-argument startup, no unsolicited preview focus; built-in Help; non-TTY/TERM/startup errors and termios restoration; suite environment isolation
PASS: installed, source removed, unrelated cwd: first_run.py
PASS: real-process settings save/restart, dual CLI path, startup warning and explicit replacement at 50x9/100x24
PASS: installed, source removed, unrelated cwd: settings_pty.py
PROCESS STOPPED {"identity": {"pid": 597, "state": "S", "ppid": 596, "pgid": 596, "start": 7826827, "exit": 0}, "observed": null, "reaped": true, "history": [null]}
PROCESS STOPPED {"identity": {"pid": 629, "state": "S", "ppid": 628, "pgid": 628, "start": 7827247, "exit": 0}, "observed": null, "reaped": true, "history": [null]}
PROCESS STOPPED {"identity": {"pid": 634, "state": "S", "ppid": 633, "pgid": 633, "start": 7827264, "exit": 0}, "observed": null, "reaped": true, "history": [null]}
PROCESS STOPPED {"identity": {"pid": 645, "state": "S", "ppid": 644, "pgid": 644, "start": 7827396, "exit": 0}, "observed": null, "reaped": true, "history": [null]}
PROCESS STOPPED {"identity": {"pid": 650, "state": "S", "ppid": 649, "pgid": 649, "start": 7827487, "exit": 0}, "observed": null, "reaped": true, "history": [null]}
PROCESS STOPPED {"identity": {"pid": 655, "state": "S", "ppid": 654, "pgid": 654, "start": 7828294, "exit": 0}, "observed": null, "reaped": true, "history": [null]}
PASS: PTY protocol ordering/bounds/clear/modal/resize, responsive selection/exit cancellation, focus cache, Auto/Off/files-only, PDF-first-page/text fallback, safe errors, unchanged text scrolling, zero/fake image, missing tools; real graphics remain unverified
PASS: installed, source removed, unrelated cwd: media_pty.py
PASS: lazy Auto PNG/JPEG/PDF, session reuse/restart/resize, ioctl, Off, failed/late/malformed replies, missing tools, selection/modal/child cleanup, UTF-8/mouse/keys, delayed startup and idle waits
PASS: installed, source removed, unrelated cwd: terminal_auto_pty.py
PASS: clean HOME/XDG and no optional tools; create/delete/cancel, media reasons, Vim/xdg-open missing guidance, navigation/copy/move, no-argument startup, no unsolicited preview focus; built-in Help; non-TTY/TERM/startup errors and termios restoration; suite environment isolation
PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, invalid combinations, real TTY -- paths/current directory and terminal restoration
PASS: staged binary first run from unrelated cwd, source removed
rm -f -- "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin/tfile"
rm -f -- "/tmp/tfile-clean-source-vw6emdw6/stage root/usr/local/bin/tfile"
rm -f -- "/tmp/tfile-clean-source-vw6emdw6/user home/.local/bin/tfile"
rm -f -- "/tmp/tfile-clean-source-vw6emdw6/stage root/usr/local/bin/tfile"
PASS: user PREFIX/spaces/0755/reinstall, default+explicit DESTDIR exact layout, source-independent UI/settings/help/media, idempotent uninstall preserves HOME/XDG/unrelated files/directories
PASS: clean source + install/uninstall, cc, isolated HOME/XDG, no optional tools; host OS/libraries

=== /tmp/tfile-cli-install-final.time ===
sha256 47b9f6ea477ec6dbc78fd7860a80a753950ddeacadcd96dc7b52b03baa4f47f6
wall=114.33 rss_kib=47896 exit=0

=== /tmp/tfile-cli-final-cases.log ===
sha256 a5c8d7fb484f54ccc43200f02968992b06a7dff08cb51d324c42f09cf4d5f4f5
python3 tests/cli_test.py
PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, invalid combinations, real TTY -- paths/current directory and terminal restoration

=== /tmp/tfile-cli-final-cases-san.log ===
sha256 5825c010ec53f455349e7b35e104c82fa14d5736f10f89faa751bb759839b3ee
PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, invalid combinations, real TTY -- paths/current directory and terminal restoration

=== /tmp/tfile-cli-lsan.log ===
sha256 fe95be9754bbb7e2f3b5fd94d549cdf2cebaf2843e1ee5ce1326be52ce4a3af1
exit: 1
stdout:
stderr:
==5==LeakSanitizer has encountered a fatal error.
==5==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==5==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)

=== /tmp/tfile-terminal-sanitizers-cli/summary.log ===
sha256 c20b75c27051b6bc4c21a38d90b2d8e5201c436212bc1cb4fe1b257384aa784e
### Startup, terminal and UI preparation sanitizer results
LSan: DISABLED — leak checks incomplete
- cli: PASS (exit 0, 0.571s)
- ux_queued_browse_pty: PASS (exit 0, 2.645s)
- ux_targets_pty: PASS (exit 0, 17.612s)
- startup_pty: PASS (exit 0, 6.534s)
- ui_io_pty: PASS (exit 0, 24.217s)
- ui_io_lifecycle_pty: PASS (exit 0, 17.776s)
- ui_io_normal_pty: PASS (exit 0, 8.815s)
- terminal: PASS (exit 0, 0.012s)
- input: PASS (exit 0, 0.019s)
- probe: PASS (exit 0, 0.024s)
- pty: PASS (exit 0, 55.927s)
- auto_pty: PASS (exit 0, 64.547s)
- vim_pty: PASS (exit 0, 6.998s)
- editor_shutdown_pty: PASS (exit 0, 1.647s)
- mark_discovery_pty: PASS (exit 0, 13.636s)
- reads_pty: PASS (exit 0, 14.536s)
Total: 16 required, 0 failed; 296.141s

=== /tmp/tfile-cli-terminal-san.time ===
sha256 830541ef734e6f7a5c03f81e155cea4a3a6e30bd11f73dec740ed7223686909b
wall=296.40 rss_kib=74084 exit=0

=== /tmp/tfile-sanitizers-cli/summary.log ===
sha256 fac63a30a18e63af4c1cc477d6ad13f1fb69abb4a6638f5d5ca4b7b5e0e78269
### Native sanitizer results
LSan: DISABLED — leak checks incomplete
- path_query_test: PASS (build exit 0, run exit 0)
- media_worker_test: PASS (build exit 0, run exit 0)
- preview_async_test: PASS (build exit 0, run exit 0)
- preview_worker_test: PASS (build exit 0, run exit 0)
- mutation_job_test: PASS (build exit 0, run exit 0)
- mutation_worker_test: PASS (build exit 0, run exit 0)
- read_job_test: PASS (build exit 0, run exit 0)
- performance_test: PASS (build exit 0, run exit 0)
- mark_policy_test: PASS (build exit 0, run exit 0)
- vim_test: PASS (build exit 0, run exit 0)
- favorites_test: PASS (build exit 0, run exit 0)
- filter_test: PASS (build exit 0, run exit 0)
- batch_conflict_test: PASS (build exit 0, run exit 0)
- external_test: PASS (build exit 0, run exit 0)
- trash_test: PASS (build exit 0, run exit 0)
- panels_ui_test: PASS (build exit 0, run exit 0)
- batch_test: PASS (build exit 0, run exit 0)
- batch_ui_test: PASS (build exit 0, run exit 0)
- operations_test: PASS (build exit 0, run exit 0)
- sort_test: PASS (build exit 0, run exit 0)
- progress_test: PASS (build exit 0, run exit 0)
- search_test: PASS (build exit 0, run exit 0)
- controller_test: PASS (build exit 0, run exit 0)
- history_ui_test: PASS (build exit 0, run exit 0)
- keyboard_ui_test: PASS (build exit 0, run exit 0)
- popup_style_test: PASS (build exit 0, run exit 0)
- progress_ui_test: PASS (build exit 0, run exit 0)
- preview_test: PASS (build exit 0, run exit 0)
- text_test: PASS (build exit 0, run exit 0)
- text_window_test: PASS (build exit 0, run exit 0)
- startup_ui_test: PASS (build exit 0, run exit 0)
- search_progress_ui_test: PASS (build exit 0, run exit 0)
- ownership_test: PASS (build exit 0, run exit 0)
Total: 33 required, 33 passed, 0 failed; 88.916s

=== /tmp/tfile-cli-native-san.time ===
sha256 c66b5d33b79f070475eb14536f0c557dda18296bb26ce1ed7d350730549b39ae
wall=89.11 rss_kib=144788 exit=0

=== Final tested binary/source identities ===
tfile da1f583aeb37fd2d1d07b9bb86b2f9b37a7383b396d5f3532773fccba04e2d4a
/tmp/tfile-terminal-sanitizers-cli/tfile d6dfb40a7ba25bcf4a1eb25d8668865583855b8d960cbbb23eabcf49a35aaaf2
src/version.h f52fcff570e029f38a169ce8d39d3105a96d3b907709a9ad052b872a9948ce36
src/ui/main.c ee1c3aca42fbcbd7fb8a330c13feeb2c9d1a95608d0ec39970198a314fdf3ff9
tests/cli_test.py e65c7528663042ca244958bffbe60bc63e5d1d60fb8b7baed8198c45c1a69397
tests/startup_ui_test.c b26e54fe2d90825c8cb2ec644ca7a2284c91ed1e68e21dc08164e06139a27411
tests/clean_source_check.py 38b596a7e0db5fb11367335defb7fbf9fa70366359975ab1b497de08cd8092ed
tests/install_check.py 03f7294f416355cccf3fc9392e5783cb1e80b11aae421551a453d4b07d906ffb

README: 207 lines before / 120 lines after. Image files unchanged; four legacy captures inspected, no new physical screenshot created.

FULL REGRESSION LOG SHA256 4f5a23fcbb96063942d87b0dde48f35e3160f7067a0b16d1d5a056b6d716b0ff

=== /tmp/tfile-cli-full.time ===
wall=716.09 rss_kib=155384 exit=0

=== Full regression selected coverage ===
PASS: CLI output/status matrix, no settings changes/control bytes, pipes/absent environment, invalid combinations, real TTY -- paths/current directory and terminal restoration
PASS: settings parsing, restart, panel isolation, 50x9 options, atomic failure preservation and path safety
PASS: real installed Vim edit/write/quit, hostile cursor path vs marks, -R, signal termination, resize, ncurses/input/mouse/preview restoration (automated PTY)
PASS startup error missing (50, 9)
PASS startup error file (50, 9)
PASS startup error denied (50, 9)
PASS startup error missing (100, 24)
PASS startup error file (100, 24)
PASS startup error denied (100, 24)
PASS noninteractive refusal and argument errors
PASS: single/dual pipe-gated batch copy/delete partial cancellation, move-between targets, mouse/Esc/resize, exact statuses/residue and queued background commands discarded

Static checks: check_docs.py, check_architecture.py, Python syntax and git diff --check: exit 0.
