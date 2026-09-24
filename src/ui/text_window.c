#include "ui.h"

static int text_width(WINDOW *win, int y, int x, int width) {
    int h, w; getmaxyx(win, h, w);
    if (y < 0 || y >= h || x < 0 || x >= w || width <= 0) return 0;
    int available = w - x - (y == h - 1 ? 1 : 0);
    return width < available ? width : available;
}
static void write_span(WINDOW *win, int y, int x, const char *text, size_t start, size_t end) {
    for (size_t at = start; at < end;) {
        wchar_t value; at += ui_text_decode(text + at, &value);
        UiTextToken token = ui_text_token(value);
        mvwaddnwstr(win, y, x, token.text, token.length); x += token.cells;
    }
}
size_t draw_window_page(WINDOW *win, int y, int x, int width, const char *text, size_t start) {
    width = text_width(win, y, x, width);
    if (!width) return start;
    UiTextSpan span = ui_text_span(text, start, width);
    write_span(win, y, x, text, start, span.end); return span.end;
}
void draw_window_text(WINDOW *win, int y, int x, int width, const char *text) {
    width = text_width(win, y, x, width);
    if (!width) return;
    UiTextSpan span = ui_text_span(text, 0, width);
    bool ellipsis = span.more && width >= 3;
    if (ellipsis) span = ui_text_span(text, 0, width - 3);
    write_span(win, y, x, text, 0, span.end);
    if (ellipsis) mvwaddstr(win, y, x + span.cells, "...");
}
void draw_text(int y, int x, int width, const char *text) {
    draw_window_text(stdscr, y, x, width, text);
}
