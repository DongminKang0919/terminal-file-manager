CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
UI_CPPFLAGS = -D_XOPEN_SOURCE=700
LDLIBS = -lncursesw
PLATFORM ?= posix
CORE_SOURCES = src/model.c $(wildcard src/core/*.c)
PLATFORM_SOURCES = src/platform/$(PLATFORM).c
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
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTFILE_TEST_HOOKS -o $@ tests/operations_test.c src/model.c $(PLATFORM_SOURCES)

tests/search_test: tests/search_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/search_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) -Wl,--wrap=lstat,--wrap=platform_directory_open,--wrap=platform_directory_next

tests/controller_test: tests/controller_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/controller_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=app_refresh,--wrap=confirm,--wrap=run_file_operation

tests/progress_test: tests/progress_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/progress_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES)

check-core: tests/progress_test tests/core_test tests/platform_test tests/operations_test tests/search_test
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
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/preview_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=platform_reader_open,--wrap=platform_reader_line,--wrap=platform_reader_close

tests/tfile_progress: tests/progress_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/progress_gate.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(LDLIBS) -Wl,--wrap=core_transfer_progress,--wrap=core_delete_progress

tests/progress_ui_test: tests/progress_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(UI_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(UI_CPPFLAGS) $(CFLAGS) -o $@ tests/progress_ui_test.c $(CORE_SOURCES) $(PLATFORM_SOURCES) $(filter-out src/ui/main.c,$(UI_SOURCES)) $(LDLIBS) -Wl,--wrap=core_monotonic_ms,--wrap=input_wide,--wrap=wrefresh

check: tests/progress_ui_test tests/tfile_progress tests/preview_test tfile check-core tests/controller_test tests/text_test tests/text_window_test
	./tests/progress_ui_test
	./tests/preview_test
	./tests/text_test
	./tests/text_window_test
	python3 tests/run_regressions.py controller
	python3 tests/progress_pty.py
	python3 tests/smoke.py
	python3 tests/display_pty.py

clean:
	rm -f tfile tests/progress_ui_test tests/tfile_progress tests/progress_test tests/preview_test tests/core_test tests/platform_test tests/operations_test tests/search_test tests/controller_test tests/text_test tests/text_window_test

.PHONY: all clean check check-core
