#include "ui.h"

/* Raw presses make selection immediate; track double presses without curses' delay. */
bool mouse_click(const MEVENT *e) {
    return (e->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED)) != 0;
}
void dialog_frame(WINDOW *win, const char *title) {
    int h, w; getmaxyx(win, h, w); (void)h;
    wbkgdset(win, COLOR_PAIR(UI_BASE));
    wattrset(win, A_NORMAL);
    werase(win);
    wattron(win, COLOR_PAIR(UI_BORDER)); box(win, 0, 0); wattroff(win, COLOR_PAIR(UI_BORDER));
    wattron(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
    mvwhline(win, 0, 1, ' ', w - 2);
    draw_window_text(win, 0, 2, w - 9, title);
    mvwaddstr(win, 0, w - 6, "[ x ]");
    wattroff(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
    /* Footer caption sits in a rule; two rows remain reserved at every size. */
    mvwhline(win, h - 3, 1, ACS_HLINE, w - 2);
}

void dialog_button(WINDOW *win, int y, int x, const char *label, bool focused, bool enabled) {
    attr_t style = !enabled ? A_DIM : focused ? ui_selection() : A_NORMAL;
    wattron(win, style); draw_window_text(win, y, x, getmaxx(win) - x - 2, label);
    wattroff(win, style);
}

/* Resolve existing semantic colors onto the popup surfaces without changing text,
   coordinates, cursor, or input policy. Keep ACS and wide-character cells intact. */
void dialog_refresh(WINDOW *win) {
    int h, w, cy, cx; getmaxyx(win, h, w); getyx(win, cy, cx);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        cchar_t cell; wchar_t text[CCHARW_MAX]; attr_t attr; short old;
        mvwin_wch(win, y, x, &cell); getcchar(&cell, text, &attr, &old, NULL);
        attr &= ~A_COLOR;
        int pair;
        bool selected = old == UI_SELECTED || old == UI_DIR_SELECTED || old == UI_FILE_SELECTED ||
                        old == UI_LINK_SELECTED || old == UI_EXEC_SELECTED ||
                        old == UI_HIDDEN_SELECTED || old == UI_SPECIAL_SELECTED;
        bool footer = y >= h - 3;
        if (x == 0 || x == w - 1 || y == h - 1) {
            pair = UI_POP_BORDER; attr |= A_BOLD;
        } else if (y == 0) {
            pair = UI_POP_TITLE; attr |= A_BOLD;
            if (!has_colors()) attr |= A_REVERSE;
        } else if (selected) { pair = UI_SELECTED; attr |= A_BOLD; }
        else if (attr & A_DIM) pair = UI_POP_DISABLED;
        else if (old == UI_SPECIAL) pair = footer ? UI_POP_FOOT_WARNING : UI_POP_WARNING;
        else if (footer) pair = UI_POP_FOOTER;
        else pair = old == UI_MUTED ? UI_POP_MUTED : UI_POP_BODY;
        mvwchgat(win, y, x, 1, attr, has_colors() ? pair : 0, NULL);
    }
    wmove(win, cy, cx); wrefresh(win);
}
WINDOW *dialog_open(UiContext *ui, const char *title, int height, int width) {
    int h, w; getmaxyx(stdscr, h, w);
    if (h < 9 || w < 50) { message(ui, "Resize terminal to at least 50x9"); return NULL; }
    if (height > h - 2) height = h - 2;
    if (width > w - 4) width = w - 4;
    WINDOW *win = newwin(height, width, (h - height) / 2, (w - width) / 2);
    if (win) { ui->modal_depth++; draw_cached(ui); keypad(win, TRUE); wbkgd(win, COLOR_PAIR(UI_BASE)); dialog_frame(win, title); }
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
    curs_set(0); delwin(win);
    if (ui->modal_depth) ui->modal_depth--;
    touchwin(stdscr); draw_cached(ui);
}

/* Wide-character editing keeps cursor movement and deletion on UTF-8 boundaries. */
static bool input_dialog(UiContext *ui, const char *label, char *out, size_t size, bool *directory, const char *initial, WINDOW *host, const char *rename_source) {
    WINDOW *win = host ? host : dialog_open(ui, label, 9, 76);
    if (!win) return false;
    UiField field;
    if (!field_init(&field, initial ? initial : "")) {
        message(ui, "Input exceeds terminal limit");
        if (!host) dialog_close(ui, win);
        return false;
    }
    int focus = 0;
    bool accepted = false;
    char warning[256] = "";
    for (;;) {
        int h, w; getmaxyx(win, h, w);
        dialog_frame(win, label);
        int cancel_x = (host || directory) ? 14 : 10;
        if (directory) {
            mvwaddstr(win, 1, 2, "Type:");
            if (focus == 3) wattron(win, ui_selection());
            mvwprintw(win, 1, 8, "[%c] File   [%c] Directory", *directory ? ' ' : 'x', *directory ? 'x' : ' ');
            wattroff(win, ui_selection());
            mvwaddstr(win, 2, 2, "Name:");
            if (h > 7) draw_window_text(win, h - 3, 2, w - 4, "Tab: next field   Type: Left/Right   Esc: cancel");
        } else {
            draw_window_text(win, 1, 2, w - 4, ui->app.directory);
            if (host) {
                mvwaddstr(win, 2, 2, "Name contains:");
                if (!*warning) draw_window_text(win, 4, 2, w - 4, focus == 2 ?
                    "Enter: close  Tab: focus  Esc: close" : "Enter: search  Tab: focus  Esc: close");
            }
        }
        if (*warning) {
            wattron(win, COLOR_PAIR(UI_SPECIAL) | A_BOLD);
            draw_window_text(win, 4, 2, w - 4, warning);
            wattroff(win, COLOR_PAIR(UI_SPECIAL) | A_BOLD);
        }
        int cols = field_draw(win, 3, 2, w - 4, &field, focus == 0);
        dialog_button(win, h - 2, 2, directory ? "[ Create ]" : host ? "[ Search ]" : "[ OK ]", focus == 1, true);
        dialog_button(win, h - 2, cancel_x, "[ Cancel ]", focus == 2, true);
        curs_set(focus == 0); wmove(win, 3, 2 + cols); dialog_refresh(win);
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
            if (e.y == y + h - 2 && e.x >= x + cancel_x && e.x < x + cancel_x + 10) break;
            if (e.y == y + h - 2 && e.x >= x + 2 && e.x < x + ((directory || host) ? 12 : 8)) { key = '\n'; kind = OK; focus = 1; }
            else if (e.y == y + 3 && e.x >= x + 2 && e.x < x + w - 2) {
                focus = 0; field_click(&field, e.x - x - 2);
                continue;
            } else continue;
        }
        if (kind == OK && key == '\t') { focus = (focus + 1) % (directory ? 4 : 3); continue; }
        if (kind == KEY_CODE_YES && key == KEY_BTAB) { focus = (focus + (directory ? 3 : 2)) % (directory ? 4 : 3); continue; }
        if ((kind == OK && (key == '\n' || key == '\r')) || (kind == KEY_CODE_YES && key == KEY_ENTER)) {
            if (focus == 2) break;
            if (focus == 3) { focus = 0; continue; }
            if (!field.len) { snprintf(warning, sizeof warning, "Enter a value to continue."); focus = 0; continue; }
            if (ui_text_encode(field.value, field.len, out, size)) {
                if (rename_source ? rename_named_entry(ui, rename_source, out, warning, sizeof warning) :
                    !directory || create_named_entry(ui, *directory, out, warning, sizeof warning)) { accepted = true; break; }
                focus = 0;
            } else { snprintf(warning, sizeof warning, "Input is too long."); focus = 0; }
            continue;
        }
        if (focus != 0) continue;
        field_edit(&field, kind, key);
    }
    if (host) curs_set(0); else dialog_close(ui, win);
    return accepted;
}
bool prompt(UiContext *ui, const char *label, char *out, size_t size) {
    return input_dialog(ui, label, out, size, NULL, NULL, NULL, NULL);
}
bool new_entry_dialog(UiContext *ui, bool directory, char *name, size_t size) {
    return input_dialog(ui, "New - File or Directory", name, size, &directory, NULL, NULL, NULL);
}
bool prompt_value(UiContext *ui, const char *label, char *out, size_t size, const char *initial) {
    return input_dialog(ui, label, out, size, NULL, initial, NULL, NULL);
}
bool search_prompt(UiContext *ui, WINDOW *win, char *out, size_t size) {
    return input_dialog(ui, "Search", out, size, NULL, out, win, NULL);
}
void rename_entry(UiContext *ui) {
    if(ui->app.marks_len>1) { message(ui,"Rename unavailable: multiple marked items");return; }
    if (ui->selected >= ui->app.files.len) return;
    char *source = text_copy(ui->app.files.entries[ui->selected].path);
    char *name = text_copy(ui->app.files.entries[ui->selected].name);
    if (!source || !name) message(ui, "Rename: Out of memory");
    else {
        char out[UI_INPUT_CAP];
        input_dialog(ui, "Rename selected item", out, sizeof out, NULL, name, NULL, source);
    }
    free(source); free(name);
}
bool confirm(UiContext *ui, const char *name, bool directory) {
    WINDOW *win = dialog_open(ui, "Confirm deletion", 8, 78);
    if (!win) return false;
    bool yes = false, result = false;
    size_t start = 0;
    for (;;) {
        int h, w; getmaxyx(win, h, w);
        dialog_frame(win, "Confirm deletion");
        draw_window_text(win, 1, 2, w - 4, directory ? "Delete directory and all its contents?" : "Delete this file?");
        size_t pages = 0, page = 0;
        for (size_t at = 0;;) {
            if (at == start) page = pages;
            pages++;
            size_t end = ui_text_span(name, at, w - 4).end;
            if (!name[end]) break;
            at = end;
        }
        wattron(win, A_BOLD);
        size_t next = draw_window_page(win, 2, 2, w - 4, name, start);
        wattroff(win, A_BOLD);
        if (pages > 1) {
            mvwaddstr(win, h - 3, 2, "[<]"); mvwaddstr(win, h - 3, w - 5, "[>]");
            char hint[80]; snprintf(hint, sizeof hint, "Name %zu/%zu  PgUp/PgDn", page + 1, pages);
            draw_window_text(win, h - 3, 7, w - 14, hint);
        }
        draw_window_text(win, 3, 2, w - 4, "This cannot be undone.");
        dialog_button(win, h - 2, 2, "[ Delete ]", yes, true);
        dialog_button(win, h - 2, 14, "[ Cancel ]", !yes, true); dialog_refresh(win);
        int key = input_key(win);
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (mouse_click(&e) && e.y == y + h - 3) {
                if (e.x >= x + 2 && e.x < x + 5) key = KEY_PPAGE;
                else if (e.x >= x + w - 5 && e.x < x + w - 2) key = KEY_NPAGE;
            }
            if (mouse_click(&e) && e.y == y + h - 2) {
                if (e.x >= x + 2 && e.x < x + 12) { result = true; break; }
                if (e.x >= x + 14 && e.x < x + 24) break;
            }
        }
        if (key == KEY_NPAGE && name[next]) { start = next; yes = false; }
        if (key == KEY_PPAGE && start) {
            size_t previous = 0;
            while (ui_text_span(name, previous, w - 4).end < start)
                previous = ui_text_span(name, previous, w - 4).end;
            start = previous; yes = false;
        }
        if (key == 27 || key == KEY_RESIZE || key == 'n') break;
        if (key == '\t' || key == KEY_LEFT || key == KEY_RIGHT) yes = !yes;
        if (key == '\n' || key == KEY_ENTER) { result = yes; break; }
    }
    dialog_close(ui, win); return result;
}

