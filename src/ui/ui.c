#include "ui.h"

static void draw_box(int x, int y, int w, int h, const char *title) {
    if (w < 2 || h < 2) return;
    attron(COLOR_PAIR(UI_BORDER));
    mvhline(y, x, ACS_HLINE, w); mvhline(y + h - 1, x, ACS_HLINE, w);
    mvvline(y, x, ACS_VLINE, h); mvvline(y, x + w - 1, ACS_VLINE, h);
    mvaddch(y, x, ACS_ULCORNER); mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER); mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    if (w > 5) { mvaddch(y, x + 2, ' '); draw_text(y, x + 3, w - 5, title); }
    attroff(COLOR_PAIR(UI_BORDER));
}
static void fit_selection(UiContext *ui, int rows) {
    if (ui->app.files.len && ui->selected >= ui->app.files.len) ui->selected = ui->app.files.len - 1;
    if (!ui->app.files.len) ui->selected = 0;
    if (ui->selected < ui->top) ui->top = ui->selected;
    if (rows > 0 && ui->selected >= ui->top + (size_t)rows) ui->top = ui->selected - (size_t)rows + 1;
}
typedef struct { const char *label; const char *compact; int key; } Action;
static const Action actions[] = {
    { "F1 Help", "F1", KEY_F(1) }, { "F2 New", "F2", KEY_F(2) },
    { "F3 Search", "F3", KEY_F(3) }, { "F5 Copy", "F5", KEY_F(5) },
    { "F6 Move", "F6", KEY_F(6) }, { "F7 Options", "F7", KEY_F(7) },
    { "F8 Delete", "F8", KEY_F(8) }, { "F9 Menu", "F9", KEY_F(9) },
    { "F10 Quit", "F10", KEY_F(10) }
};
static const char *action_label(size_t index, int width) {
    int required = 1;
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i)
        required += (int)strlen(actions[i].label) + 3;
    return width >= required ? actions[index].label : actions[index].compact;
}
int header_action(int x, int width) {
    int at = 1;
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        int length = (int)strlen(action_label(i, width)) + 2;
        if (at + length >= width) break;
        if (x >= at && x < at + length) return actions[i].key;
        at += length + 1;
    }
    return 0;
}

void draw(UiContext *ui) {
    erase(); int h, w; getmaxyx(stdscr, h, w);
    if (h < 9 || w < 50) { mvaddstr(0, 0, "Terminal too small (minimum 50x9)"); refresh(); return; }
    attron(COLOR_PAIR(UI_HEADER) | A_BOLD); mvhline(0, 0, ' ', w);
    int at = 1;
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        const char *label = action_label(i, w);
        int length = (int)strlen(label) + 2;
        if (at + length >= w) break;
        mvaddch(0, at, '['); draw_text(0, at + 1, length - 2, label);
        mvaddch(0, at + length - 1, ']');
        at += length + 1;
    }
    attroff(COLOR_PAIR(UI_HEADER) | A_BOLD);
    attron(COLOR_PAIR(UI_PATH) | A_BOLD);
    mvhline(1, 0, ' ', w);
    if (!ui->app.history_at) attron(A_DIM);
    draw_text(1, 1, 3, "[<]"); attroff(A_DIM);
    if (ui->app.history_at + 1 >= ui->app.history_len) attron(A_DIM);
    draw_text(1, 5, 3, "[>]"); attroff(A_DIM);
    draw_text(1, 8, 10, "Location:");
    int available = w - 19;
    if (ui_text_span(ui->app.directory, 0, available).more) {
        mvaddstr(1, 18, "... ");
        size_t start = ui_text_tail(ui->app.directory, available - 4);
        draw_window_page(stdscr, 1, 22, available - 4, ui->app.directory, start);
    } else draw_text(1, 18, available, ui->app.directory);
    attroff(COLOR_PAIR(UI_PATH) | A_BOLD);
    int mid = ui->app.show_preview ? w / 2 : w, panel_h = h - 4, rows = panel_h - 3;
    char title[320];
    snprintf(title, sizeof title, " Files (%zu) ", ui->app.files.len);
    draw_box(0, 2, mid, panel_h, title); if (ui->app.show_preview) draw_box(mid, 2, w - mid, panel_h, " Preview ");
    attron(COLOR_PAIR(UI_HEADER)); mvhline(3, 1, ' ', mid - 2);
    draw_text(3, 2, mid - 4, "[Parent]  [Open]");
    if (mid >= 44) { char hint[40]; snprintf(hint, sizeof hint, "Wheel: %d row%s", ui->app.wheel_step, ui->app.wheel_step == 1 ? "" : "s"); draw_text(3, 21, mid - 23, hint); }
    attroff(COLOR_PAIR(UI_HEADER));
    fit_selection(ui, rows);
    for (int r = 0; r < rows && ui->top + (size_t)r < ui->app.files.len; ++r) {
        Item *it = &ui->app.files.entries[ui->top + (size_t)r];
        int y = 4 + r;
        const char *kind = it->valid && (it->kind == FILE_DIRECTORY) ? "Directory" :
                           it->valid && (it->kind == FILE_LINK) ? "Link" :
                           it->valid && (it->kind == FILE_REGULAR) ? "File" : "Other";
        bool active = ui->top + (size_t)r == ui->selected;
        if (active) { attron(COLOR_PAIR(UI_SELECTED)); mvhline(y, 1, ' ', mid - 2); attroff(COLOR_PAIR(UI_SELECTED)); }
        int pair = item_color(it, active);
        attron(COLOR_PAIR(pair) | (active ? A_BOLD : A_NORMAL));
        draw_text(y, 2, 9, kind); draw_text(y, 13, mid - 15, it->name);
        attroff(COLOR_PAIR(pair) | (active ? A_BOLD : A_NORMAL));
    }
    if (ui->app.show_preview) preview(ui, mid, 2, w - mid, panel_h);
    if (!ui->app.files.len) draw_text(4, 2, mid - 4, "Empty directory - F2 New");
    attron(COLOR_PAIR(UI_STATUS)); mvhline(h - 2, 0, ' ', w); draw_text(h - 2, 1, w - 2, ui->status); attroff(COLOR_PAIR(UI_STATUS));
    attron(COLOR_PAIR(UI_HEADER)); mvhline(h - 1, 0, ' ', w);
    attroff(COLOR_PAIR(UI_HEADER));
    attron(COLOR_PAIR(UI_MUTED));
    draw_text(h - 1, 1, w - 2, w < 98 ? "F1: Help  F9: Menu with action names" :
              "Click: select  Double-click: open  Wheel: scroll  Backspace: parent directory");
    attroff(COLOR_PAIR(UI_MUTED));
    refresh();
}
