#include "ui.h"

void show_help(UiContext *ui) {
    static const char *lines[] = {
        "GETTING STARTED",
        "  Click a file to preview it; double-click a directory to open.",
        "  Click Parent in the file panel to return to the parent directory.",
        "  Select an item, then click Open (or double-click the item).",
        "  Wheel over the left list: move one row (default).",
        "  [<] / [>] beside Location: back / forward history.",
        "  Mouse side buttons: back / forward if sent by the terminal.",
        "  Alt+Left/Right or [ / ]: keyboard back / forward.",
        "  Wheel over Preview: scroll file information and text.",
        "  Selecting another file resets Preview to its first row.",
        "  Top buttons and Menu work with the mouse; shortcuts are optional.",
        "  Every window has an [ x ] close button. Esc also cancels.",
        "",
        "COPY AND MOVE",
        "  Choose source, browse destination, keep or change the name.",
        "  Copy keeps the original. Move changes its location.",
        "  In a picker: Open enters a directory; Use confirms your choice.",
        "  Keyboard: Arrows browse, Enter opens, Space confirms.",
        "  Nothing is transferred until Copy now / Move now is chosen.",
        "",
        "NAVIGATION",
        "  Up/Down or k/j       Move selection",
        "  PgUp/PgDn            Move one page",
        "  Home/End             First/last item",
        "  Enter or Right        Open directory; preview file on right",
        "  Backspace or Left     Go to parent directory",
        "  r                    Refresh current directory",
        "",
        "FILE OPERATIONS",
        "  F2                   New: choose File / Directory, enter name",
        "  F4                   New with Directory selected (shortcut)",
        "  F5                   Open the Copy form",
        "  F6                   Move or rename selected item",
        "  F8 or Delete         Delete selected item (confirmation)",
        "  Copy/Move: click fields to change source, directory or name.",
        "  Existing destinations are not overwritten.",
        "  New: duplicate names keep the form open for correction.",
        "  New: Tab/Shift-Tab changes fields; Left/Right changes type.",
        "",
        "SEARCH",
        "  F3 or /              Search names below current directory",
        "  In search: Up/Down, PgUp/PgDn to browse results",
        "  In search: Enter opens a result; / or F3 searches again",
        "  In search: Esc closes or cancels an active scan",
        "  Matches include full relative paths, including same-name files.",
        "",
        "OTHER",
        "  F9 or m              Menu (all commands)",
        "  F7                   Options (session settings)",
        "  [ x ] or Esc         Close a window",
        "  Directory / File / Link labels identify items in the main list.",
        "  Mouse: click to select, double-click to open, wheel to scroll",
        "  Mouse: click the top bar to run a command",
        "  q or F10             Quit tfile",
    };
    const size_t total = sizeof lines / sizeof lines[0];
    int screen_h, screen_w; getmaxyx(stdscr, screen_h, screen_w);
    if (screen_h < 9 || screen_w < 50) { message(ui, "Terminal too small for help"); return; }
    int h = screen_h - 2, w = screen_w - 8;
    if (w > 78) w = 78;
    WINDOW *win = newwin(h, w, (screen_h - h) / 2, (screen_w - w) / 2);
    if (!win) { message(ui, "Cannot open help window"); return; }
    keypad(win, TRUE); wbkgd(win, COLOR_PAIR(UI_BASE));
    size_t offset = 0; int key;
    for (;;) {
        dialog_frame(win, "Help");
        for (int row = 0; row < h - 3 && offset + (size_t)row < total; ++row) {
            const char *line = lines[offset + (size_t)row];
            bool heading = *line && *line != ' ';
            wattron(win, COLOR_PAIR(heading ? UI_DIR : UI_BASE) | (heading ? A_BOLD : A_NORMAL));
            mvwaddnstr(win, row + 1, 2, line, w - 4);
            wattroff(win, COLOR_PAIR(heading ? UI_DIR : UI_BASE) | (heading ? A_BOLD : A_NORMAL));
        }
        wattron(win, COLOR_PAIR(UI_MUTED));
        char hint[128];
        snprintf(hint, sizeof hint, "Wheel/Arrows: scroll  Esc: close  %zu/%zu", offset + 1, total);
        mvwaddnstr(win, h - 2, 2, hint, w - 4);
        wattroff(win, COLOR_PAIR(UI_MUTED));
        wrefresh(win);
        key = input_key(win);
        key = mouse_key(win, key);
        if (key == KEY_RESIZE || key == 27 || key == KEY_F(1) || key == 'q' || key == KEY_F(10)) break;
        if (key == KEY_UP && offset > 0) offset--;
        if (key == KEY_DOWN && offset + (size_t)(h - 3) < total) offset++;
        if (key == KEY_PPAGE) offset = offset > (size_t)(h - 3) ? offset - (size_t)(h - 3) : 0;
        if (key == KEY_NPAGE && offset + (size_t)(h - 3) < total) {
            offset += (size_t)(h - 3);
            if (offset + (size_t)(h - 3) > total) offset = total - (size_t)(h - 3);
        }
    }
    dialog_close(ui, win);
}
