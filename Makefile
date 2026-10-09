CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
UI_CPPFLAGS = -D_XOPEN_SOURCE=700
LDLIBS = -lncursesw
PLATFORM ?= posix
CORE_SOURCES = src/model.c $(wildcard src/core/*.c)
PLATFORM_SOURCES = src/platform/$(PLATFORM).c src/platform/$(PLATFORM)_media.c src/platform/$(PLATFORM)_external.c
UI_SOURCES = $(wildcard src/ui/*.c)
HEADERS = src/model.h $(wildcard src/core/*.h src/platform/*.h src/ui/*.h)

all: tfile

tfile: $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(LDLIBS)

tests/core_test: tests/core_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/core_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)

tests/platform_test: tests/platform_test.c src/model.c $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/platform_test.c src/model.c $(PLATFORM_SOURCES)

tests/operations_test: tests/operations_test.c src/model.c $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTFILE_TEST_HOOKS -o $@ tests/operations_test.c src/model.c $(PLATFORM_SOURCES) -Wl,--wrap=renameat2

tests/search_test: tests/search_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/search_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=lstat,--wrap=platform_directory_open,--wrap=platform_directory_next

tests/controller_test: tests/controller_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/controller_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=app_refresh,--wrap=confirm,--wrap=run_file_operation

tests/progress_test: tests/progress_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/progress_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)

tests/sort_test: tests/sort_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/sort_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)

check-core: tests/batch_test tests/ownership_test tests/sort_test tests/progress_test tests/core_test tests/platform_test tests/operations_test tests/search_test
	./tests/batch_test
	./tests/ownership_test
	./tests/sort_test
	./tests/progress_test
	python3 tests/check_architecture.py
	python3 tests/run_native.py
	python3 tests/run_operations.py
	python3 tests/run_regressions.py search

tests/text_test: tests/text_test.c src/ui/text.c src/ui/text.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/text_test.c src/ui/text.c

tests/text_window_test: tests/text_window_test.c src/ui/text.c src/ui/text_window.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/text_window_test.c src/ui/text.c src/ui/text_window.c $(LDLIBS)

tests/preview_test: tests/preview_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/preview_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=platform_reader_open,--wrap=platform_reader_line,--wrap=platform_reader_close,--wrap=platform_reader_changed,--wrap=readdir,--wrap=platform_directory_empty,--wrap=platform_directory_open,--wrap=platform_info,--wrap=app_refresh

tests/history_ui_test: tests/history_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/history_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=platform_directory_open

tests/keyboard_ui_test: tests/keyboard_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/keyboard_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=input_wide,--wrap=input_key,--wrap=app_refresh,--wrap=newwin,--wrap=delwin

tests/popup_style_test: tests/popup_style_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/popup_style_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS)

tests/tfile_search: tests/search_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/search_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(LDLIBS) -Wl,--wrap=core_search

tests/tfile_progress: tests/progress_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/progress_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(LDLIBS) -Wl,--wrap=core_transfer_progress,--wrap=core_delete_progress

tests/progress_ui_test: tests/progress_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/progress_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=core_monotonic_ms,--wrap=input_wide,--wrap=wrefresh

tests/startup_ui_test: tests/startup_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/startup_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=platform_directory_open,--wrap=qsort,--wrap=app_init,--wrap=input_key,--wrap=initscr,--wrap=terminal_session_check

tests/search_progress_ui_test: tests/search_progress_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/search_progress_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=platform_monotonic_ms,--wrap=core_monotonic_ms,--wrap=input_key,--wrap=input_wide,--wrap=wrefresh

check-isolated: tests/picker_test tests/media_test tests/panels_ui_test tests/tfile_batch tests/batch_ui_test tests/startup_ui_test tests/search_progress_ui_test tests/history_ui_test tests/keyboard_ui_test tests/popup_style_test tests/tfile_search tests/progress_ui_test tests/tfile_progress tests/preview_test tfile check-core tests/controller_test tests/text_test tests/text_window_test
	./tests/picker_test
	python3 tests/media_tools.py
	python3 tests/media_pty.py
	python3 tests/media_fit_pty.py
	python3 tests/media_redraw_measure.py --verify
	python3 tests/media_real_fit.py
	./tests/panels_ui_test
	./tests/batch_ui_test
	./tests/startup_ui_test
	./tests/search_progress_ui_test
	./tests/history_ui_test
	./tests/keyboard_ui_test
	./tests/popup_style_test
	./tests/progress_ui_test
	./tests/preview_test
	./tests/text_test
	./tests/text_window_test
	python3 tests/run_regressions.py controller
	python3 tests/progress_pty.py
	python3 tests/smoke.py
	python3 tests/first_run.py
	python3 tests/display_pty.py
	python3 tests/sort_pty.py
	python3 tests/layout_pty.py
	python3 tests/navigation_pty.py
	python3 tests/history_pty.py
	python3 tests/notice_pty.py
	python3 tests/quick_find_pty.py
	python3 tests/rename_pty.py
	python3 tests/transfer_form_pty.py
	python3 tests/keyboard_pty.py
	python3 tests/preview_focus_pty.py
	python3 tests/ux_consistency_pty.py
	python3 tests/search_ui_pty.py
	python3 tests/help_pty.py
	python3 tests/panels_pty.py
	python3 tests/dual_transfer_pty.py
	python3 tests/workflow_pty.py
	python3 tests/recovery_pty.py
	python3 tests/batch_destination_pty.py
	python3 tests/batch_pty.py
	python3 tests/batch_progress_pty.py

clean:
	rm -f tests/mark_policy_test tests/vim_test tests/trash_test tests/external_test tests/batch_conflict_test tests/filter_test tests/favorites_test tests/graphics_probe_test tests/terminal_test tests/terminal_input_test tests/settings_sanitize tests/settings_core_test tests/settings_test tfile tests/picker_test tests/media_test tests/panels_ui_test tests/tfile_batch tests/batch_test tests/batch_ui_test tests/startup_ui_test tests/search_progress_ui_test tests/ownership_test tests/history_ui_test tests/keyboard_ui_test tests/popup_style_test tests/tfile_search tests/sort_test tests/progress_ui_test tests/tfile_progress tests/progress_test tests/preview_test tests/core_test tests/platform_test tests/operations_test tests/search_test tests/controller_test tests/text_test tests/text_window_test

.PHONY: all clean check check-core

tests/ownership_test: tests/ownership_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/ownership_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free

tests/batch_test: tests/batch_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/batch_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=platform_move

tests/batch_ui_test: tests/batch_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/batch_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=app_refresh,--wrap=platform_directory_open,--wrap=platform_reader_line,--wrap=qsort,--wrap=core_monotonic_ms,--wrap=wrefresh,--wrap=input_key,--wrap=input_wide,--wrap=batch_prepare,--wrap=newwin,--wrap=delwin,--wrap=malloc,--wrap=calloc,--wrap=realloc

tests/tfile_batch: tests/batch_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/batch_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(LDLIBS) -Wl,--wrap=batch_execute


tests/panels_ui_test: tests/panels_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/panels_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=app_init,--wrap=app_refresh,--wrap=platform_directory_open,--wrap=platform_reader_line,--wrap=qsort,--wrap=newwin,--wrap=delwin,--wrap=input_key,--wrap=input_wide,--wrap=confirm,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free

tests/media_test: tests/media_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/media_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=platform_monotonic_ms

check-media: tests/media_test tests/picker_test tfile
	./tests/picker_test
	python3 tests/media_tools.py
	python3 tests/media_pty.py
	python3 tests/media_fit_pty.py
	python3 tests/media_redraw_measure.py --verify
	python3 tests/media_real_fit.py

.PHONY: check-media

tests/picker_test: tests/picker_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/picker_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=input_key,--wrap=prompt_value_status

tests/settings_test: tests/settings_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/settings_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=write,--wrap=close,--wrap=renameat,--wrap=openat,--wrap=fsync,--wrap=input_key

check-isolated: check-settings
check-settings: tests/settings_test tests/settings_core_test tfile
	./tests/settings_core_test
	./tests/settings_test
	python3 tests/settings_pty.py
.PHONY: check-settings

check-settings-sanitize:
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) -O1 -g -Wall -Wextra -Wpedantic -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer -o tests/settings_sanitize tests/settings_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=write,--wrap=close,--wrap=renameat,--wrap=openat,--wrap=fsync,--wrap=input_key
	ASAN_OPTIONS=$${ASAN_OPTIONS:-detect_leaks=0} UBSAN_OPTIONS=$${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1} ./tests/settings_sanitize
.PHONY: check-settings-sanitize

tests/settings_core_test: tests/settings_core_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/settings_core_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)

check:
	+python3 tests/isolated_check.py $(MAKE) check-isolated
.PHONY: check-isolated

tests/terminal_test: tests/terminal_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/terminal_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)
tests/terminal_input_test: tests/terminal_input_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/terminal_input_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=core_monotonic_ms
tests/graphics_probe_test: tests/graphics_probe_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/graphics_probe_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=core_monotonic_ms,--wrap=core_terminal_pixels,--wrap=terminal_query,--wrap=terminal_query_cells
check-terminal: tests/terminal_test tests/terminal_input_test tests/graphics_probe_test tfile
	./tests/terminal_test
	./tests/terminal_input_test
	./tests/graphics_probe_test
	python3 tests/terminal_pty.py
	python3 tests/terminal_auto_pty.py
check-isolated: check-terminal
.PHONY: check-terminal

tests/favorites_test: tests/favorites_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/favorites_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=platform_favorites_write
check-favorites: tests/favorites_test tfile
	./tests/favorites_test
	python3 tests/favorites_pty.py
check-isolated: check-favorites
.PHONY: check-favorites

tests/filter_test: tests/filter_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/filter_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)
check-filter: tests/filter_test tfile
	./tests/filter_test
	python3 tests/filter_pty.py
check-isolated: check-filter
.PHONY: check-filter

tests/batch_conflict_test: tests/batch_conflict_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/batch_conflict_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=platform_move,--wrap=platform_copy_progress
check-conflicts: tests/batch_conflict_test tfile
	./tests/batch_conflict_test
	python3 tests/batch_conflict_pty.py
check-isolated: check-conflicts
.PHONY: check-conflicts

tests/external_test: tests/external_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/external_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)
check-external: tests/external_test tfile
	python3 tests/external_tools.py
	python3 tests/external_pty.py
check-isolated: check-external
.PHONY: check-external

tests/trash_test: tests/trash_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTFILE_TRASH_TEST_HOOKS -o $@ tests/trash_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=fsync,--wrap=write,--wrap=unlinkat,--wrap=fstatfs,--wrap=fchmod,--wrap=mkdirat
check-trash: tests/trash_test tfile
	./tests/trash_test
	python3 tests/trash_pty.py
check-isolated: check-trash
.PHONY: check-trash

tests/vim_test: tests/vim_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/vim_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=platform_reader_peek
check-vim: tests/vim_test tfile
	python3 tests/vim_tools.py
	python3 tests/vim_pty.py
check-isolated: check-vim
.PHONY: check-vim

tests/mark_policy_test: tests/mark_policy_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/mark_policy_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS)
check-marking: tests/mark_policy_test tfile
	./tests/mark_policy_test
	python3 tests/mark_discovery_pty.py
check-isolated: check-marking
.PHONY: check-marking
