#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool drop_done, block_open, fail_malloc;
bool __real_platform_task_send(PlatformTaskWriter *, unsigned, const void *, size_t);
bool __wrap_platform_task_send(PlatformTaskWriter *writer, unsigned type, const void *data, size_t len) {
    if (drop_done && type == 4) { fputs("Injected worker exit\n", stderr); return false; }
    return __real_platform_task_send(writer, type, data, len);
}
Result __real_platform_directory_open(const char *, PlatformDirectory **);
Result __wrap_platform_directory_open(const char *path, PlatformDirectory **out) {
    if (block_open) { struct timespec delay = {2, 0}; nanosleep(&delay, NULL); }
    return __real_platform_directory_open(path, out);
}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t len) {
    if (fail_malloc) { fail_malloc = false; return NULL; }
    return __real_malloc(len);
}
static void pause_tick(void) { struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL); }
static void ready(void) {
    uint64_t began = core_monotonic_ms();
    while (core_read_cleanup_pending()) { assert(core_monotonic_ms() - began < 1000); pause_tick(); }
}
static SearchResult collect(ReadJob *job, char **directory) {
    bool done = false; uint64_t began = core_monotonic_ms();
    while (!done) { (void)core_read_poll(job, &done); assert(core_monotonic_ms() - began < 5000); if (!done) pause_tick(); }
    SearchResult result; core_read_take(job, &result, directory); core_read_close(job); ready(); return result;
}
static int descriptors(void) {
    DIR *d = opendir("/proc/self/fd"); assert(d); int count = 0;
    while (readdir(d)) count++;
    assert(!closedir(d)); return count;
}
static void same(const FileList *a, const FileList *b) {
    assert(a->len == b->len);
    for (size_t i = 0; i < a->len; i++) {
        assert(!strcmp(a->entries[i].name, b->entries[i].name));
        assert(!strcmp(a->entries[i].path, b->entries[i].path));
        assert(a->entries[i].kind == b->entries[i].kind && a->entries[i].size == b->entries[i].size);
    }
}
static bool cancel_load(size_t entries, bool complete, bool waiting, void *context) {
    (void)entries; (void)waiting;
    unsigned *calls = context; (*calls)++;
    return complete;
}
int main(void) {
    char root[] = "/tmp/tfile-read-job-XXXXXX"; assert(mkdtemp(root));
    for (unsigned i = 0; i < 1025; i++) {
        char name[32]; snprintf(name, sizeof name, "entry-%04u", i);
        assert(core_create(root, name, false).code == RESULT_OK);
    }
    assert(core_create(root, "nested", true).code == RESULT_OK);
    char *nested = core_path_join(root, "nested"); assert(nested);
    assert(core_create(nested, "child", false).code == RESULT_OK);
    assert(core_create(root, ".hidden", false).code == RESULT_OK);
    int before = descriptors();
    const char *terms[] = {"", "entry", "child", "absent"};
    for (unsigned i = 0; i < 4; i++) {
        SearchResult sync = core_search(root, terms[i], search_default_limits(), NULL, NULL);
        ReadJob *job; assert(core_read_search_start(root, terms[i], search_default_limits(), &job).code == RESULT_OK);
        SearchResult async = collect(job, NULL);
        assert(sync.result.code == RESULT_OK && async.result.code == RESULT_OK);
        assert(sync.visited == async.visited && !async.stopped && !async.incomplete);
        same(&sync.matches, &async.matches); search_result_free(&sync); search_result_free(&async);
    }
    ReadJob *job;
    SearchLimits limits = search_default_limits(); limits.max_results = 65;
    assert(core_read_search_start(root, "", limits, &job).code == RESULT_OK);
    SearchResult result = collect(job, NULL);
    assert(result.limited && result.matches.len == 65); search_result_free(&result);
    assert(core_read_search_start("/no/such/tfile-root", "", limits, &job).code == RESULT_OK);
    result = collect(job, NULL); assert(result.result.code == RESULT_NOT_FOUND); search_result_free(&result);
    FileList expected; assert(core_list(root, false, false, &expected).code == RESULT_OK);
    assert(core_read_directory_start(root, ".", false, false, &job).code == RESULT_OK);
    char *directory; result = collect(job, &directory);
    assert(result.result.code == RESULT_OK && !strcmp(directory, root)); free(directory);
    if (result.matches.len > 1) qsort(result.matches.entries, result.matches.len, sizeof(FileInfo), file_compare);
    same(&expected, &result.matches); file_list_free(&expected); search_result_free(&result);
    drop_done = true;
    assert(core_read_search_start(root, "entry", search_default_limits(), &job).code == RESULT_OK);
    drop_done = false;
    result = collect(job, NULL);
    assert(result.result.code == RESULT_IO && result.matches.len == 1025 && strstr(result.result.detail, "Injected worker exit"));
    search_result_free(&result);
    assert(core_read_search_start(root, "entry", search_default_limits(), &job).code == RESULT_OK);
    fail_malloc = true;
    result = collect(job, NULL); assert(result.result.code == RESULT_NO_MEMORY); search_result_free(&result);
    AppState app; assert(app_init(&app, root).code == RESULT_OK);
    assert(app_mark_all(&app).code == RESULT_OK);
    FileInfo *old = app.files.entries; char *old_directory = app.directory;
    size_t old_marks = app.marks_len, old_history = app.history_len;
    unsigned calls = 0;
    app.load_progress = cancel_load; app.load_context = &calls; block_open = true;
    assert(app_navigate(&app, nested).code == RESULT_CANCELLED);
    block_open = false;
    assert(calls == 2 && app.files.entries == old && app.directory == old_directory);
    assert(app.marks_len == old_marks && app.history_len == old_history);
    ready(); app_free(&app);
    for (unsigned i = 0; i < 32; i++) {
        block_open = true;
        assert(core_read_search_start(root, "", limits, &job).code == RESULT_OK);
        block_open = false;
        core_read_cancel(job); result = collect(job, NULL);
        assert(result.stopped); search_result_free(&result);
    }
    assert(descriptors() == before);
    /* Measure a larger listing separately from the fault/cancellation fixtures. */
    for (unsigned i = 0; i < 9000; i++) {
        char name[32]; snprintf(name, sizeof name, "bulk-%05u", i);
        assert(core_create(root, name, false).code == RESULT_OK);
    }
    uint64_t began = core_monotonic_ms();
    assert(core_list(root, true, false, &expected).code == RESULT_OK);
    uint64_t sync_ms = core_monotonic_ms() - began;
    began = core_monotonic_ms();
    assert(core_read_directory_start(root, ".", true, false, &job).code == RESULT_OK);
    result = collect(job, &directory); free(directory);
    assert(result.result.code == RESULT_OK);
    qsort(result.matches.entries, result.matches.len, sizeof(FileInfo), file_compare);
    uint64_t async_ms = core_monotonic_ms() - began;
    same(&expected, &result.matches);
    printf("large listing: %zu entries, sync %llu ms, isolated %llu ms (1 ms test polling)\n",
           expected.len, (unsigned long long)sync_ms, (unsigned long long)async_ms);
    file_list_free(&expected); search_result_free(&result);
    assert(descriptors() == before);
    assert(core_delete(root).code == RESULT_OK); free(nested); core_read_shutdown();
    puts("PASS: async/sync result equality, tail results, recursion, hidden list, limits, errors, interrupted IPC, parent OOM, transactional cancellation, 32 cancellations and FD/child cleanup");
}
