#include "ui.h"

/* Shared form editing: original invalid bytes/control characters remain tokens,
   and only drawing uses escaped text. No shell expansion or normalization. */
bool field_init(UiField *f, const char *initial) {
    *f = (UiField){0};
    size_t at = 0;
    while (initial[at] && f->len + 1 < UI_INPUT_CAP)
        at += ui_text_decode(initial + at, &f->value[f->len++]);
    f->cursor = f->len;
    return !initial[at];
}
bool field_edit(UiField *f, int kind, wint_t key) {
    if (kind == KEY_CODE_YES && key == KEY_LEFT) { if (f->cursor) f->cursor--; }
    else if (kind == KEY_CODE_YES && key == KEY_RIGHT) { if (f->cursor < f->len) f->cursor++; }
    else if (kind == KEY_CODE_YES && key == KEY_HOME) f->cursor = 0;
    else if (kind == KEY_CODE_YES && key == KEY_END) f->cursor = f->len;
    else if ((kind == KEY_CODE_YES && key == KEY_BACKSPACE) || (kind == OK && (key == 127 || key == 8))) {
        if (f->cursor) { memmove(f->value + f->cursor - 1, f->value + f->cursor, (f->len - f->cursor + 1) * sizeof *f->value); f->cursor--; f->len--; }
    } else if (kind == KEY_CODE_YES && key == KEY_DC) {
        if (f->cursor < f->len) { memmove(f->value + f->cursor, f->value + f->cursor + 1, (f->len - f->cursor) * sizeof *f->value); f->len--; }
    } else if (kind == OK && key >= 32 && iswprint(key) && f->len + 1 < UI_INPUT_CAP) {
        memmove(f->value + f->cursor + 1, f->value + f->cursor, (f->len - f->cursor + 1) * sizeof *f->value);
        f->value[f->cursor++] = key; f->len++;
    } else return false;
    return true;
}
int field_draw(WINDOW *win, int y, int x, int width, UiField *f, bool focused) {
    if (f->cursor < f->start) f->start = f->cursor;
    int cols = 0;
    for (size_t i = f->start; i < f->cursor; i++) cols += ui_text_token(f->value[i]).cells;
    while (cols >= width - 1 && f->start < f->cursor) cols -= ui_text_token(f->value[f->start++]).cells;
    attr_t style = focused ? ui_selection() : COLOR_PAIR(UI_BASE);
    wattron(win, style); mvwhline(win, y, x, ' ', width);
    int used = 0;
    for (size_t i = f->start; i < f->len; i++) {
        UiTextToken token = ui_text_token(f->value[i]);
        if (used + token.cells > width - 1) break;
        mvwaddnwstr(win, y, x + used, token.text, token.length); used += token.cells;
    }
    wattroff(win, style);
    return cols;
}
void field_click(UiField *f, int column) {
    f->cursor = f->start; int col = 0;
    while (f->cursor < f->len) {
        int n = ui_text_token(f->value[f->cursor]).cells;
        if (col + n > column) break;
        col += n; f->cursor++;
    }
}
