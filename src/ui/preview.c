#include "ui.h"


void preview_reset(UiContext *ui) {
    preview_session_close(ui->preview_session); ui->preview_session = NULL;
    preview_text_free(&ui->preview_page); free(ui->preview_link); ui->preview_link = NULL;
    ui->preview_ready = false; ui->preview_result = result_make(RESULT_OK, NULL);
    ui->preview_offset = 0; ui->preview_more = false; free(ui->preview_path); ui->preview_path = NULL;
}
void preview_scroll(UiContext *ui, bool down) {
    if (down) { if (ui->preview_more) ui->preview_offset += (size_t)ui->app.wheel_step; }
    else ui->preview_offset = ui->preview_offset > (size_t)ui->app.wheel_step ? ui->preview_offset - ui->app.wheel_step : 0;
}

/* Preparation owns all I/O; rendering below only consumes the prepared page.
   No idle timer: metadata is checked on viewport changes or the next redraw
   after one second. Explicit refresh resets even a latched error. */
void preview_prepare(UiContext *ui, int rows) {
    if (!ui->app.show_preview || ui->selected >= ui->app.files.len) { preview_reset(ui); return; }
    const Item *it = &ui->app.files.entries[ui->selected];
    bool fresh = !ui->preview_path || strcmp(ui->preview_path, it->path);
    if (fresh) {
        preview_reset(ui); ui->preview_path = text_copy(it->path);
        if (!ui->preview_path) { ui->preview_result = result_make(RESULT_NO_MEMORY, "Out of memory"); return; }
        if (it->valid && it->kind == FILE_REGULAR)
            ui->preview_result = preview_session_open(it->path, &ui->preview_session);
        if (it->valid && it->kind == FILE_LINK)
            ui->preview_result = core_link_target(it->path, &ui->preview_link);
    }
    if (rows < 1 || it->kind != FILE_REGULAR || !it->valid) return;
    if (rows > PREVIEW_PAGE_MAX - 1) rows = PREVIEW_PAGE_MAX - 1;
    size_t content = it->has_posix_mode ? 8 : 7;
    size_t start = ui->preview_offset > content ? ui->preview_offset - content : 0;
    size_t limit = ui->preview_offset + (size_t)rows + 1 > content + start ?
        ui->preview_offset + (size_t)rows + 1 - content - start : 0;
    uint64_t now = core_monotonic_ms(); bool changed = false;
    bool viewport = !ui->preview_ready || start != ui->preview_start || limit != ui->preview_limit;
    if (ui->preview_session && ui->preview_result.code == RESULT_OK &&
        (viewport || now - ui->preview_checked >= 1000)) {
        ui->preview_result = preview_session_check(ui->preview_session, &changed);
        ui->preview_checked = now;
    }
    if (changed) {
        ui->preview_offset = 0; ui->preview_ready = false;
        start = 0; limit = (size_t)rows + 1 > content ? (size_t)rows + 1 - content : 0;
    }
    if (ui->preview_result.code == RESULT_OK && ui->preview_session && (viewport || changed)) {
        preview_text_free(&ui->preview_page);
        ui->preview_result = preview_session_page(ui->preview_session, start, limit, &ui->preview_page);
        if (ui->preview_result.code == RESULT_OK && !ui->preview_page.more && !ui->preview_page.binary) {
            size_t total = content + ui->preview_page.skipped + ui->preview_page.len;
            size_t max = total > (size_t)rows ? total - (size_t)rows : 0;
            if (ui->preview_offset > max) {
                ui->preview_offset = max; start = max > content ? max - content : 0;
                limit = max + (size_t)rows + 1 > content + start ? max + (size_t)rows + 1 - content - start : 0;
                preview_text_free(&ui->preview_page);
                ui->preview_result = preview_session_page(ui->preview_session, start, limit, &ui->preview_page);
            }
        }
        ui->preview_start = start; ui->preview_limit = limit; ui->preview_ready = true;
    }
    if (ui->preview_result.code != RESULT_OK) {
        preview_session_close(ui->preview_session); ui->preview_session = NULL;
        preview_text_free(&ui->preview_page); ui->preview_offset = 0;
    }
}

