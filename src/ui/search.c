#include "ui.h"

typedef struct {
    Item *entries;
    size_t len, visited;
    bool stopped, limited, incomplete, close_requested;
    size_t skipped_directories, skipped_entries;
    Result outcome, omission;
    WINDOW *win;
    char term[256];
    unsigned frame;
    bool drawn;
    uint64_t last_draw;

} Search;

static void search_draw(Search *s, size_t choice, size_t offset, bool scanning) {
    int h, w; getmaxyx(s->win, h, w);
    dialog_frame(s->win, "Search");
    wattron(s->win, COLOR_PAIR(UI_DIR));
    draw_window_text(s->win, 1, 2, w - 4, "Name: ");
    draw_window_text(s->win, 1, 8, w - 10, s->term);
    wattroff(s->win, COLOR_PAIR(UI_DIR));
    char info[128];
    if (scanning) {
        const char *frames = "|/-\\";
        snprintf(info, sizeof info, "Searching %c  %zu checked  %zu found",
                 frames[s->frame++ % 4], s->visited, s->len);
    }
    else {
        const char *state = s->outcome.code != RESULT_OK ? "Error" : s->stopped ? "Cancelled" : s->incomplete ? "Incomplete" : "Complete";
        snprintf(info, sizeof info, "%s%s: %zu found, %zu dirs / %zu entries skipped", state, s->limited ? " (limit)" : "", s->len, s->skipped_directories, s->skipped_entries);
    }
    wattron(s->win, COLOR_PAIR(UI_MUTED)); draw_window_text(s->win, 2, 2, w - 4, info); wattroff(s->win, COLOR_PAIR(UI_MUTED));
    for (int row = 0; row < h - 6 && offset + (size_t)row < s->len; ++row) {
        Item *it = &s->entries[offset + (size_t)row];
        bool active = !scanning && offset + (size_t)row == choice;
        if (active) { wattron(s->win, COLOR_PAIR(UI_SELECTED)); mvwhline(s->win, row + 3, 1, ' ', w - 2); wattroff(s->win, COLOR_PAIR(UI_SELECTED)); }
        int pair = item_color(it, active);
        wattron(s->win, COLOR_PAIR(pair) | (active ? A_BOLD | (has_colors() ? 0 : A_REVERSE) : A_NORMAL));
        mvwaddch(s->win, row + 3, 2, type_letter(item_type(it)));
        draw_window_text(s->win, row + 3, 4, w - 6, it->name);
        wattroff(s->win, COLOR_PAIR(pair) | (active ? A_BOLD | (has_colors() ? 0 : A_REVERSE) : A_NORMAL));
    }
    if (!scanning && !s->len) draw_window_text(s->win, 3, 2, w - 4, "No results collected. Use Search to try again.");
    char detail[256];
    const Result *issue = s->outcome.code != RESULT_OK ? &s->outcome : &s->omission;
    if (issue->code != RESULT_OK) snprintf(detail, sizeof detail, "%.100s: %.150s", issue->detail, issue->path);
    else snprintf(detail, sizeof detail, "%s", scanning ? "Enter/Esc: keep results  x: close" : "Enter: open  /: search  Esc: close");
    draw_window_text(s->win, h - 3, 2, w - 4, detail);
    dialog_button(s->win, h - 2, 2, scanning ? "[ Cancel ]" : "[ Search ]", scanning, true);
    if (!scanning) dialog_button(s->win, h - 2, 14, "[ Open ]", false, s->len != 0);
    dialog_refresh(s->win);
}

static void search_update(Search *s, const SearchResult *result) {
    s->entries = result->matches.entries; s->len = result->matches.len;
    s->visited = result->visited; s->limited = result->limited; s->incomplete = result->incomplete;
    s->skipped_directories = result->skipped_directories; s->skipped_entries = result->skipped_entries;
    s->outcome = result->result; s->omission = result->first_omission;
}
static bool search_progress(const SearchResult *progress, const char *path, void *context) {
    (void)path;
    Search *s = context;
    search_update(s, progress);
    uint64_t now = core_monotonic_ms();
    if (!s->drawn || now - s->last_draw >= 100) {
        search_draw(s, 0, s->len > 1 ? s->len - 1 : 0, true);
        s->drawn = true; s->last_draw = now;
    }
    /* Poll every callback, including callbacks whose rendering was suppressed. */
    nodelay(s->win, TRUE);
    int key = input_key(s->win);
    if (key == KEY_MOUSE) {
        MEVENT event;
        if (getmouse(&event) == OK) {
            int y, x, h, w; getbegyx(s->win, y, x); getmaxyx(s->win, h, w); (void)w;
            if (dialog_closed(s->win, &event)) s->close_requested = s->stopped = true;
            else if (mouse_click(&event) && event.y == y + h - 2 && event.x >= x + 2 && event.x < x + 12) s->stopped = true;
        }
    }
    if (key == KEY_RESIZE || key == KEY_F(10)) s->close_requested = s->stopped = true;
    if (key == 27 || key == '\n' || key == KEY_ENTER) s->stopped = true;
    nodelay(s->win, FALSE);
    return !s->stopped;
}

