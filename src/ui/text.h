#ifndef TFILE_UI_TEXT_H
#define TFILE_UI_TEXT_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
/* Display only. Invalid UTF-8 bytes use internal non-Unicode values so editing
   a prefilled name can round-trip its bytes without interpreting escapes. */
typedef struct { wchar_t text[16]; int length, cells; } UiTextToken;
typedef struct { size_t end; int cells; bool more; } UiTextSpan;
size_t ui_text_decode(const char *text, wchar_t *value);
UiTextToken ui_text_token(wchar_t value);
UiTextSpan ui_text_span(const char *text, size_t start, int cells);
size_t ui_text_tail(const char *text, int cells);
bool ui_text_encode(const wchar_t *text, size_t length, char *out, size_t capacity);
#endif
