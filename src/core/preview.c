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
