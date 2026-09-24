#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>

typedef struct {
    const char *root, *term;
    SearchLimits limits;
    SearchResult result;
    size_t capacity;
    bool depth_limited;
    uint64_t last_progress;
    SearchProgress callback;
    void *context;
} Search;
SearchLimits search_default_limits(void) { return (SearchLimits){100000, 10000, 64}; }
static bool progress(Search *s, const char *path) {
    s->last_progress = platform_monotonic_ms();
    if (s->callback && !s->callback(&s->result, path, s->context)) s->result.stopped = true;
    return !s->result.stopped;
}
static void search_tree(Search *s, const char *path, unsigned depth) {
    if (s->result.stopped || s->result.limited || s->result.result.code != RESULT_OK) return;
    if (depth > s->limits.max_depth) { s->depth_limited = true; return; }
    PlatformDirectory *dir = NULL;
    Result r = platform_directory_open(path, &dir);
    if (r.code != RESULT_OK) { if (!depth) s->result.result = r; return; }
    for (;;) {
        if (s->result.stopped || s->result.limited || s->result.result.code != RESULT_OK) break;
        if (s->result.visited >= s->limits.max_visited || s->result.matches.len >= s->limits.max_results) { s->result.limited = true; break; }
        FileInfo entry; bool end;
        r = platform_directory_next(dir, &entry, &end);
        if (r.code != RESULT_OK) { s->result.result = r; break; }
        if (end) break;
        if (!entry.valid) { file_info_free(&entry); continue; }
        s->result.visited++;
        char *child_path = text_copy(entry.path);
        bool directory = entry.kind == FILE_DIRECTORY;
        if (!child_path) { file_info_free(&entry); s->result.result = result_make(RESULT_NO_MEMORY, "Out of memory"); break; }
        if (text_contains(entry.name, s->term)) {
            char *relative = platform_path_relative(s->root, entry.path);
            if (!relative) { free(child_path); file_info_free(&entry); s->result.result = result_make(RESULT_NO_MEMORY, "Out of memory"); break; }
            if (s->result.matches.len == s->capacity) {
                size_t next = s->capacity ? s->capacity * 2 : 64;
                FileInfo *entries = realloc(s->result.matches.entries, next * sizeof *entries);
                if (!entries) { free(relative); free(child_path); file_info_free(&entry); s->result.result = result_make(RESULT_NO_MEMORY, "Out of memory"); break; }
                s->result.matches.entries = entries; s->capacity = next;
            }
            free(entry.name); entry.name = relative;
            s->result.matches.entries[s->result.matches.len++] = entry;
        } else file_info_free(&entry);
        if (s->result.visited % 64 == 0 || platform_monotonic_ms() - s->last_progress >= 100) progress(s, child_path);
        if (directory) search_tree(s, child_path, depth + 1);
        free(child_path);
    }
    platform_directory_close(dir);
}
SearchResult core_search(const char *root, const char *term, SearchLimits limits, SearchProgress callback, void *context) {
    Search s = { .root = root, .term = term, .limits = limits, .callback = callback, .context = context };
    if (progress(&s, root)) search_tree(&s, root, 0);
    s.result.limited |= s.depth_limited;
    if (!s.result.stopped) progress(&s, root);
    if (s.result.matches.len > 1) qsort(s.result.matches.entries, s.result.matches.len, sizeof *s.result.matches.entries, file_compare);
    return s.result;
}
void search_result_free(SearchResult *result) { file_list_free(&result->matches); *result = (SearchResult){0}; }
