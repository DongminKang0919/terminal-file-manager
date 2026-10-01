#include "ui.h"

UiLayout ui_layout(int width, int height, bool preview) {
    int list = preview ? (width < 80 ? width * 3 / 5 : width * 11 / 20) : width;
    bool columns = height >= 12;
    return (UiLayout){list, height - 4, columns ? 5 : 4, height - (columns ? 8 : 7), columns};
}
const char *ui_kind(const Item *it) {
    if (!it->valid) return "?";
    return it->kind == FILE_DIRECTORY ? "Dir" : it->kind == FILE_LINK ? "Link" :
           it->kind == FILE_REGULAR ? "File" : "Other";
}
void ui_size(const Item *it, char *out, size_t size) {
    if (!it->valid || it->kind == FILE_DIRECTORY) { snprintf(out, size, "-"); return; }
    double value = (double)it->size;
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    int unit = 0;
    while (value >= 1024 && unit < 6) { value /= 1024; unit++; }
    snprintf(out, size, unit ? "%.1f %s" : "%.0f %s", value, units[unit]);
}

static void draw_box(int x, int y, int w, int h, const char *title) {
    if (w < 2 || h < 2) return;
    attron(COLOR_PAIR(UI_BORDER));
    mvhline(y, x, ACS_HLINE, w); mvhline(y + h - 1, x, ACS_HLINE, w);
    mvvline(y, x, ACS_VLINE, h); mvvline(y, x + w - 1, ACS_VLINE, h);
    mvaddch(y, x, ACS_ULCORNER); mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER); mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    if (w > 5) {
        attrset(COLOR_PAIR(UI_MUTED));
        mvaddch(y, x + 2, ' '); draw_text(y, x + 3, w - 5, title);
        attrset(COLOR_PAIR(UI_BASE) | A_BOLD);
        draw_text(y, x + 4, w - 6, x ? "Preview" : "Files");
    }
    attrset(A_NORMAL);
}
void fit_selection(UiContext *ui, int rows) {
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
static bool action_bounds(size_t index, int width, int *at, int *length) {
    *at = 1;
    for (size_t i = 0; i <= index; i++) {
        *length = (int)strlen(action_label(i, width)) + 2;
        if (*at + *length >= width) return false;
        if (i != index) {
            int gap = width >= 120 && (i == 0 || i == 2 || i == 4 || i == 6) ? 3 : 1;
            *at += *length + gap;
        }
    }
    return true;
}
int header_action(int x, int width) {
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        int at, length;
        if (!action_bounds(i, width, &at, &length)) break;
        if (x >= at && x < at + length) return actions[i].key;
    }
    return 0;
}

