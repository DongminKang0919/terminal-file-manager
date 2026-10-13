#include "ui.h"

static bool directory_progress(size_t entries, bool complete, bool waiting, void *context) {
    UiContext *ui = context;
    if (complete) {
        if (ui->directory_load_window) dialog_close(ui, ui->directory_load_window);
        ui->directory_load_window = NULL;
        ui->directory_load_started = ui->directory_load_drawn = 0;
        return true;
    }
    uint64_t now = core_monotonic_ms();
    if (!ui->directory_load_started) ui->directory_load_started = now ? now : 1;
    if (!ui->directory_load_window && now - ui->directory_load_started >= 100) {
        ui->directory_load_window = dialog_open(ui, "Loading directory", 7, 60);
        if (!ui->directory_load_window) return false;
    }
    /* Fast loads must leave queued navigation/form input in ncurses. Only an
       actual loading dialog takes input ownership; slow loads become cancellable
       after the 100 ms display threshold. */
    if (!ui->directory_load_window) {
        if (waiting) napms(10);
        return !core_media_shutdown_requested() && !core_editor_shutdown_requested();
    }
    WINDOW *host = ui->directory_load_window ? ui->directory_load_window : stdscr;
    if (ui->directory_load_window && (!ui->directory_load_drawn || now - ui->directory_load_drawn >= 100)) {
        int h, w; getmaxyx(host, h, w);
        dialog_frame(host, "Loading directory");
        char text[128]; snprintf(text, sizeof text, "Reading directory... %zu entries received", entries);
        draw_window_text(host, 1, 2, w - 4, text);
        draw_window_text(host, 2, 2, w - 4, "Previous list stays until loading completes.");
        draw_window_text(host, h - 3, 2, w - 4, "Esc: cancel");
        dialog_button(host, h - 2, 2, "[ Cancel ]", true, true);
        dialog_refresh(host); ui->directory_load_drawn = now;
    }
    wtimeout(host, waiting ? 40 : 0);
    int key = input_key(host);
    wtimeout(host, -1);
    bool cancel = key == 27 || key == KEY_RESIZE || key == KEY_F(10) || core_media_shutdown_requested() || core_editor_shutdown_requested();
    if (key == KEY_MOUSE) {
        MEVENT event;
        if (getmouse(&event) == OK && ui->directory_load_window)
            cancel |= dialog_closed(host, &event) || dialog_button_hit(host, &event, getmaxy(host) - 2, 2, "[ Cancel ]", true);
    }
    if (cancel) flushinp();
    return !cancel;
}
void ui_enable_async_reads(UiContext *ui) {
    ui->async_reads = true;
    for (unsigned i = 0; i < 2; i++) {
        ui->panels[i].app.load_progress = directory_progress;
        ui->panels[i].app.load_context = ui;
    }
}
