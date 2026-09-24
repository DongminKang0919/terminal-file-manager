#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>
void preview_text_free(PreviewText *text) {
    for (size_t i = 0; i < text->len; i++) free(text->lines[i]);
    free(text->lines); *text = (PreviewText){0};
}
Result core_preview_text(const char *path, size_t start, size_t limit, PreviewText *out) {
    *out = (PreviewText){0}; PlatformReader *reader = NULL;
    Result r = platform_reader_open(path, &reader);
    if (r.code != RESULT_OK) return r;
    unsigned char sample[4096]; size_t sampled = 0;
    r = platform_reader_peek(reader, sample, sizeof sample, &sampled);
    out->binary = r.code == RESULT_OK && memchr(sample, 0, sampled) != NULL;
    if (r.code != RESULT_OK || out->binary) { platform_reader_close(reader); return r; }
    if (limit > SIZE_MAX / sizeof *out->lines) { platform_reader_close(reader); return result_make(RESULT_NO_MEMORY, "Preview page too large"); }
    out->lines = calloc(limit ? limit : 1, sizeof *out->lines);
    if (!out->lines) { platform_reader_close(reader); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
    char buffer[4176]; bool end;
    while (out->skipped < start) {
        r = platform_reader_line(reader, buffer, sizeof buffer, &end);
        if (r.code != RESULT_OK || end) goto done;
        out->skipped++;
    }
    for (;;) {
        r = platform_reader_line(reader, buffer, sizeof buffer, &end);
        if (r.code != RESULT_OK || end) break;
        if (out->len == limit) { out->more = true; break; }
        for (char *p = buffer; *p; p++) if (*p == '\t') *p = ' ';
        buffer[strcspn(buffer, "\r\n")] = 0;
        out->lines[out->len] = text_copy(buffer);
        if (!out->lines[out->len]) { r = result_make(RESULT_NO_MEMORY, "Out of memory"); break; }
        out->len++;
    }
done:
    platform_reader_close(reader);
    if (r.code != RESULT_OK) preview_text_free(out);
    return r;
}

#define CACHE_ROWS 512
struct PreviewSession {
    PlatformReader *reader;
    char *path, *lines[CACHE_ROWS];
    size_t first, next;
    bool eof, binary;
    Result error;
};
static void session_clear(PreviewSession *s) {
    platform_reader_close(s->reader); s->reader = NULL;
    for (size_t i = 0; i < CACHE_ROWS; i++) { free(s->lines[i]); s->lines[i] = NULL; }
    s->first = s->next = 0; s->eof = s->binary = false;
}
static Result session_start(PreviewSession *s) {
    session_clear(s);
    s->error = platform_reader_open(s->path, &s->reader);
    if (s->error.code == RESULT_OK) {
        unsigned char sample[4096]; size_t n = 0;
        s->error = platform_reader_peek(s->reader, sample, sizeof sample, &n);
        s->binary = s->error.code == RESULT_OK && memchr(sample, 0, n) != NULL;
    }
    if (s->error.code != RESULT_OK) session_clear(s);
    return s->error;
}
void preview_session_close(PreviewSession *s) {
    if (s) { session_clear(s); free(s->path); free(s); }
}
Result preview_session_open(const char *path, PreviewSession **out) {
    *out = NULL;
    PreviewSession *s = calloc(1, sizeof *s);
    if (!s) return result_make(RESULT_NO_MEMORY, "Out of memory");
    s->path = text_copy(path);
    if (!s->path) { free(s); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
    Result r = session_start(s);
    if (r.code != RESULT_OK) { preview_session_close(s); return r; }
    *out = s; return r;
}
Result preview_session_check(PreviewSession *s, bool *changed) {
    *changed = false;
    if (s->error.code != RESULT_OK) return s->error;
    Result r = platform_reader_changed(s->reader, s->path, changed);
    if (r.code == RESULT_OK && *changed) r = session_start(s);
    if (r.code != RESULT_OK) { s->error = r; session_clear(s); }
    return r;
}
Result preview_session_page(PreviewSession *s, size_t start, size_t limit, PreviewText *out) {
    *out = (PreviewText){0};
    if (s->error.code != RESULT_OK) return s->error;
    if (limit > PREVIEW_PAGE_MAX || start > SIZE_MAX - limit - 1)
        return result_make(RESULT_INVALID_NAME, "Preview page exceeds bounded capacity");
    if (start < s->first && session_start(s).code != RESULT_OK) return s->error;
    out->binary = s->binary;
    if (s->binary) return result_make(RESULT_OK, NULL);
    size_t target = start + limit + 1;
    while (!s->eof && s->next < target) {
        char buffer[4176]; bool end;
        Result r = platform_reader_line(s->reader, buffer, sizeof buffer, &end);
        if (r.code != RESULT_OK) { s->error = r; goto fail; }
        if (end) { s->eof = true; break; }
        for (char *p = buffer; *p; p++) if (*p == '\t') *p = ' ';
        buffer[strcspn(buffer, "\r\n")] = 0;
        char *line = text_copy(buffer);
        if (!line) { s->error = result_make(RESULT_NO_MEMORY, "Out of memory"); goto fail; }
        size_t slot = s->next % CACHE_ROWS;
        free(s->lines[slot]); s->lines[slot] = line; s->next++;
        if (s->next - s->first > CACHE_ROWS) s->first++;
    }
    out->skipped = start < s->next ? start : s->next;
    out->len = s->next - out->skipped;
    if (out->len > limit) out->len = limit;
    out->more = !s->eof || out->skipped + out->len < s->next;
    out->lines = calloc(out->len ? out->len : 1, sizeof *out->lines);
    if (!out->lines) { s->error = result_make(RESULT_NO_MEMORY, "Out of memory"); goto fail; }
    for (size_t i = 0; i < out->len; i++) {
        out->lines[i] = text_copy(s->lines[(out->skipped + i) % CACHE_ROWS]);
        if (!out->lines[i]) { s->error = result_make(RESULT_NO_MEMORY, "Out of memory"); goto fail; }
    }
    return result_make(RESULT_OK, NULL);
fail:
    /* calloc initializes unfilled page slots, so partial allocation is safe. */
    if (out->lines) preview_text_free(out); else *out = (PreviewText){0};
    session_clear(s); return s->error;
}