static void draw_screen(UiContext *ui, bool prepare) {
    erase(); int h, w; getmaxyx(stdscr, h, w);
    if (prepare) preview_prepare(ui, h >= 9 && w >= 50 ? h - 7 : 0);
    if (h < 9 || w < 50) { mvaddstr(0, 0, "Terminal too small (minimum 50x9)"); refresh(); return; }
    attrset(ui_bar()); mvhline(0, 0, ' ', w);
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        const char *label = action_label(i, w);
        int at, length;
        if (!action_bounds(i, w, &at, &length)) break;
        mvaddch(0, at, '['); draw_text(0, at + 1, length - 2, label);
        mvaddch(0, at + length - 1, ']');
        attron(A_BOLD);
        draw_text(0, at + 1, (int)strlen(actions[i].compact), actions[i].compact);
        attroff(A_BOLD);
    }
    attrset(COLOR_PAIR(UI_PATH));
    mvhline(1, 0, ' ', w);
    if (!ui->app.history_at) attrset(COLOR_PAIR(UI_DISABLED) | A_DIM);
    draw_text(1, 1, 3, "[<]"); attrset(COLOR_PAIR(UI_PATH));
    if (ui->app.history_at + 1 >= ui->app.history_len) attrset(COLOR_PAIR(UI_DISABLED) | A_DIM);
    draw_text(1, 5, 3, "[>]"); attrset(COLOR_PAIR(UI_MUTED));
    draw_text(1, 10, 10, "Location:");
    attrset(COLOR_PAIR(UI_PATH));
    int available = w - 21;
    if (ui_text_span(ui->app.directory, 0, available).more) {
        mvaddstr(1, 20, "... ");
        size_t start = ui_text_tail(ui->app.directory, available - 4);
        draw_window_page(stdscr, 1, 24, available - 4, ui->app.directory, start);
    } else draw_text(1, 20, available, ui->app.directory);
    attroff(COLOR_PAIR(UI_PATH));
    UiLayout layout = ui_layout(w, h, ui->app.show_preview);
    int mid = layout.list_width, panel_h = layout.panel_height, rows = layout.list_rows;
    char title[320];
    snprintf(title, sizeof title, " Files (%zu) | %s %s ", ui->app.files.len,
             sort_label(ui->app.sort.key), ui->app.sort.descending ? "descending" : "ascending");
    if ((int)strlen(title) > mid - 5)
        snprintf(title, sizeof title, " Files (%zu) %s%c ", ui->app.files.len,
                 ui->app.sort.key == SORT_MODIFIED ? "Time" : sort_label(ui->app.sort.key),
                 ui->app.sort.descending ? '-' : '+');
    draw_box(0, 2, mid, panel_h, title);
    if (ui->app.show_preview) draw_box(mid, 2, w - mid, panel_h, " Preview ");
    attron(COLOR_PAIR(UI_MUTED));
    draw_text(3, 2, mid - 4, "[Parent]  [Open]");
    int date_x = mid >= 72 ? mid - 18 : 0;
    int size_x = mid >= 48 ? (date_x ? date_x - 11 : mid - 12) : 0;
    int kind_x = size_x ? size_x - 6 : mid - 7;
    int name_width = kind_x - 4;
    if (layout.columns) {
        attrset(COLOR_PAIR(UI_COLUMNS) | (COLORS < 256 ? A_UNDERLINE : A_NORMAL));
        mvhline(4, 1, ' ', mid - 2);
        draw_text(4, 3, name_width, "Name");
        draw_text(4, kind_x, 5, "Kind");
        if (size_x) draw_text(4, size_x + 5, 4, "Size");
        if (date_x) draw_text(4, date_x, 16, "Modified");
    }
    attrset(A_NORMAL);
    fit_selection(ui, rows);
    for (int r = 0; r < rows && ui->top + (size_t)r < ui->app.files.len; ++r) {
        Item *it = &ui->app.files.entries[ui->top + (size_t)r];
        int y = layout.list_y + r;
        bool active = ui->top + (size_t)r == ui->selected;
        attr_t style = active ? ui_selection() : COLOR_PAIR(UI_BASE);
        attrset(style);
        mvhline(y, 1, ' ', mid - 2);
        mvaddch(y, 1, active ? '>' : ' ');
        if (!active) attrset(COLOR_PAIR(item_color(it, false)));
        draw_text(y, 3, name_width, it->name);
        attrset(active ? style : COLOR_PAIR(UI_MUTED));
        draw_text(y, kind_x, 5, ui_kind(it));
        if (size_x) {
            char size[32], aligned[40]; ui_size(it, size, sizeof size);
            snprintf(aligned, sizeof aligned, "%9s", size);
            draw_text(y, size_x, 9, aligned);
        }
        if (date_x) {
            time_t modified = (time_t)it->modified; struct tm *tm = localtime(&modified);
            char date[32] = "?";
            if (it->valid && tm) strftime(date, sizeof date, "%Y-%m-%d %H:%M", tm);
            draw_text(y, date_x, 16, date);
        }
        attrset(A_NORMAL);
    }
    if (ui->app.files.len && (ui->top || ui->app.files.len > (size_t)rows)) {
        size_t end = ui->top + (size_t)rows;
        if (end > ui->app.files.len) end = ui->app.files.len;
        char range[96];
        snprintf(range, sizeof range, " Shown %zu-%zu/%zu ", ui->top + 1, end, ui->app.files.len);
        if ((int)strlen(range) > mid - 4)
            snprintf(range, sizeof range, " %s %s ", ui->top ? "^ more" : "Top", end < ui->app.files.len ? "v more" : "End");
        attrset(COLOR_PAIR(UI_MUTED));
        draw_text(h - 3, 2, mid - 4, range);
        attrset(A_NORMAL);
    }
    if (ui->app.show_preview) preview(ui, mid, 2, w - mid, panel_h);
    if (!ui->app.files.len) draw_text(layout.list_y, 2, mid - 4, "Empty directory - F2 New");
    char summary[96];
    snprintf(summary, sizeof summary, "Shown: %zu | Hidden: %s", ui->app.files.len, ui->app.show_hidden ? "on" : "off");
    attron(COLOR_PAIR(UI_STATUS)); mvhline(h - 2, 0, ' ', w);
    draw_text(h - 2, 1, w - 2, ui->status[0] ? ui->status : summary);
    attroff(COLOR_PAIR(UI_STATUS));
    attrset(ui_bar()); mvhline(h - 1, 0, ' ', w);
    if (!ui->modal_depth) {
        const char *keys[] = {"Enter", "Backspace", "F1", "F9"};
        const char *labels[] = {": Open", ": Parent", ": Help", ": Menu"};
        int x = 1;
        for (size_t i = 0; i < 4; i++) {
            int keylen = (int)strlen(keys[i]), len = keylen + (int)strlen(labels[i]);
            if (x + len > w - 1) break;
            attron(A_BOLD); draw_text(h - 1, x, keylen, keys[i]); attroff(A_BOLD);
            draw_text(h - 1, x + keylen, len - keylen, labels[i]);
            x += len + 2;
        }
    }
    attrset(A_NORMAL);
    if (ui->modal_depth) {
        /* Change cell attributes only; keep wide glyphs and ACS border characters. */
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            cchar_t cell; wchar_t text[CCHARW_MAX]; attr_t attr; short pair;
            mvwin_wch(stdscr, y, x, &cell); getcchar(&cell, text, &attr, &pair, NULL);
            mvchgat(y, x, 1, (attr & A_ALTCHARSET) | A_DIM, has_colors() ? UI_INACTIVE : 0, NULL);
        }
    }
    refresh();
}

/* Modal focus changes consume prepared data only: no metadata poll or reader I/O. */
void draw_cached(UiContext *ui) {
    if (stdscr && ui->app.directory) draw_screen(ui, false);
}
void draw(UiContext *ui) { draw_screen(ui, true); }
