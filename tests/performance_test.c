#define _GNU_SOURCE
#include "../src/core/core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t copies;
char *__real_text_copy(const char *);
char *__wrap_text_copy(const char *text) {
    copies++;
    return __real_text_copy(text);
}
static void ok(Result r) { assert(r.code == RESULT_OK); }

int main(void) {
    char root[] = "/tmp/tfile-performance-XXXXXX";
    assert(mkdtemp(root));
    enum { ENTRIES = 2048, ROWS = 20000 };
    for (size_t i = 0; i < ENTRIES; i++) {
        char name[32]; snprintf(name, sizeof name, "entry-%06zu", i);
        ok(core_create(root, name, false));
    }
    copies = 0;
    uint64_t began = core_monotonic_ms();
    SearchResult search = core_search(root, "absent", search_default_limits(), NULL, NULL);
    ok(search.result);
    assert(search.visited == ENTRIES && !search.matches.len && !search.incomplete);
    assert(copies <= ENTRIES * 3 + 1);
    printf("search: %zu entries, %zu string copies, %llu ms\n", search.visited,
           copies, (unsigned long long)(core_monotonic_ms() - began));
    search_result_free(&search);

    /* Result growth inside recursion must not invalidate a matched parent's path. */
    ok(core_create(root, "nested", true));
    char *nested = core_path_join(root, "nested"); assert(nested);
    for (size_t i = 0; i < 80; i++) {
        char name[32]; snprintf(name, sizeof name, "child-%06zu", i);
        ok(core_create(nested, name, false));
    }
    search = core_search(root, "", search_default_limits(), NULL, NULL);
    ok(search.result);
    assert(search.visited == ENTRIES + 81 && search.matches.len == ENTRIES + 81);
    for (size_t i = 0; i < search.matches.len; i++)
        assert(!access(search.matches.entries[i].path, F_OK));
    search_result_free(&search);
    ok(core_delete(nested)); free(nested);

    char *path = core_path_join(root, "text"); assert(path);
    FILE *file = fopen(path, "w"); assert(file);
    for (size_t i = 0; i < ROWS; i++) fprintf(file, "row-%06zu\n", i);
    assert(!fclose(file));
    PreviewSession *session = NULL; ok(preview_session_open(path, &session));
    copies = 0; began = core_monotonic_ms();
    PreviewText page;
    ok(preview_session_page(session, ROWS - 40, 20, &page));
    assert(page.len == 20 && page.more && !strcmp(page.lines[0], "row-019960"));
    assert(copies <= 512 + 20);
    printf("preview jump: %zu string copies, %llu ms\n", copies,
           (unsigned long long)(core_monotonic_ms() - began));
    preview_text_free(&page);
    copies = 0;
    ok(preview_session_page(session, ROWS - 60, 20, &page));
    assert(page.len == 20 && !strcmp(page.lines[0], "row-019940") && copies == 20);
    preview_text_free(&page);
    /* Jumping beyond EOF must not leave missing strings in the cache range. */
    ok(preview_session_page(session, ROWS + 1000, 20, &page));
    assert(!page.len && !page.more && page.skipped == ROWS);
    preview_text_free(&page);
    ok(preview_session_page(session, ROWS - 10, 20, &page));
    assert(page.len == 10 && !page.more && !strcmp(page.lines[0], "row-019990"));
    preview_text_free(&page);
    /* Retained rows after a restart must also support backward navigation. */
    ok(preview_session_page(session, ROWS - 60, 20, &page));
    assert(page.len == 20 && !strcmp(page.lines[0], "row-019940"));
    preview_text_free(&page);
    ok(preview_session_page(session, 0, 20, &page));
    assert(page.len == 20 && !strcmp(page.lines[0], "row-000000"));
    preview_text_free(&page); preview_session_close(session);
    assert(!unlink(path)); free(path);
    for (size_t i = 0; i < ENTRIES; i++) {
        char name[32]; snprintf(name, sizeof name, "entry-%06zu", i);
        path = core_path_join(root, name); assert(path);
        assert(!unlink(path)); free(path);
    }
    assert(!rmdir(root));
    puts("PASS: large search and preview jump/cache/restart");
}
