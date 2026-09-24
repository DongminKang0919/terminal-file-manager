#include "ui.h"

static bool pick_path(UiContext *ui, bool folders_only, const char *initial, char *result) {
    WINDOW *win = dialog_open(ui, folders_only ? "Choose destination directory" : "Choose source file or directory", LINES - 2, 86);
    if (!win) return false;
    if (strlen(initial) >= UI_INPUT_CAP) { dialog_close(ui, win); message(ui, "Path exceeds terminal input limit"); return false; }
    char folder[UI_INPUT_CAP], warning[256] = "";
    snprintf(folder, sizeof folder, "%s", initial);
    FileInfo *entries = NULL;
    size_t picker_count = 0, choice = 0, offset = 0;
    bool reload = true, accepted = false;
    size_t last_choice = SIZE_MAX;
    uint64_t last_click = 0;
    for (;;) {
        if (reload) {
            FileList old = {entries, picker_count}; file_list_free(&old); entries = NULL; picker_count = choice = offset = 0;
            FileList list;
            Result read_result = core_list(folder, true, folders_only, &list);
            entries = list.entries; picker_count = list.len;
            if (read_result.code != RESULT_OK) snprintf(warning, sizeof warning, "Cannot read directory: %.220s", read_result.detail);
            reload = false; last_choice = SIZE_MAX;
        }
        int h, w; getmaxyx(win, h, w); int rows = h - 6;
        if (choice < offset) offset = choice;
        if (choice >= offset + (size_t)rows) offset = choice - rows + 1;
        dialog_frame(win, folders_only ? "Choose destination directory" : "Choose source file or directory");
        wattron(win, COLOR_PAIR(UI_PATH) | A_BOLD);
        mvwhline(win, 1, 1, ' ', w - 2); draw_window_text(win, 1, 2, w - 4, folder);
        wattroff(win, COLOR_PAIR(UI_PATH) | A_BOLD);
        draw_window_text(win, 2, 2, w - 4, "[Parent]  [Enter path...]");
        for (int i = 0; i < rows && offset + (size_t)i < picker_count; i++) {
            FileInfo *it = &entries[offset + i]; bool active = offset + i == choice;
            wattron(win, COLOR_PAIR(active ? UI_SELECTED : it->directory_target ? UI_DIR : UI_FILE));
            mvwhline(win, i + 3, 1, ' ', w - 2);
            draw_window_text(win, i + 3, 2, 11, it->directory_target ? "Directory  " : "File       ");
            draw_window_text(win, i + 3, 13, w - 15, it->name);
            wattroff(win, COLOR_PAIR(active ? UI_SELECTED : it->directory_target ? UI_DIR : UI_FILE));
        }
        if (!picker_count) draw_window_text(win, 3, 2, w - 4, folders_only ? "No subdirectories. You can use this directory." : "This directory is empty.");
        draw_window_text(win, h - 3, 2, w - 4, *warning ? warning : "Double-click/Enter: open   Space: use   p: path");
        wattron(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
        draw_window_text(win, h - 2, 2, w - 4, folders_only ? "[ Use directory ]    [ Open ]  [ Cancel ]" : "[ Use selected ]     [ Open ]  [ Cancel ]");
        wattroff(win, COLOR_PAIR(UI_HEADER) | A_BOLD); wrefresh(win);
        int key = input_key(win);
        bool use = false, open = false, up = false, path = false;
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            if (!wenclose(win, e.y, e.x)) continue;
            int y, x; getbegyx(win, y, x); int row = e.y - y, col = e.x - x;
            if (e.bstate & BUTTON4_PRESSED) key = KEY_UP;
            else if (e.bstate & BUTTON5_PRESSED) key = KEY_DOWN;
            else if (mouse_click(&e)) {
                if (row >= 3 && row < 3 + rows && offset + (size_t)(row - 3) < picker_count) {
                    choice = offset + row - 3;
                    uint64_t now = core_monotonic_ms();
                    uint64_t ms = now - last_click;
                    open = choice == last_choice && ms < 350;
                    last_choice = open ? SIZE_MAX : choice; last_click = now;
                }
                if (row == 2) { up = col >= 2 && col < 10; path = col >= 12 && col < 27; }
                if (row == h - 2) {
                    use = col >= 2 && col < 21; open = col >= 23 && col < 31;
                    if (col >= 32 && col < 42) break;
                }
            }
        }
        if (key == 27 || key == KEY_RESIZE) break;
        if (key == KEY_UP && choice) choice--;
        if (key == KEY_DOWN && choice + 1 < picker_count) choice++;
        if (key == KEY_HOME) choice = 0;
        if (key == KEY_END && picker_count) choice = picker_count - 1;
        if (key == KEY_PPAGE) choice = choice > (size_t)rows ? choice - rows : 0;
        if (key == KEY_NPAGE && picker_count) { choice += rows; if (choice >= picker_count) choice = picker_count - 1; }
        up |= key == KEY_LEFT || key == KEY_BACKSPACE || key == 127;
        path |= key == 'p'; open |= key == KEY_RIGHT || key == '\n' || key == KEY_ENTER;
        use |= key == ' ';
        if (up) {
            char *parent = core_path_parent(folder);
            if (parent) { snprintf(folder, sizeof folder, "%s", parent); free(parent); reload = true; warning[0] = 0; }
        } else if (path) {
            char input[UI_INPUT_CAP];
            int old_h = LINES, old_w = COLS;
            if (prompt_value(ui, "Directory path - absolute or relative", input, sizeof input, folder)) {
                char *resolved = NULL;
                Result r = core_resolve_directory(folder, input, &resolved);
                if (r.code == RESULT_OK && strlen(resolved) < sizeof folder) {
                    snprintf(folder, sizeof folder, "%s", resolved); reload = true; warning[0] = 0;
                } else snprintf(warning, sizeof warning, "Cannot open directory: %.220s", r.code == RESULT_OK ? "Path exceeds input limit" : r.detail);
                free(resolved);
            }
            if (old_h != LINES || old_w != COLS) break;
            touchwin(win);
        } else if (use || open) {
            if (use && folders_only) { snprintf(result, UI_INPUT_CAP, "%s", folder); accepted = true; break; }
            if (!picker_count) continue;
            char full[UI_INPUT_CAP];
            if (join(full, sizeof full, folder, entries[choice].name) < 0) { snprintf(warning, sizeof warning, "Path is too long."); continue; }
            if (open && entries[choice].directory_target) {
                char *resolved = NULL;
                Result r = core_resolve_directory(NULL, full, &resolved);
                if (r.code != RESULT_OK || strlen(resolved) >= sizeof folder) {
                    snprintf(warning, sizeof warning, "Cannot open directory: %.220s", r.code == RESULT_OK ? "Path exceeds input limit" : r.detail); free(resolved); continue;
                }
                snprintf(folder, sizeof folder, "%s", resolved); free(resolved); reload = true; warning[0] = 0;
            } else if (!folders_only) { snprintf(result, UI_INPUT_CAP, "%s", full); accepted = true; break; }
        }
    }
    FileList old = {entries, picker_count}; file_list_free(&old); dialog_close(ui, win); return accepted;
}

