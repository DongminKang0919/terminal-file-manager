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

check-core: tests/core_test tests/platform_test tests/operations_test
	python3 tests/check_architecture.py
	python3 tests/run_native.py
	python3 tests/run_operations.py

check: tfile check-core
	python3 tests/smoke.py

clean:
	rm -f tfile tests/core_test tests/platform_test tests/operations_test

.PHONY: all clean check check-core
