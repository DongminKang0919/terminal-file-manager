#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum Fault { NONE, CHILD_OOM, METADATA_GONE, METADATA_OOM, READ_ERROR, DISAPPEAR };
static enum Fault fault;
static const char *fixture;
static unsigned next_calls;
static void ok(Result r) { if (r.code) fprintf(stderr, "%s\n", r.detail); assert(r.code == RESULT_OK); }
static char *path(const char *name) { char *p = core_path_join(fixture, name); assert(p); return p; }
static bool ends(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix); return a >= b && !strcmp(text + a - b, suffix);
}
int __real_lstat(const char *path, struct stat *out);
int __wrap_lstat(const char *p, struct stat *out) {
    if (ends(p, "/vanished") && (fault == METADATA_GONE || fault == METADATA_OOM)) {
        errno = fault == METADATA_GONE ? ENOENT : ENOMEM; return -1;
    }
    return __real_lstat(p, out);
}
Result __real_platform_directory_open(const char *, PlatformDirectory **);
Result __wrap_platform_directory_open(const char *p, PlatformDirectory **out) {
    if (fault == CHILD_OOM && ends(p, "/broken")) {
        *out = NULL; return result_make(RESULT_NO_MEMORY, "Injected directory allocation failure");
    }
    if (fault == DISAPPEAR && !strcmp(p, fixture)) {
        char *hidden = path(".hidden"); assert(unlink(hidden) == 0); free(hidden); fault = NONE;
    }
    return __real_platform_directory_open(p, out);
}
Result __real_platform_directory_next(PlatformDirectory *, FileInfo *, bool *, Result *);
Result __wrap_platform_directory_next(PlatformDirectory *dir, FileInfo *out, bool *end, Result *metadata) {
    if (fault == METADATA_GONE || fault == METADATA_OOM || fault == READ_ERROR) {
        if (next_calls++ == 0) {
            /* Deliver one real match before the injected failure, independently
               of filesystem readdir ordering, to verify result retention. */
            char *known = path("found.txt"); Result r = platform_info(known, out); free(known);
            *end = false; if (metadata) *metadata = result_make(RESULT_OK, NULL); return r;
        }
        if (fault == READ_ERROR) {
            *out = (FileInfo){0}; *end = false;
            if (metadata) *metadata = result_make(RESULT_OK, NULL);
            return result_make(RESULT_IO, "Injected directory read failure");
        }
    }
    return __real_platform_directory_next(dir, out, end, metadata);
}
static bool cancel(const SearchResult *r, const char *p, void *context) {
    (void)p; size_t threshold = *(size_t *)context; return r->visited < threshold;
}
static void search_states(void) {
    char *locked = path("locked"); assert(chmod(locked, 0) == 0);
    SearchResult r = core_search(fixture, "found", search_default_limits(), NULL, NULL);
    ok(r.result); assert(r.incomplete && r.skipped_directories == 1 && !r.skipped_entries);
    assert(r.matches.len == 1 && !r.stopped && !r.limited);
    assert(r.first_omission.code == RESULT_ACCESS && !strcmp(r.first_omission.path, locked));
    search_result_free(&r); assert(chmod(locked, 0700) == 0); free(locked);
    r = core_search(fixture, "found", search_default_limits(), NULL, NULL);
    ok(r.result); assert(!r.incomplete && r.matches.len == 1 && !r.stopped && !r.limited); search_result_free(&r);
    for (enum Fault f = CHILD_OOM; f <= READ_ERROR; ++f) {
        fault = f; next_calls = 0;
        r = core_search(fixture, "", search_default_limits(), NULL, NULL);
        assert(r.matches.len > 0 && !r.stopped);
        if (f == METADATA_GONE) {
            ok(r.result); assert(r.incomplete && r.skipped_entries == 1);
            assert(r.first_omission.code == RESULT_NOT_FOUND && ends(r.first_omission.path, "/vanished"));
        } else {
            assert(r.result.code == (f == READ_ERROR ? RESULT_IO : RESULT_NO_MEMORY));
            assert(*r.result.path);
        }
        search_result_free(&r);
    }
    fault = NONE;
    size_t stop = 0;
    r = core_search(fixture, "", search_default_limits(), cancel, &stop);
    ok(r.result); assert(r.stopped && !r.matches.len); search_result_free(&r);
    for (unsigned i = 0; i < 80; ++i) {
        char name[32]; snprintf(name, sizeof name, "entry-%u", i); ok(core_create(fixture, name, false));
    }
    stop = 64;
    r = core_search(fixture, "", search_default_limits(), cancel, &stop);
    ok(r.result); assert(r.stopped && r.matches.len && r.visited == 64); search_result_free(&r);
    SearchLimits limits = search_default_limits(); limits.max_results = 1;
    r = core_search(fixture, "", limits, NULL, NULL);
    ok(r.result); assert(r.limited && r.matches.len == 1 && !r.stopped); search_result_free(&r);
    char *absent = path("absent"); r = core_search(absent, "", search_default_limits(), NULL, NULL);
    assert(r.result.code == RESULT_NOT_FOUND && !r.incomplete && !strcmp(r.result.path, absent));
    search_result_free(&r); free(absent);
}
static void hidden_selection(void) {
    AppState app; ok(app_init(&app, fixture)); app.show_hidden = false; ok(app_refresh(&app));
    char *hidden = path(".hidden"); size_t choice = SIZE_MAX; bool revealed;
    ok(app_open_search_result(&app, hidden, &choice, &revealed));
    assert(revealed && app.show_hidden && choice < app.files.len);
    assert(!strcmp(app.files.entries[choice].path, hidden));
    PreviewText text; ok(core_preview_text(hidden, 0, 1, &text));
    assert(text.len == 1 && !strcmp(text.lines[0], "hidden preview")); preview_text_free(&text);
    app.show_hidden = false; ok(app_refresh(&app));
    char *old_dir = app.directory; FileInfo *old_files = app.files.entries;
    size_t old_len = app.files.len, old_history = app.history_len, old_at = app.history_at;
    fault = DISAPPEAR; choice = 123;
    Result r = app_open_search_result(&app, hidden, &choice, &revealed);
    assert(r.code == RESULT_NOT_FOUND && choice == 123 && !revealed && !app.show_hidden);
    assert(app.directory == old_dir && app.files.entries == old_files && app.files.len == old_len);
    assert(app.history_len == old_history && app.history_at == old_at);
    char *locked = path("locked"); assert(chmod(locked, 0) == 0);
    r = app_open_search_result(&app, locked, &choice, &revealed); assert(r.code == RESULT_ACCESS);
    assert(app.directory == old_dir && app.files.entries == old_files && !app.show_hidden);
    assert(chmod(locked, 0700) == 0); free(locked);
    assert(chmod(fixture, 0) == 0); r = app_refresh(&app); assert(r.code == RESULT_ACCESS);
    assert(app.directory == old_dir && app.files.entries == old_files && app.history_len == old_history);
    assert(chmod(fixture, 0700) == 0); app_free(&app); free(hidden);
}
int main(int argc, char **argv) {
    assert(argc == 2 && geteuid() != 0); fixture = argv[1];
    search_states(); hidden_selection();
    puts("PASS: search omissions/metadata errors/OOM/cancellation/limits, retained results, hidden selection and transactional failures");
}
