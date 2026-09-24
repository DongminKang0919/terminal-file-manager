#include "ui.h"

/* The synchronous callback is the only input handler while a job is active.
   It never dispatches commands. Drawing is throttled; polling is not. */
typedef struct {
    WINDOW *win;
    bool copy, drawn, cancelled;
    int height, width;
    uint64_t last_draw;
} OperationView;
static bool operation_progress(const OperationProgress *progress, void *context) {
    OperationView *view = context;
    if (LINES != view->height || COLS != view->width) view->cancelled = true;
    uint64_t now = core_monotonic_ms();
    if (!view->cancelled && (!view->drawn || now - view->last_draw >= 100)) {
        int h, w; getmaxyx(view->win, h, w);
        dialog_frame(view->win, view->copy ? "Copying" : "Deleting");
        draw_window_text(view->win, 1, 2, w - 4, progress->path);
        char text[96];
        snprintf(text, sizeof text, "Completed items: %llu", (unsigned long long)progress->completed_items);
        draw_window_text(view->win, 2, 2, w - 4, text);
        if (view->copy) snprintf(text, sizeof text, "Copied bytes: %llu", (unsigned long long)progress->copied_bytes);
        else snprintf(text, sizeof text, "Deleted items cannot be restored");
        draw_window_text(view->win, 3, 2, w - 4, text);
        draw_window_text(view->win, 4, 2, w - 4, "Cancel stops here; changes are kept");
        wattron(view->win, COLOR_PAIR(UI_SELECTED));
        draw_window_text(view->win, h - 2, 2, w - 4, "[ Cancel ]  Esc / Enter");
        wattroff(view->win, COLOR_PAIR(UI_SELECTED));
        wrefresh(view->win); view->last_draw = now; view->drawn = true;
    }
    /* Bound input work even if a terminal continuously sends unrelated events. */
    for (int i = 0; i < 64 && !view->cancelled; i++) {
        wint_t key; int kind = input_wide(view->win, &key);
        if (kind == ERR) break;
        if ((kind == OK && (key == 27 || key == '\n' || key == '\r')) ||
            (kind == KEY_CODE_YES && (key == KEY_RESIZE || key == KEY_ENTER))) view->cancelled = true;
        if (kind == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e;
            if (getmouse(&e) == OK && mouse_click(&e)) {
                int y, x, h, w; getbegyx(view->win, y, x); getmaxyx(view->win, h, w); (void)w;
                if (dialog_closed(view->win, &e) ||
                    (e.y == y + h - 2 && e.x >= x + 2 && e.x < x + 12)) view->cancelled = true;
            }
        }
    }
    return !view->cancelled;
}
Result run_file_operation(UiContext *ui, bool copy, const char *source,
                          const char *directory, const char *name, char **destination) {
    OperationView view = {.copy = copy, .height = LINES, .width = COLS};
    /* Controllers also run without a terminal in native regression tests. */
    if (stdscr) {
        view.win = dialog_open(ui, copy ? "Copying" : "Deleting", 7, 76);
        if (!view.win) return result_make(RESULT_CANCELLED, "Cannot open progress window; no changes");
        wtimeout(view.win, 0);
    }
    Result r = copy ? core_transfer_progress(false, source, directory, name, destination,
                                             view.win ? operation_progress : NULL, &view) :
                      core_delete_progress(source, view.win ? operation_progress : NULL, &view);
    if (view.win) {
        /* Discard queued keyboard/mouse commands, including those behind Cancel.
           Resize has already been consumed, and draw() uses the new dimensions. */
        flushinp();
        delwin(view.win); touchwin(stdscr);
    }
    return r;
}
