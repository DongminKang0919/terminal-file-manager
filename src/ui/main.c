#include "ui.h"

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    if (argc > 2) { fprintf(stderr, "Usage: %s [directory]\n", argv[0]); return 1; }
    UiContext context = {0}; UiContext *ui = &context;
    Result initialized = ui_init(ui, argc == 2 ? argv[1] : NULL);
    if (initialized.code != RESULT_OK) { fprintf(stderr, "%s\n", initialized.detail); ui_free(ui); return 1; }

    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); init_theme();
    input_init(); graphics_init(ui); core_media_install_signals();
    set_escdelay(25);
    uint64_t last_click = 0;
    size_t last_index = SIZE_MAX;

    /* app_init already loaded and sorted this directory. */
    int key;
    for (;;) {
        if (core_media_shutdown_requested()) break;
        draw(ui);
        wtimeout(stdscr, (media_pending(ui) || core_media_cleanup_pending()) ? 40 : -1);
        key = input_key(stdscr);
        wtimeout(stdscr, -1); /* Forms and inline find retain their blocking input policy. */
        if (key == ERR) continue;
        int h, w; getmaxyx(stdscr, h, w); UiScreenLayout screen=ui_screen_layout(ui,w,h); UiLayout layout=screen.list; int rows = layout.list_rows; if (rows < 1) rows = 1;
        if (key == KEY_MOUSE) {
            MEVENT event;
            if (getmouse(&event) != OK) continue;
            int list_width = layout.list_width, list_x=screen.x[ui->active];
            if (h < 9 || w < 50) continue;
            int hit=ui_panel_at(&screen,event.x,event.y);
            if(ui->mode==UI_LIST_LIST && hit>=0 && (mouse_click(&event)||(event.bstate&(BUTTON4_PRESSED|BUTTON5_PRESSED)))) {
                if(ui->active!=(unsigned)hit) last_index=SIZE_MAX;
                ui_activate_panel(ui,(unsigned)hit);
                list_x=screen.x[ui->active]; list_width=screen.width[ui->active];
            }
            int local_x=event.x-list_x;
            if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
                last_index = SIZE_MAX;
                if (ui_preview_enabled(ui) && event.x > list_width && event.x < w - 1 && event.y > 2 && event.y < h - 3) {
                    preview_scroll(ui, (event.bstate & BUTTON5_PRESSED) != 0);
                    continue;
                }
                if (local_x <= 0 || local_x >= list_width - 1 || event.y < layout.list_y || event.y >= h - 3) continue;
                size_t max_top = ui_panel(ui)->app.files.len > (size_t)rows ? ui_panel(ui)->app.files.len - (size_t)rows : 0;
                if (event.bstate & BUTTON4_PRESSED) {
                    ui_panel(ui)->top = ui_panel(ui)->top > (size_t)ui->wheel_step ? ui_panel(ui)->top - ui->wheel_step : 0;
                    ui_panel(ui)->selected = ui_panel(ui)->selected > (size_t)ui->wheel_step ? ui_panel(ui)->selected - ui->wheel_step : 0;
                } else {
                    ui_panel(ui)->top += ui->wheel_step; if (ui_panel(ui)->top > max_top) ui_panel(ui)->top = max_top;
                    if (ui_panel(ui)->app.files.len) { ui_panel(ui)->selected += ui->wheel_step; if (ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) ui_panel(ui)->selected = ui_panel(ui)->app.files.len - 1; }
                }
                if (ui_panel(ui)->selected < ui_panel(ui)->top) ui_panel(ui)->selected = ui_panel(ui)->top;
                if (ui_panel(ui)->selected >= ui_panel(ui)->top + (size_t)rows) ui_panel(ui)->selected = ui_panel(ui)->top + rows - 1;
                key = 0;
            }
            else if (mouse_click(&event)) {
                if (event.y > 2 && event.y < h - 3) {
                    if (local_x > 0 && local_x < list_width - 1) ui->focus = UI_FOCUS_FILES;
                    else if (ui_preview_enabled(ui) && event.x > list_width && event.x < w - 1) ui->focus = UI_FOCUS_PREVIEW;
                }
                if (event.y == h - 2) key = UI_RESULT;
                else if (event.y == 0) key = header_action(event.x, w);
                else if (event.y == 1 && local_x >= 1 && local_x < 4) key = UI_BACK;
                else if (event.y == 1 && local_x >= 5 && local_x < 8) key = UI_FORWARD;
                else if (event.y == 3 && local_x >= 2 && local_x < 10) key = KEY_BACKSPACE;
                else if (event.y == 3 && local_x >= 12 && local_x < 18) key = KEY_ENTER;
                else if (event.y >= layout.list_y && event.y < h - 3 && local_x > 0 && local_x < list_width - 1 && ui_panel(ui)->top + (size_t)(event.y - layout.list_y) < ui_panel(ui)->app.files.len) {
                    ui_panel(ui)->selected = ui_panel(ui)->top + (size_t)(event.y - layout.list_y);
                    uint64_t now = core_monotonic_ms();
                    uint64_t ms = now - last_click;
                    bool twice = ui_panel(ui)->selected == last_index && ms < 350;
                    key = twice ? '\n' : 0;
                    last_click = now; last_index = twice ? SIZE_MAX : ui_panel(ui)->selected;
                } else key = 0;
            } else key = 0;
        }
        if (key != KEY_MOUSE && key != 0 && key != '\n') last_index = SIZE_MAX;
        if (key == KEY_F(9) || key == 'm') {
            key = show_menu(ui);
            if (key == KEY_BACKSPACE) ui->focus = UI_FOCUS_FILES;
        }
        if (key == '[') key = UI_BACK;
        if (key == ']') key = UI_FORWARD;
        if (key == 'q' || key == KEY_F(10)) break;
        if (panel_key(ui, key, h)) continue;
        switch (key) {
            case UI_MODE_PREVIEW: ui_set_mode(ui,UI_LIST_PREVIEW); break;
            case UI_MODE_FILES: ui_set_mode(ui,UI_LIST_ONLY); break;
            case UI_MODE_DUAL: ui_set_mode(ui,UI_LIST_LIST); break;
            case ' ':
                if (ui->focus == UI_FOCUS_FILES && ui_panel(ui)->selected < ui_panel(ui)->app.files.len) {
                    Result r = app_mark_toggle(&ui_panel(ui)->app, ui_panel(ui)->app.files.entries[ui_panel(ui)->selected].name);
                    if (r.code != RESULT_OK) message(ui, r.detail);
                }
                break;
            case UI_SELECT_ALL: {
                Result r = app_mark_all(&ui_panel(ui)->app); if (r.code != RESULT_OK) message(ui, r.detail); break;
            }
            case UI_CLEAR_SELECTION: app_marks_clear(&ui_panel(ui)->app); break;
            case '!': case UI_RESULT: show_result(ui); break;
            case 'z': notice_dismiss(ui); break;
            case 6: case UI_QUICK_FIND: quick_find(ui); break;
            case UI_RENAME: rename_entry(ui); break;
            case UI_BACK: history_dir(ui, false); break;
            case UI_FORWARD: history_dir(ui, true); break;
            case KEY_F(7): show_options(ui); break;
            case KEY_F(1): case '?': show_help(ui); break;
            case KEY_UP: case 'k': if (ui_panel(ui)->selected) ui_panel(ui)->selected--; break;
            case KEY_DOWN: case 'j': if (ui_panel(ui)->selected + 1 < ui_panel(ui)->app.files.len) ui_panel(ui)->selected++; break;
            case KEY_PPAGE: ui_panel(ui)->selected = ui_panel(ui)->selected > (size_t)rows ? ui_panel(ui)->selected - (size_t)rows : 0; break;
            case KEY_NPAGE: if (ui_panel(ui)->app.files.len) { ui_panel(ui)->selected += (size_t)rows; if (ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) ui_panel(ui)->selected = ui_panel(ui)->app.files.len - 1; } break;
            case KEY_HOME: ui_panel(ui)->selected = 0; break;
            case KEY_END: if (ui_panel(ui)->app.files.len) ui_panel(ui)->selected = ui_panel(ui)->app.files.len - 1; break;
            case '\n': case KEY_ENTER: case KEY_RIGHT: enter_item(ui); break;
            case KEY_BACKSPACE: case 127: case 8: case KEY_LEFT: parent_dir(ui); break;
            case KEY_F(2): create_entry(ui, false); break;
            case KEY_F(3): case '/': search_items(ui); break;
            case KEY_F(4): create_entry(ui, true); break;
            case KEY_F(5): transfer_entry(ui, false); break;
            case KEY_F(6): transfer_entry(ui, true); break;
            case KEY_F(8): case KEY_DC: delete_entry(ui); break;
            case 'r': if (load_dir(ui, NULL).code == RESULT_OK) message(ui, "Refreshed"); break;
            case KEY_RESIZE: break;
        }
    }
    graphics_clear(ui); endwin(); ui_free(ui); return 0;
}
