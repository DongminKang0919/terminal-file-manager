#define _XOPEN_SOURCE 700
#include "text.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#define RAW_BYTE_BASE 0x110000
size_t ui_text_decode(const char *text, wchar_t *value) {
    mbstate_t state = {0};
    size_t n = mbrtowc(value, text, MB_CUR_MAX, &state);
    if (n == (size_t)-1 || n == (size_t)-2 || (n && (*value > 0x10FFFF || (*value >= 0xD800 && *value <= 0xDFFF)))) {
        *value = RAW_BYTE_BASE + (unsigned char)*text; return 1;
    }
    return n;
}
UiTextToken ui_text_token(wchar_t value) {
    UiTextToken token = {0}; char escape[16];
    if (value >= RAW_BYTE_BASE && value < RAW_BYTE_BASE + 256)
        snprintf(escape, sizeof escape, "\\x%02X", (unsigned)(value - RAW_BYTE_BASE));
    else if (value == L'\\') snprintf(escape, sizeof escape, "\\\\");
    else if (value == L'\n') snprintf(escape, sizeof escape, "\\n");
    else if (value == L'\r') snprintf(escape, sizeof escape, "\\r");
    else if (value == L'\t') snprintf(escape, sizeof escape, "\\t");
    else if (value < 32 || value == 127) snprintf(escape, sizeof escape, "\\x%02X", (unsigned)value);
    else if (!iswprint(value) || wcwidth(value) <= 0)
        snprintf(escape, sizeof escape, "\\u{%04X}", (unsigned)value);
    else {
        token.text[0] = value; token.length = 1; token.cells = wcwidth(value); return token;
    }
    token.length = token.cells = (int)strlen(escape);
    for (int i = 0; i < token.length; ++i) token.text[i] = (unsigned char)escape[i];
    return token;
}
UiTextSpan ui_text_span(const char *text, size_t start, int cells) {
    UiTextSpan span = { .end = start };
    while (text[span.end]) {
        wchar_t value; size_t bytes = ui_text_decode(text + span.end, &value);
        UiTextToken token = ui_text_token(value);
        if (token.cells > cells - span.cells) break;
        span.cells += token.cells; span.end += bytes;
    }
    span.more = text[span.end] != 0; return span;
}
size_t ui_text_tail(const char *text, int cells) {
    size_t total = 0, start = 0;
    for (size_t at = 0; text[at];) {
        wchar_t value; at += ui_text_decode(text + at, &value);
        total += (size_t)ui_text_token(value).cells;
    }
    while (text[start] && (cells < 0 || total > (size_t)cells)) {
        wchar_t value; start += ui_text_decode(text + start, &value);
        total -= (size_t)ui_text_token(value).cells;
    }
    return start;
}
bool ui_text_encode(const wchar_t *text, size_t length, char *out, size_t capacity) {
    size_t at = 0;
    for (size_t i = 0; i < length; ++i) {
        char encoded[MB_LEN_MAX]; size_t n;
        if (text[i] >= RAW_BYTE_BASE && text[i] < RAW_BYTE_BASE + 256) {
            encoded[0] = (char)(text[i] - RAW_BYTE_BASE); n = 1;
        } else {
            mbstate_t state = {0}; n = wcrtomb(encoded, text[i], &state);
            if (n == (size_t)-1) return false;
        }
        if (at >= capacity || n >= capacity - at) return false;
        memcpy(out + at, encoded, n); at += n;
    }
    if (at >= capacity) return false;
    out[at] = 0; return true;
}
