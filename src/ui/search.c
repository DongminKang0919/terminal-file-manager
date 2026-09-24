#include "ui.h"

typedef struct {
    Item *entries;
    size_t len, visited;
    bool stopped, limited;
    WINDOW *win;
    char term[256];
    unsigned frame;

} Search;

static void search_draw(Search *s, size_t choice, size_t offset, bool scanning) {
    int h, w; getmaxyx(s->win, h, w);
    dialog_frame(s->win, "Search");
    wattron(s->win, COLOR_PAIR(UI_DIR));
    mvwaddnstr(s->win, 1, 2, "Name: ", w - 4);
    mvwaddnstr(s->win, 1, 8, s->term, w - 10);
    wattroff(s->win, COLOR_PAIR(UI_DIR));
    char info[128];
    if (scanning) {
        const char *frames = "|/-\\";
        snprintf(info, sizeof info, "Searching %c  %zu checked  %zu found  Esc: cancel",
                 frames[s->frame++ % 4], s->visited, s->len);
    }
    else snprintf(info, sizeof info, "%zu results%s  Enter: open  /: search  Esc: close", s->len, s->limited ? " (limit)" : "");
    wattron(s->win, COLOR_PAIR(UI_MUTED)); mvwaddnstr(s->win, 2, 2, info, w - 4); wattroff(s->win, COLOR_PAIR(UI_MUTED));
    for (int row = 0; row < h - 5 && offset + (size_t)row < s->len; ++row) {
        Item *it = &s->entries[offset + (size_t)row];
        bool active = !scanning && offset + (size_t)row == choice;
        if (active) { wattron(s->win, COLOR_PAIR(UI_SELECTED)); mvwhline(s->win, row + 3, 1, ' ', w - 2); wattroff(s->win, COLOR_PAIR(UI_SELECTED)); }
        int pair = item_color(it, active);
        wattron(s->win, COLOR_PAIR(pair) | (active ? A_BOLD : A_NORMAL));
        mvwaddch(s->win, row + 3, 2, type_letter(item_type(it)));
        mvwaddnstr(s->win, row + 3, 4, it->name, w - 6);
        wattroff(s->win, COLOR_PAIR(pair) | (active ? A_BOLD : A_NORMAL));
    }
    if (!scanning && !s->len) mvwaddnstr(s->win, 3, 2, "No matches. Use Search to try another name.", w - 4);
    mvwaddnstr(s->win, h - 2, 2, scanning ? "[ Cancel ]" : "[ Search ]  [ Open ]", w - 4);
    wrefresh(s->win);
}

static bool search_progress(const SearchResult *progress, const char *path, void *context) {
    (void)path;
    Search *s = context;
    s->entries = progress->matches.entries; s->len = progress->matches.len;
    s->visited = progress->visited;
    search_draw(s, 0, s->len > 1 ? s->len - 1 : 0, true);
    nodelay(s->win, TRUE);
    int key = input_key(s->win);
    if (key == KEY_MOUSE) {
        MEVENT event;
        if (getmouse(&event) == OK) {
            int y, x, h, w; getbegyx(s->win, y, x); getmaxyx(s->win, h, w); (void)w;
            if (dialog_closed(s->win, &event) || (mouse_click(&event) && event.y == y + h - 2 && event.x >= x + 2 && event.x < x + 12)) s->stopped = true;
        }
    }
    if (key == KEY_RESIZE || key == 27) s->stopped = true;
    nodelay(s->win, FALSE);
    return !s->stopped;
}