void transfer_entry(UiContext *ui, bool move_it) {
    WINDOW *win = dialog_open(ui, move_it ? "Move / Rename" : "Copy", 13, 88);
    if (!win) return;
    if (strlen(ui->app.directory) >= UI_INPUT_CAP ||
        (ui->selected < ui->app.files.len && strlen(ui->app.files.entries[ui->selected].path) >= UI_INPUT_CAP)) {
        dialog_close(ui, win); message(ui, "Path exceeds terminal input limit"); return;
    }
    char source[UI_INPUT_CAP] = "", folder[UI_INPUT_CAP], name[UI_INPUT_CAP] = "", warning[256] = "";
    if (ui->selected < ui->app.files.len) {
        snprintf(source, sizeof source, "%s", ui->app.files.entries[ui->selected].path);
        snprintf(name, sizeof name, "%s", ui->app.files.entries[ui->selected].name);
    }
    snprintf(folder, sizeof folder, "%s", ui->app.directory);
    bool keep_name = true;
    int focus = 0, offset = 0;
    for (;;) {
        int h, w; getmaxyx(win, h, w); int rows = h - 4;
        if (focus < offset) offset = focus;
        if (focus >= offset + rows) offset = focus - rows + 1;
        char *source_name = core_path_name(source);
        if (!source_name) break;
        char labels[6][UI_INPUT_CAP + 80];
        snprintf(labels[0], sizeof labels[0], "1. Choose source: %s", *source ? source_name : "Choose a file or directory");
        snprintf(labels[1], sizeof labels[1], "2. Browse destination: %s", folder);
        snprintf(labels[2], sizeof labels[2], "3. Change name: %s", *name ? name : "Choose a source first");
        snprintf(labels[3], sizeof labels[3], "[%c] Keep original name (click to %s)", keep_name ? 'x' : ' ', keep_name ? "rename" : "restore");
        snprintf(labels[4], sizeof labels[4], "[ %s now ]", move_it ? "Move" : "Copy");
        snprintf(labels[5], sizeof labels[5], "[ Cancel ]");
        free(source_name);
        dialog_frame(win, move_it ? "Move / Rename" : "Copy");
        for (int i = 0; i < rows && offset + i < 6; i++) {
            wattron(win, COLOR_PAIR(offset + i == focus ? UI_SELECTED : UI_BASE));
            mvwhline(win, i + 1, 1, ' ', w - 2); draw_window_text(win, i + 1, 2, w - 4, labels[offset + i]);
            wattroff(win, COLOR_PAIR(offset + i == focus ? UI_SELECTED : UI_BASE));
        }
        if (rows > 6) draw_window_text(win, 7, 2, w - 4, move_it ? "Moves the original. Same directory + new name = rename." : "Creates a separate copy. The original stays where it is.");
        if (rows > 7) {
            char result[UI_INPUT_CAP * 2 + 16];
            char *destination = core_path_join(folder, name);
            snprintf(result, sizeof result, "Result: %s", destination ? destination : "Out of memory"); free(destination);
            wattron(win, COLOR_PAIR(UI_DIR)); draw_window_text(win, 8, 2, w - 4, result); wattroff(win, COLOR_PAIR(UI_DIR));
        }
        wattron(win, COLOR_PAIR(*warning ? UI_SPECIAL : UI_MUTED));
        draw_window_text(win, h - 3, 2, w - 4, *warning ? warning : "Click a field to change it, then choose the action below.");
        wattroff(win, COLOR_PAIR(*warning ? UI_SPECIAL : UI_MUTED));
        draw_window_text(win, h - 2, 2, w - 4, "Tab / Arrows: select   Enter: choose   Esc: cancel"); wrefresh(win);
        int key = input_key(win), action = -1;
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            if (!wenclose(win, e.y, e.x)) continue;
            int y, x; getbegyx(win, y, x);
            if (mouse_click(&e) && e.x > x && e.x < x + w - 1 && e.y > y && e.y < y + 1 + rows && offset + e.y - y - 1 < 6) action = focus = offset + e.y - y - 1;
            if (e.bstate & BUTTON4_PRESSED) key = KEY_UP;
            if (e.bstate & BUTTON5_PRESSED) key = KEY_DOWN;
        }
        if (key == 27 || key == KEY_RESIZE) break;
        if (key == KEY_UP || key == KEY_BTAB) focus = (focus + 5) % 6;
        if (key == KEY_DOWN || key == '\t') focus = (focus + 1) % 6;
        if (key == '\n' || key == KEY_ENTER) action = focus;
        int old_h = LINES, old_w = COLS;
        if (action == 0) {
            char picked[UI_INPUT_CAP];
            if (pick_path(ui, false, ui->app.directory, picked)) {
                snprintf(source, sizeof source, "%s", picked);
                if (keep_name) { char *base = core_path_name(source); if (base) { snprintf(name, sizeof name, "%s", base); free(base); } }
                warning[0] = 0;
            }
        } else if (action == 1) {
            char picked[UI_INPUT_CAP];
            if (pick_path(ui, true, folder, picked)) { snprintf(folder, sizeof folder, "%s", picked); warning[0] = 0; }
        } else if (action == 2 || action == 3) {
            if (action == 3 && !keep_name && *source) {
                char *base = core_path_name(source); if (base) { snprintf(name, sizeof name, "%s", base); free(base); } keep_name = true; warning[0] = 0;
            } else {
                char input[UI_INPUT_CAP];
                if (prompt_value(ui, "New name (no directory path)", input, sizeof input, name)) {
                    snprintf(name, sizeof name, "%s", input); keep_name = false; warning[0] = 0;
                }
            }
        } else if (action == 4) {
            if (transfer_path(ui, move_it, source, folder, name, warning, sizeof warning)) break;
        } else if (action == 5) break;
        if (old_h != LINES || old_w != COLS) break;
        touchwin(win);
    }
    dialog_close(ui, win);
}