static int choice_dialog(WINDOW *win, const char *title, const char **labels, int total, int *selection, int *scroll, const char *warning) {
    int selected_row = *selection, offset = *scroll, result = -1;
    for (;;) {
        int h, w; getmaxyx(win, h, w); int rows = h - 4;
        if (selected_row < offset) offset = selected_row;
        if (selected_row >= offset + rows) offset = selected_row - rows + 1;
        dialog_frame(win, title);
        for (int i = 0; i < rows && offset + i < total; i++) {
            if (offset + i == selected_row) wattron(win, ui_selection());
            draw_window_text(win, i + 1, 2, w - 4, labels[offset + i]); wattroff(win, ui_selection());
        }
        if (warning && *warning) {
            wattron(win, COLOR_PAIR(UI_SPECIAL) | A_BOLD);
            draw_window_text(win, h - 3, 2, w - 4, warning);
            wattroff(win, COLOR_PAIR(UI_SPECIAL) | A_BOLD);
        }
        draw_window_text(win, h - 2, 2, w - 4, "Up/Down  Enter: choose  Esc: close"); dialog_refresh(win);
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
    *selection = result >= 0 ? result : selected_row; *scroll = offset;
    return result;
}
int show_menu(UiContext *ui) {
    const char *labels[] = {"F1   Help", "F2   New...", "F3   Search", "F5   Copy", "F6   Move / Rename", "F7   Options", "F8   Delete", "F10  Quit", "Backspace   Parent directory", "r    Refresh", "Ctrl+F  Find in current list", "Rename selected item", "!    Recent operation result", "z    Dismiss notification", "Select all visible items", "Clear selection"};
    if(ui->app.marks_len > 1) labels[11]="[disabled] Rename: multiple marked items";
    const int keys[] = {KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(5), KEY_F(6), KEY_F(7), KEY_F(8), KEY_F(10), KEY_BACKSPACE, 'r', UI_QUICK_FIND, UI_RENAME, UI_RESULT, 'z', UI_SELECT_ALL, UI_CLEAR_SELECTION};
    WINDOW *win = dialog_open(ui, "Menu", 14, 52);
    if (!win) return 0;
    int selected = 0, offset = 0;
    int i = choice_dialog(win, "Menu", labels, 16, &selected, &offset, NULL);
    dialog_close(ui, win);
    if (i == 11 && ui->app.marks_len > 1) { message(ui, "Rename unavailable: multiple marked items"); return 0; }
    return i < 0 ? 0 : keys[i];
}
void show_options(UiContext *ui) {
    WINDOW *win = dialog_open(ui, "Options", 10, 52);
    if (!win) return;
    int selected = 0, offset = 0;
    char warning[sizeof ui->status] = "";
    for (;;) {
        char hidden[64], preview_text[64], wheel[64], sort[64], direction[64];
        snprintf(hidden, sizeof hidden, "[%c] Show hidden files", ui->app.show_hidden ? 'x' : ' ');
        snprintf(preview_text, sizeof preview_text, "[%c] Show preview panel", ui->app.show_preview ? 'x' : ' ');
        snprintf(wheel, sizeof wheel, "Wheel scroll: %d rows (click to change)", ui->app.wheel_step);
        snprintf(sort, sizeof sort, "Sort by: %s (click to change)", sort_label(ui->app.sort.key));
        snprintf(direction, sizeof direction, "Sort order: %s (click to change)", ui->app.sort.descending ? "Descending" : "Ascending");
        const char *labels[] = {hidden, preview_text, wheel, sort, direction, "Done (settings apply to this session)"};
        int i = choice_dialog(win, "Options", labels, 6, &selected, &offset, warning);
        if (i < 0 || i == 5) break;
        if (i == 0) {
            ui->app.show_hidden = !ui->app.show_hidden;
            if (load_dir(ui, NULL).code != RESULT_OK) {
                ui->app.show_hidden = !ui->app.show_hidden;
                snprintf(warning, sizeof warning, "%s", ui->status);
            }
        }
        if (i == 1) ui->app.show_preview = !ui->app.show_preview;
        if (i == 2) ui->app.wheel_step = ui->app.wheel_step == 1 ? 3 : ui->app.wheel_step == 3 ? 5 : 1;
        if (i == 3) change_sort(ui, (SortSettings){(ui->app.sort.key + 1) % 4, ui->app.sort.descending});
        if (i == 4) change_sort(ui, (SortSettings){ui->app.sort.key, !ui->app.sort.descending});
        draw(ui);
    }
    dialog_close(ui, win);
}