typedef struct { int x, y, width, rows; size_t line; } View;
static void preview_line(UiContext *ui, View *view, const char *text, int color) {
    if (view->line >= ui->preview_offset && view->line - ui->preview_offset < (size_t)view->rows) {
        attron(COLOR_PAIR(color));
        draw_text(view->y + (int)(view->line - ui->preview_offset), view->x, view->width, text);
        attroff(COLOR_PAIR(color));
    }
    view->line++;
}
static void preview_content(UiContext *ui, View *v, const Item *it) {
    char text[UI_INPUT_CAP + 80];
    if (ui->preview_result.code != RESULT_OK) {
        preview_line(ui, v, ui->preview_result.detail, UI_SPECIAL);
        preview_line(ui, v, "Refresh or select another file to retry", UI_SPECIAL);
        preview_line(ui, v, it->path, UI_MUTED); return;
    }
    preview_line(ui, v, it->name, item_color(it, false));
    preview_line(ui, v, it->path, UI_MUTED);
    if (!it->valid) { preview_line(ui, v, "Cannot read metadata", UI_SPECIAL); return; }
    snprintf(text, sizeof text, "Type: %s", (it->kind == FILE_DIRECTORY) ? "Directory" :
             (it->kind == FILE_LINK) ? "Symbolic link" : (it->kind == FILE_REGULAR) ? "File" : "Special file");
    preview_line(ui, v, text, UI_BASE);
    snprintf(text, sizeof text, "Size: %lld bytes", (long long)it->size); preview_line(ui, v, text, UI_BASE);
    if (it->has_posix_mode) { snprintf(text, sizeof text, "Mode: %04o", it->posix_mode); preview_line(ui, v, text, UI_BASE); }
    time_t modified = (time_t)it->modified;
    struct tm *tm = localtime(&modified);
    char date[64] = "unknown";
    if (tm) strftime(date, sizeof date, "%Y-%m-%d %H:%M", tm);
    snprintf(text, sizeof text, "Modified: %s", date); preview_line(ui, v, text, UI_BASE);
    if ((it->kind == FILE_LINK)) {
        if (ui->preview_link) { preview_line(ui, v, "Link target:", UI_DIR); preview_line(ui, v, ui->preview_link, UI_BASE); }
        return;
    }
    if ((it->kind == FILE_DIRECTORY)) { preview_line(ui, v, "Double-click or use Open to enter", UI_DIR); return; }
    if (!(it->kind == FILE_REGULAR)) return;
    const PreviewText *page = &ui->preview_page;
    if (page->binary) { preview_line(ui, v, "Binary file - no text preview", UI_MUTED); return; }
    preview_line(ui, v, "", UI_BASE); preview_line(ui, v, "Contents", UI_DIR);
    v->line += page->skipped;
    for (size_t i = 0; i < page->len; i++) preview_line(ui, v, page->lines[i], UI_BASE);
    if (page->more) v->line++;

}
void preview(UiContext *ui, int x, int y, int w, int h) {
    if (!ui->app.files.len || ui->selected >= ui->app.files.len || w < 4 || h < 4) return;
    const Item *it = &ui->app.files.entries[ui->selected];
    View v = { .x = x + 2, .y = y + 1, .width = w - 4, .rows = h - 3 };
    if (v.rows > PREVIEW_PAGE_MAX - 1) v.rows = PREVIEW_PAGE_MAX - 1;
    preview_content(ui, &v, it);
    ui->preview_more = v.line > ui->preview_offset + (size_t)v.rows;
    if (!ui->preview_more) {
        size_t max_offset = v.line > (size_t)v.rows ? v.line - (size_t)v.rows : 0;
        if (ui->preview_offset > max_offset) {
            ui->preview_offset = max_offset;
            for (int r = 0; r < v.rows; r++) mvhline(v.y + r, x + 1, ' ', w - 2);
            v.line = 0; preview_content(ui, &v, it);
        }
    }
    char hint[96];
    snprintf(hint, sizeof hint, "Wheel: scroll | Row %zu%s", ui->preview_offset + 1, ui->preview_more ? "" : " | End");
    attron(COLOR_PAIR(UI_HEADER)); mvhline(y + h - 2, x + 1, ' ', w - 2);
    draw_text(y + h - 2, x + 2, w - 4, hint); attroff(COLOR_PAIR(UI_HEADER));
}