void search_items(UiContext *ui) {
    int screen_h, screen_w; getmaxyx(stdscr, screen_h, screen_w);
    if (screen_h < 9 || screen_w < 50) { message(ui, "Terminal too small for search"); return; }
    int h = screen_h - 2, w = screen_w - 8;
    if (w > 90) w = 90;
    WINDOW *win = newwin(h, w, (screen_h - h) / 2, (screen_w - w) / 2);
    if (!win) { message(ui, "Cannot open search window"); return; }
    keypad(win, TRUE); wbkgd(win, COLOR_PAIR(UI_BASE));
    Search s = { .win = win };
    SearchResult result = {0};
    char *destination = NULL;
    bool choose_directory = false;
    for (;;) {
        search_draw(&s, 0, 0, false);
        if (!search_prompt(ui, win, s.term, sizeof s.term)) break;
        search_result_free(&result);
        s.entries = NULL; s.len = s.visited = 0; s.stopped = s.limited = false;
        s.frame = 0;
        result = core_search(ui->app.directory, s.term, search_default_limits(), search_progress, &s);
        s.entries = result.matches.entries; s.len = result.matches.len;
        s.visited = result.visited; s.stopped = result.stopped; s.limited = result.limited;
        if (result.result.code != RESULT_OK) { message(ui, result.result.detail); break; }
        if (s.stopped) break;
        size_t choice = 0, offset = 0, last_choice = SIZE_MAX;
        uint64_t last_click = 0;
        for (;;) {
            search_draw(&s, choice, offset, false);
            int key = input_key(win);
            if (key == KEY_MOUSE) {
                MEVENT event;
                if (getmouse(&event) == OK) {
                    if (dialog_closed(win, &event)) goto search_done;
                    if (!wenclose(win, event.y, event.x)) continue;
                    int wy, wx; getbegyx(win, wy, wx);
                    int row = event.y - wy - 3;
                    if (mouse_click(&event) && event.y == wy + h - 2) {
                        if (event.x >= wx + 2 && event.x < wx + 12) break;
                        if (event.x >= wx + 14 && event.x < wx + 22) key = '\n';
                    }
                    else if (event.bstate & BUTTON4_PRESSED) key = KEY_UP;
                    else if (event.bstate & BUTTON5_PRESSED) key = KEY_DOWN;
                    else if (mouse_click(&event) && event.x > wx && event.x < wx + w - 1 && row >= 0 && row < h - 5 && offset + (size_t)row < s.len) {
                        choice = offset + (size_t)row;
                        uint64_t now = core_monotonic_ms();
                        uint64_t ms = now - last_click;
                        bool twice = choice == last_choice && ms < 350;
                        last_choice = choice; last_click = now;
                        if (twice) key = '\n'; else continue;
                    } else continue;
                } else continue;
            }
            if (key == KEY_RESIZE || key == 27 || key == KEY_F(10)) goto search_done;
            if (key == '/' || key == KEY_F(3)) break;
            if (key == KEY_UP && choice > 0) choice--;
            if (key == KEY_DOWN && choice + 1 < s.len) choice++;
            if (key == KEY_PPAGE) choice = choice > (size_t)(h - 5) ? choice - (size_t)(h - 5) : 0;
            if (key == KEY_NPAGE && s.len) { choice += (size_t)(h - 5); if (choice >= s.len) choice = s.len - 1; }
            if ((key == '\n' || key == KEY_ENTER) && s.len) {
                destination = text_copy(s.entries[choice].path);
                choose_directory = (s.entries[choice].kind == FILE_DIRECTORY);
                goto search_done;
            }
            if (choice < offset) offset = choice;
            if (choice >= offset + (size_t)(h - 5)) offset = choice - (size_t)(h - 5) + 1;
        }
    }
search_done:;
    Result outcome = result.result;
    search_result_free(&result); dialog_close(ui, win);
    if (destination) {
        char *directory = choose_directory ? text_copy(destination) : core_path_parent(destination);
        char *name = choose_directory ? NULL : core_path_name(destination);
        if (directory && navigate(ui, directory, name)) message(ui, "Opened search result");
        free(directory); free(name); free(destination);
    } else if (s.stopped) message(ui, "Search cancelled");
    else if (outcome.code != RESULT_OK) message(ui, outcome.detail);
    else message(ui, "Search closed");
}

