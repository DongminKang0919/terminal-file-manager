#include "../src/ui/ui.h"
#include <assert.h>
static wchar_t cell(WINDOW *win, int y, int x) {
    cchar_t ch; wchar_t value[CCHARW_MAX]; attr_t attr; short pair;
    assert(mvwin_wch(win, y, x, &ch) == OK);
    assert(getcchar(&ch, value, &attr, &pair, NULL) == OK); return value[0];
}
int main(void) {
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *out = tmpfile(), *in = tmpfile(); assert(out && in);
    SCREEN *screen = newterm("xterm-256color", out, in); assert(screen);
    WINDOW *win = newwin(7, 46, 0, 0); assert(win);
    const char *names[] = {"name\nFAKE\t\033[99;99H", "literal\\n", "invalid\377\342\202", "\314\201e\314\201", "한글한글한글한글한글한글한글한글한글한글한글한글한글"};
    for (size_t n = 0; n < sizeof names / sizeof *names; ++n) {
        for (int y = 0; y < 7; ++y) for (int x = 0; x < 46; ++x) mvwaddch(win, y, x, '.');
        draw_window_text(win, 2, 2, 20, names[n]);
        for (int y = 0; y < 7; ++y) for (int x = 0; x < 46; ++x)
            if (y != 2 || x < 2 || x >= 22) assert(cell(win, y, x) == L'.');
        size_t next = draw_window_page(win, 3, 2, 42, names[n], 0);
        assert(next > 0 && cell(win, 3, 1) == L'.' && cell(win, 3, 44) == L'.');
    }
    werase(win); draw_window_text(win, 2, 2, 12, "a\nb\t\377");
    const wchar_t *expected = L"a\\nb\\t\\xFF";
    for (size_t i = 0; expected[i]; ++i) assert(cell(win, 2, 2 + (int)i) == expected[i]);
    werase(win); draw_window_text(win, 2, 2, 6, "한글이름");
    assert(cell(win, 2, 2) == L'한' && cell(win, 2, 4) == L'.' && cell(win, 2, 6) == L'.');
    delwin(win); endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: curses cell inspection keeps text within its row/region and preserves guards");
}
