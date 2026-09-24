#include "ui.h"

/* Raw presses make selection immediate; track double presses without curses' delay. */
bool mouse_click(const MEVENT *e) {
    return (e->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED)) != 0;
}
void dialog_frame(WINDOW *win, const char *title) {
    int h, w; getmaxyx(win, h, w); (void)h;
    werase(win);
    wattron(win, COLOR_PAIR(UI_BORDER)); box(win, 0, 0); wattroff(win, COLOR_PAIR(UI_BORDER));
    wattron(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
    mvwhline(win, 0, 1, ' ', w - 2);
    mvwaddnstr(win, 0, 2, title, w - 9);
    mvwaddstr(win, 0, w - 6, "[ x ]");
    wattroff(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
}
WINDOW *dialog_open(UiContext *ui, const char *title, int height, int width) {
    int h, w; getmaxyx(stdscr, h, w);
    if (h < 9 || w < 50) { message(ui, "Resize terminal to at least 50x9"); return NULL; }
    if (height > h - 2) height = h - 2;
    if (width > w - 4) width = w - 4;
    WINDOW *win = newwin(height, width, (h - height) / 2, (w - width) / 2);
    if (win) { keypad(win, TRUE); wbkgd(win, COLOR_PAIR(UI_BASE)); dialog_frame(win, title); }
    return win;
}
bool dialog_closed(WINDOW *win, const MEVENT *e) {
    int y, x, h, w; getbegyx(win, y, x); getmaxyx(win, h, w); (void)h;
    return mouse_click(e) && e->y == y && e->x >= x + w - 6 && e->x < x + w - 1;
}
int mouse_key(WINDOW *win, int key) {
    if (key != KEY_MOUSE) return key;
    MEVENT e;
    if (getmouse(&e) != OK) return 0;
    if (dialog_closed(win, &e)) return 27;
    if (!wenclose(win, e.y, e.x)) return 0;
    if (e.bstate & BUTTON4_PRESSED) return KEY_UP;
    if (e.bstate & BUTTON5_PRESSED) return KEY_DOWN;
    return 0;
}
void dialog_close(UiContext *ui, WINDOW *win) {
    (void)ui; curs_set(0); delwin(win); touchwin(stdscr); draw(ui); }

/* Wide-character editing keeps cursor movement and deletion on UTF-8 boundaries. */
static bool input_dialog(UiContext *ui, const char *label, char *out, size_t size, bool *directory, const char *initial, WINDOW *host) {
    WINDOW *win = host ? host : dialog_open(ui, label, 9, 76);
    if (!win) return false;
    wchar_t value[UI_INPUT_CAP] = L"";
    size_t len = 0, cursor = 0, start = 0;
    if (initial) {
        size_t n = mbstowcs(value, initial, UI_INPUT_CAP - 1);
        if (n != (size_t)-1) { value[n] = 0; len = cursor = n; }
    }
    int focus = 0;
    bool accepted = false;
    char warning[256] = "";
    for (;;) {
        int h, w; getmaxyx(win, h, w);
        dialog_frame(win, label);
        if (directory) {
            mvwaddstr(win, 1, 2, "Type:");
            if (focus == 3) wattron(win, A_REVERSE);
            mvwprintw(win, 1, 8, "[%c] File   [%c] Directory", *directory ? ' ' : 'x', *directory ? 'x' : ' ');
            wattroff(win, A_REVERSE);
            mvwaddstr(win, 2, 2, "Name:");
            wattron(win, COLOR_PAIR(UI_SPECIAL) | A_BOLD);
            mvwaddnstr(win, 4, 2, warning, w - 4);
            wattroff(win, COLOR_PAIR(UI_SPECIAL) | A_BOLD);
            if (h > 7) mvwaddnstr(win, h - 3, 2, "Tab: next field   Type: Left/Right   Esc: cancel", w - 4);
        } else {
            mvwaddnstr(win, 1, 2, ui->app.directory, w - 4);
            if (host) {
                mvwaddstr(win, 2, 2, "Name contains:");
                mvwaddnstr(win, 4, 2, warning, w - 4);
                if (h > 8) mvwaddnstr(win, 5, 2, "Search below this directory. Enter a name, then Search.", w - 4);
            }
        }
        if (cursor < start) start = cursor;
        int cols = 0;
        for (size_t i = start; i < cursor; i++) { int n = wcwidth(value[i]); cols += n > 0 ? n : 1; }
        while (cols >= w - 5 && start < cursor) { int n = wcwidth(value[start++]); cols -= n > 0 ? n : 1; }
        wattron(win, COLOR_PAIR(UI_SELECTED)); mvwhline(win, 3, 2, ' ', w - 4);
        int used = 0;
        for (size_t i = start; i < len; i++) {
            int n = wcwidth(value[i]); if (n < 1) n = 1;
            if (used + n > w - 5) break;
            mvwaddnwstr(win, 3, 2 + used, &value[i], 1); used += n;
        }
        wattroff(win, COLOR_PAIR(UI_SELECTED));
        wattron(win, focus == 1 ? A_REVERSE : A_NORMAL); mvwaddstr(win, h - 2, 2, directory ? "[ Create ]" : host ? "[ Search ]" : "[ OK ]"); wattroff(win, A_REVERSE);
        wattron(win, focus == 2 ? A_REVERSE : A_NORMAL); mvwaddstr(win, h - 2, 12, "[ Cancel ]"); wattroff(win, A_REVERSE);
        curs_set(focus == 0); wmove(win, 3, 2 + cols); wrefresh(win);
        wint_t key; int kind = input_wide(win, &key);
        if (kind == ERR) continue;
        if ((kind == KEY_CODE_YES && key == KEY_RESIZE) || (kind == OK && key == 27)) break;
        if (directory && focus == 3 && ((kind == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) || (kind == OK && key == ' '))) {
            *directory = !*directory; continue;
        }
        if (kind == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (!mouse_click(&e)) continue;
            if (directory && e.y == y + 1) {
                if (e.x >= x + 8 && e.x < x + 16) { *directory = false; focus = 0; }
                if (e.x >= x + 19 && e.x < x + 32) { *directory = true; focus = 0; }
                continue;
            }
            if (e.y == y + h - 2 && e.x >= x + 12 && e.x < x + 22) break;
            if (e.y == y + h - 2 && e.x >= x + 2 && e.x < x + ((directory || host) ? 12 : 8)) { key = '\n'; kind = OK; focus = 1; }
            else if (e.y == y + 3 && e.x >= x + 2 && e.x < x + w - 2) {
                focus = 0; cursor = start; int col = 0;
                while (cursor < len) { int n = wcwidth(value[cursor]); if (n < 1) n = 1; if (col + n > e.x - x - 2) break; col += n; cursor++; }
                continue;
            } else continue;
        }
        if (kind == OK && key == '\t') { focus = (focus + 1) % (directory ? 4 : 3); continue; }
        if (kind == KEY_CODE_YES && key == KEY_BTAB) { focus = (focus + (directory ? 3 : 2)) % (directory ? 4 : 3); continue; }
        if ((kind == OK && (key == '\n' || key == '\r')) || (kind == KEY_CODE_YES && key == KEY_ENTER)) {
            if (focus == 2) break;
            if (focus == 3) { focus = 0; continue; }
            if (!len) { snprintf(warning, sizeof warning, "Enter a name to continue."); focus = 0; continue; }
            size_t n = wcstombs(out, value, size);
            if (n != (size_t)-1 && n < size) {
                if (!directory || create_named_entry(ui, *directory, out, warning, sizeof warning)) { accepted = true; break; }
                focus = 0;
            } else snprintf(warning, sizeof warning, "Name is too long.");
            continue;
        }
        if (focus != 0) continue;
        if (kind == KEY_CODE_YES && key == KEY_LEFT) { if (cursor) cursor--; }
        else if (kind == KEY_CODE_YES && key == KEY_RIGHT) { if (cursor < len) cursor++; }
        else if (kind == KEY_CODE_YES && key == KEY_HOME) cursor = 0;
        else if (kind == KEY_CODE_YES && key == KEY_END) cursor = len;
        else if ((kind == KEY_CODE_YES && key == KEY_BACKSPACE) || (kind == OK && (key == 127 || key == 8))) {
            if (cursor) { memmove(value + cursor - 1, value + cursor, (len - cursor + 1) * sizeof *value); cursor--; len--; }
        } else if (kind == KEY_CODE_YES && key == KEY_DC) {
            if (cursor < len) { memmove(value + cursor, value + cursor + 1, (len - cursor) * sizeof *value); len--; }
        } else if (kind == OK && key >= 32 && iswprint(key) && len + 1 < UI_INPUT_CAP) {
            memmove(value + cursor + 1, value + cursor, (len - cursor + 1) * sizeof *value); value[cursor++] = key; len++;
        }
    }
    if (host) curs_set(0); else dialog_close(ui, win);
    return accepted;
}
bool prompt(UiContext *ui, const char *label, char *out, size_t size) {
    return input_dialog(ui, label, out, size, NULL, NULL, NULL);
}
bool new_entry_dialog(UiContext *ui, bool directory, char *name, size_t size) {
    return input_dialog(ui, "New - File or Directory", name, size, &directory, NULL, NULL);
}
bool prompt_value(UiContext *ui, const char *label, char *out, size_t size, const char *initial) {
    return input_dialog(ui, label, out, size, NULL, initial, NULL);
}
bool search_prompt(UiContext *ui, WINDOW *win, char *out, size_t size) {
    return input_dialog(ui, "Search", out, size, NULL, out, win);
}
bool confirm(UiContext *ui, const char *name, bool directory) {
    WINDOW *win = dialog_open(ui, "Confirm deletion", 8, 78);
    if (!win) return false;
    bool yes = false, result = false;
    for (;;) {
        int h, w; getmaxyx(win, h, w);
        dialog_frame(win, "Confirm deletion");
        mvwaddnstr(win, 1, 2, directory ? "Delete directory and all its contents?" : "Delete this file?", w - 4);
        wattron(win, A_BOLD); mvwaddnstr(win, 2, 2, name, w - 4); wattroff(win, A_BOLD);
        mvwaddnstr(win, 3, 2, "This cannot be undone.", w - 4);
        wattron(win, yes ? A_REVERSE : A_NORMAL); mvwaddstr(win, h - 2, 2, "[ Delete ]"); wattroff(win, A_REVERSE);
        wattron(win, !yes ? A_REVERSE : A_NORMAL); mvwaddstr(win, h - 2, 16, "[ Cancel ]"); wattroff(win, A_REVERSE); wrefresh(win);
        int key = input_key(win);
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (mouse_click(&e) && e.y == y + h - 2) {
                if (e.x >= x + 2 && e.x < x + 12) { result = true; break; }
                if (e.x >= x + 16 && e.x < x + 26) break;
            }
        }
        if (key == 27 || key == KEY_RESIZE || key == 'n') break;
        if (key == '\t' || key == KEY_LEFT || key == KEY_RIGHT) yes = !yes;
        if (key == '\n' || key == KEY_ENTER) { result = yes; break; }
    }
    dialog_close(ui, win); return result;
}

static int choice_dialog(UiContext *ui, const char *title, const char **labels, int total) {
    WINDOW *win = dialog_open(ui, title, total + 4, 52);
    if (!win) return -1;
    int selected_row = 0, offset = 0, result = -1;
    for (;;) {
        int h, w; getmaxyx(win, h, w); int rows = h - 3;
        if (selected_row < offset) offset = selected_row;
        if (selected_row >= offset + rows) offset = selected_row - rows + 1;
        dialog_frame(win, title);
        for (int i = 0; i < rows && offset + i < total; i++) {
            if (offset + i == selected_row) wattron(win, A_REVERSE);
            mvwaddnstr(win, i + 1, 2, labels[offset + i], w - 4); wattroff(win, A_REVERSE);
        }
        mvwaddnstr(win, h - 2, 2, "Up/Down  Enter: choose  Esc: close", w - 4); wrefresh(win);
        int key = input_key(win);
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (!wenclose(win, e.y, e.x)) continue;
            if (e.bstate & BUTTON4_PRESSED) key = KEY_UP;
            else if (e.bstate & BUTTON5_PRESSED) key = KEY_DOWN;
            else if (mouse_click(&e) && e.x > x && e.x < x + w - 1 && e.y > y && e.y < y + 1 + rows && offset + e.y - y - 1 < total) { result = offset + e.y - y - 1; break; }
        }
        if (key == 27 || key == KEY_RESIZE) break;
        if (key == KEY_UP && selected_row) selected_row--;
        if (key == KEY_DOWN && selected_row + 1 < total) selected_row++;
        if (key == '\n' || key == KEY_ENTER) { result = selected_row; break; }
    }
    dialog_close(ui, win); return result;
}
int show_menu(UiContext *ui) {
    const char *labels[] = {"F1   Help", "F2   New...", "F3   Search", "F5   Copy", "F6   Move / Rename", "F7   Options", "F8   Delete", "F10  Quit", "Backspace   Parent directory", "r    Refresh"};
    const int keys[] = {KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(5), KEY_F(6), KEY_F(7), KEY_F(8), KEY_F(10), KEY_BACKSPACE, 'r'};
    int i = choice_dialog(ui, "Menu", labels, 10); return i < 0 ? 0 : keys[i];
}
void show_options(UiContext *ui) {
    for (;;) {
        char hidden[64], preview_text[64], wheel[64];
        snprintf(hidden, sizeof hidden, "[%c] Show hidden files", ui->app.show_hidden ? 'x' : ' ');
        snprintf(preview_text, sizeof preview_text, "[%c] Show preview panel", ui->app.show_preview ? 'x' : ' ');
        snprintf(wheel, sizeof wheel, "Wheel scroll: %d rows (click to change)", ui->app.wheel_step);
        const char *labels[] = {hidden, preview_text, wheel, "Done (settings apply to this session)"};
        int i = choice_dialog(ui, "Options", labels, 4);
        if (i < 0 || i == 3) break;
        if (i == 0) {
            ui->app.show_hidden = !ui->app.show_hidden;
            if (load_dir(ui, NULL).code != RESULT_OK) ui->app.show_hidden = !ui->app.show_hidden;
        }
        if (i == 1) ui->app.show_preview = !ui->app.show_preview;
        if (i == 2) ui->app.wheel_step = ui->app.wheel_step == 1 ? 3 : ui->app.wheel_step == 3 ? 5 : 1;
        draw(ui);
    }
}
