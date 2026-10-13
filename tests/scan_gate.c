#define _GNU_SOURCE
#include "../src/platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static PlatformDirectory *watched;
static char watched_path[4096];
static unsigned calls;
static void hold(const char *path) {
    const char *control = getenv("TFILE_SCAN_GATE");
    if (control) {
        FILE *f = fopen(control, "r");
        char target[4096];
        if (f) {
            bool held = fgets(target, sizeof target, f) != NULL; fclose(f);
            if (held && !strcmp(target, path)) {
                char marker[4096]; snprintf(marker, sizeof marker, "%s.entered", control);
                f = fopen(marker, "w");
                if (f) { fprintf(f, "%ld\n", (long)getpid()); fclose(f); }
                while (access(control, F_OK) == 0) {
                    struct timespec pause = {0, 10000000}; nanosleep(&pause, NULL);
                }
            }
        }
    }
}
Result __real_platform_directory_open(const char *, PlatformDirectory **);
Result __wrap_platform_directory_open(const char *path, PlatformDirectory **out) {
    const char *phase = getenv("TFILE_SCAN_PHASE");
    if (!phase || strcmp(phase, "next")) hold(path);
    Result r = __real_platform_directory_open(path, out);
    const char *control = getenv("TFILE_SCAN_GATE");
    if (r.code == RESULT_OK && phase && !strcmp(phase, "next") && control) {
        FILE *f = fopen(control, "r"); char target[4096];
        if (f) {
            if (fgets(target, sizeof target, f) && !strcmp(target, path)) {
                watched = *out; snprintf(watched_path, sizeof watched_path, "%s", path); calls = 0;
            }
            fclose(f);
        }
    }
    return r;
}
Result __real_platform_directory_next(PlatformDirectory *, FileInfo *, bool *, Result *);
Result __wrap_platform_directory_next(PlatformDirectory *dir, FileInfo *out, bool *end, Result *metadata) {
    if (dir == watched && calls++ == 64) hold(watched_path);
    return __real_platform_directory_next(dir, out, end, metadata);
}
