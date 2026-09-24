#include "ui.h"

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    if (argc > 2) { fprintf(stderr, "Usage: %s [directory]\n", argv[0]); return 1; }
    UiContext context = {0}; UiContext *ui = &context;
    Result initialized = app_init(&ui->app, argc == 2 ? argv[1] : NULL);
    if (initialized.code != RESULT_OK) { fprintf(stderr, "%s\n", initialized.detail); app_free(&ui->app); return 1; }
    message(ui, "Click a file to preview. Double-click a directory to open. F1: Help");
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); init_theme();
    input_init();
    set_escdelay(25);
    uint64_t last_click = 0;
    size_t last_index = SIZE_MAX;

    load_dir(ui, NULL); int key;
    for (;;) {
        draw(ui); key = input_key(stdscr);
        int h, w; getmaxyx(stdscr, h, w); int rows = h - 7; if (rows < 1) rows = 1;
        if (key == KEY_MOUSE) {
            MEVENT event;
            if (getmouse(&event) != OK) continue;
            int list_width = ui->app.show_preview ? w / 2 : w;
            if (h < 9 || w < 50) continue;
            if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
                last_index = SIZE_MAX;
                if (ui->app.show_preview && event.x > list_width && event.x < w - 1 && event.y > 2 && event.y < h - 3) {
                    preview_scroll(ui, (event.bstate & BUTTON5_PRESSED) != 0);
                    continue;
                }
                if (event.x <= 0 || event.x >= list_width - 1 || event.y < 4 || event.y >= h - 3) continue;
                size_t max_top = ui->app.files.len > (size_t)rows ? ui->app.files.len - (size_t)rows : 0;
                if (event.bstate & BUTTON4_PRESSED) {
                    ui->top = ui->top > (size_t)ui->app.wheel_step ? ui->top - ui->app.wheel_step : 0;
                    ui->selected = ui->selected > (size_t)ui->app.wheel_step ? ui->selected - ui->app.wheel_step : 0;
                } else {
                    ui->top += ui->app.wheel_step; if (ui->top > max_top) ui->top = max_top;
                    if (ui->app.files.len) { ui->selected += ui->app.wheel_step; if (ui->selected >= ui->app.files.len) ui->selected = ui->app.files.len - 1; }
                }
                if (ui->selected < ui->top) ui->selected = ui->top;
                if (ui->selected >= ui->top + (size_t)rows) ui->selected = ui->top + rows - 1;
                key = 0;
            }
            else if (mouse_click(&event)) {
                if (event.y == 0) key = header_action(event.x, w);
                else if (event.y == 1 && event.x >= 1 && event.x < 4) key = UI_BACK;
                else if (event.y == 1 && event.x >= 5 && event.x < 8) key = UI_FORWARD;
                else if (event.y == 3 && event.x >= 2 && event.x < 10) key = KEY_BACKSPACE;
                else if (event.y == 3 && event.x >= 12 && event.x < 18) key = KEY_ENTER;
                else if (event.y >= 4 && event.y < h - 3 && event.x > 0 && event.x < list_width - 1 && ui->top + (size_t)(event.y - 4) < ui->app.files.len) {
                    ui->selected = ui->top + (size_t)(event.y - 4);
                    uint64_t now = core_monotonic_ms();
                    uint64_t ms = now - last_click;
                    bool twice = ui->selected == last_index && ms < 350;
                    key = twice ? '\n' : 0;
                    last_click = now; last_index = twice ? SIZE_MAX : ui->selected;
                } else key = 0;
            } else key = 0;
        }
        if (key != KEY_MOUSE && key != 0 && key != '\n') last_index = SIZE_MAX;
        if (key == KEY_F(9) || key == 'm') key = show_menu(ui);
        if (key == '[') key = UI_BACK;
        if (key == ']') key = UI_FORWARD;
        if (key == 'q' || key == KEY_F(10)) break;
        switch (key) {
            case UI_BACK: history_dir(ui, false); break;
            case UI_FORWARD: history_dir(ui, true); break;
            case KEY_F(7): show_options(ui); break;
            case KEY_F(1): case '?': show_help(ui); break;
            case KEY_UP: case 'k': if (ui->selected) ui->selected--; break;
            case KEY_DOWN: case 'j': if (ui->selected + 1 < ui->app.files.len) ui->selected++; break;
            case KEY_PPAGE: ui->selected = ui->selected > (size_t)rows ? ui->selected - (size_t)rows : 0; break;
            case KEY_NPAGE: if (ui->app.files.len) { ui->selected += (size_t)rows; if (ui->selected >= ui->app.files.len) ui->selected = ui->app.files.len - 1; } break;
            case KEY_HOME: ui->selected = 0; break;
            case KEY_END: if (ui->app.files.len) ui->selected = ui->app.files.len - 1; break;
            case '\n': case KEY_ENTER: case KEY_RIGHT: enter_item(ui); break;
            case KEY_BACKSPACE: case 127: case 8: case KEY_LEFT: parent_dir(ui); break;
            case KEY_F(2): create_entry(ui, false); break;
            case KEY_F(3): case '/': search_items(ui); break;
            case KEY_F(4): create_entry(ui, true); break;
            case KEY_F(5): transfer_entry(ui, false); break;
            case KEY_F(6): transfer_entry(ui, true); break;
            case KEY_F(8): case KEY_DC: delete_entry(ui); break;
            case 'r': load_dir(ui, NULL); message(ui, "Refreshed"); break;
            case KEY_RESIZE: break;
        }
    }
    endwin(); preview_reset(ui); app_free(&ui->app); return 0;
}
