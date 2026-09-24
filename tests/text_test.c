#define _XOPEN_SOURCE 700
#include "../src/ui/text.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
static void displayed(const char *raw, const wchar_t *expected) {
    wchar_t shown[2048], decoded[512]; size_t length = 0, decoded_length = 0;
    for (size_t at = 0; raw[at];) {
        wchar_t value; at += ui_text_decode(raw + at, &value); decoded[decoded_length++] = value;
        UiTextToken token = ui_text_token(value);
        assert(token.cells > 0 && token.length > 0);
        for (int i = 0; i < token.length; ++i) { assert(wcwidth(token.text[i]) > 0); shown[length++] = token.text[i]; }
    }
    shown[length] = 0; assert(!wcscmp(shown, expected));
    char restored[2048]; assert(ui_text_encode(decoded, decoded_length, restored, sizeof restored));
    assert(!strcmp(restored, raw));
}
int main(void) {
    assert(setlocale(LC_ALL, "C.UTF-8"));
    displayed("normal-한글.txt", L"normal-한글.txt");
    displayed("a\nb\tc\rd\033\001\177", L"a\\nb\\tc\\rd\\x1B\\x01\\x7F");
    displayed("a\\nb\\tc\\xFF", L"a\\\\nb\\\\tc\\\\xFF");
    displayed("bad-\377\300\257\342\202", L"bad-\\xFF\\xC0\\xAF\\xE2\\x82");
    displayed("\364\220\200\200", L"\\xF4\\x90\\x80\\x80"); /* non-scalar UTF-8 */
    displayed("e\314\201", L"e\\u{0301}");
    displayed("\314\201start", L"\\u{0301}start");
    displayed("x\342\200\215y\342\200\256z", L"x\\u{200D}y\\u{202E}z");
    displayed("x\302\205y", L"x\\u{0085}y");
    const char *raw = "한글-\n-\377-e\314\201-end";
    for (int width = 1; width < 40; ++width) {
        UiTextSpan span = ui_text_span(raw, 0, width);
        assert(span.cells <= width && span.more == (raw[span.end] != 0));
        size_t tail = ui_text_tail(raw, width);
        UiTextSpan ending = ui_text_span(raw, tail, width);
        assert(!ending.more && ending.cells <= width);
    }
    assert(ui_text_span("한", 0, 1).end == 0);
    assert(ui_text_span("\n", 0, 1).end == 0);
    assert(ui_text_span("\377", 0, 3).end == 0);
    char long_name[512] = "";
    for (int i = 0; i < 70; ++i) strcat(long_name, "한");
    strcat(long_name, "-tail-\t\377");
    size_t at = 0, pages = 0;
    while (long_name[at]) {
        UiTextSpan page = ui_text_span(long_name, at, 42);
        assert(page.end > at && page.cells <= 42); at = page.end; pages++;
    }
    assert(pages > 3 && at == strlen(long_name));
    char tiny[2]; assert(!ui_text_encode(L"한", 1, tiny, sizeof tiny));
    puts("PASS: display escapes, Unicode cell widths, zero-width marks, pagination and exact byte round trips");
}