void search_items(UiContext *ui) {
    int screen_h, screen_w; getmaxyx(stdscr, screen_h, screen_w);
    if (screen_h < 9 || screen_w < 50) { message(ui, "Terminal too small for search"); return; }
    int h = screen_h - 2, w = screen_w - 8;
    if (w > 90) w = 90;
    WINDOW *win = dialog_open(ui, "Search", 7, w < 60 ? w : 60);
    if (!win) { message(ui, "Cannot open search window"); return; }
    Search s = { .win = win };
    SearchResult result = {0};
    char *destination = NULL;
    for (;;) {
        /* Resize the existing window; never stack an input popup over results. */
        int input_w = w < 60 ? w : 60;
        if (mvwin(win, 0, 0) == ERR || wresize(win, 7, input_w) == ERR ||
            mvwin(win, (screen_h - 7) / 2, (screen_w - input_w) / 2) == ERR) {
            s.outcome = result_make(RESULT_NO_MEMORY, "Cannot resize search window"); break;
        }
        draw_cached(ui);
        if (!search_prompt(ui, win, s.term, sizeof s.term)) break;
        if (mvwin(win, 0, 0) == ERR || wresize(win, h, w) == ERR ||
            mvwin(win, (screen_h - h) / 2, (screen_w - w) / 2) == ERR) {
            s.outcome = result_make(RESULT_NO_MEMORY, "Cannot resize search window"); break;
        }
        draw_cached(ui);
        search_result_free(&result);
        WINDOW *host = s.win;
        char term[sizeof s.term]; memcpy(term, s.term, sizeof term);
        s = (Search){ .win = host }; memcpy(s.term, term, sizeof term);
        result = core_search(ui_panel(ui)->app.directory, s.term, search_default_limits(), search_progress, &s);
        search_update(&s, &result); s.stopped = result.stopped;
        if (s.close_requested) break;
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
                    else if (mouse_click(&event) && event.x > wx && event.x < wx + w - 1 && row >= 0 && row < h - 6 && offset + (size_t)row < s.len) {
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
            if (key == KEY_PPAGE) choice = choice > (size_t)(h - 6) ? choice - (size_t)(h - 6) : 0;
            if (key == KEY_NPAGE && s.len) { choice += (size_t)(h - 6); if (choice >= s.len) choice = s.len - 1; }
            if ((key == '\n' || key == KEY_ENTER) && s.len) {
                destination = text_copy(s.entries[choice].path);
                if (!destination) s.outcome = result_make(RESULT_NO_MEMORY, "Cannot open result: Out of memory");
                goto search_done;
            }
            if (choice < offset) offset = choice;
            if (choice >= offset + (size_t)(h - 6)) offset = choice - (size_t)(h - 6) + 1;
        }
    }
search_done:;
    if (s.close_requested) flushinp();
    search_result_free(&result); dialog_close(ui, win);
    if (destination) { open_search_result(ui, destination); free(destination); }
    else if (s.outcome.code != RESULT_OK) {
        ui->status_kind = NOTICE_ERROR;
        snprintf(ui->status, sizeof ui->status, "Search error: %.180s: %.250s", s.outcome.detail, s.outcome.path);
    } else if (s.stopped) {
        ui->status_kind = NOTICE_CANCELLED;
        snprintf(ui->status, sizeof ui->status, "Search cancelled: %zu found, %zu skipped%s", s.len, s.skipped_directories + s.skipped_entries, s.limited ? " (limit)" : "");
    } else if (s.incomplete) {
        ui->status_kind = NOTICE_WARNING;
        snprintf(ui->status, sizeof ui->status, "Search incomplete%s: %zu dirs / %zu entries skipped; %.120s: %.240s", s.limited ? " (limit)" : "", s.skipped_directories, s.skipped_entries, s.omission.detail, s.omission.path);
    } else if (s.limited) { message(ui, "Search limit reached"); ui->status_kind = NOTICE_WARNING; }
    /* Closing a normal search leaves the prior operation status intact. */
}
