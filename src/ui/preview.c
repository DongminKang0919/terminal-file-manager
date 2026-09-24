#include "ui.h"


void preview_reset(UiContext *ui) {
    ui->preview_offset = 0; ui->preview_more = false; free(ui->preview_path); ui->preview_path = NULL;
}
void preview_scroll(UiContext *ui, bool down) {
    if (down) { if (ui->preview_more) ui->preview_offset += (size_t)ui->app.wheel_step; }
    else ui->preview_offset = ui->preview_offset > (size_t)ui->app.wheel_step ? ui->preview_offset - ui->app.wheel_step : 0;
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
        char *target = NULL;
        if (core_link_target(it->path, &target).code == RESULT_OK) { preview_line(ui, v, "Link target:", UI_DIR); preview_line(ui, v, target, UI_BASE); }
        free(target);
        return;
    }
    if ((it->kind == FILE_DIRECTORY)) { preview_line(ui, v, "Double-click or use Open to enter", UI_DIR); return; }
    if (!(it->kind == FILE_REGULAR)) return;
    size_t content_row = v->line + 2;
    size_t start = ui->preview_offset > content_row ? ui->preview_offset - content_row : 0;
    size_t limit = ui->preview_offset + (size_t)v->rows + 1 > content_row + start ? ui->preview_offset + (size_t)v->rows + 1 - content_row - start : 0;
    PreviewText text_page;
    Result result = core_preview_text(it->path, start, limit, &text_page);
    if (result.code != RESULT_OK) { preview_line(ui, v, "Cannot read file contents", UI_SPECIAL); return; }
    if (text_page.binary) { preview_line(ui, v, "Binary file - no text preview", UI_MUTED); preview_text_free(&text_page); return; }
    preview_line(ui, v, "", UI_BASE); preview_line(ui, v, "Contents", UI_DIR);
    v->line += text_page.skipped;
    for (size_t i = 0; i < text_page.len; i++) preview_line(ui, v, text_page.lines[i], UI_BASE);
    if (text_page.more) v->line++;
    preview_text_free(&text_page);
}
void preview(UiContext *ui, int x, int y, int w, int h) {
    if (!ui->app.files.len || ui->selected >= ui->app.files.len || w < 4 || h < 4) return;
    const Item *it = &ui->app.files.entries[ui->selected];
    if (!ui->preview_path || strcmp(ui->preview_path, it->path)) {
        preview_reset(ui); ui->preview_path = text_copy(it->path);
    }
    View v = { .x = x + 2, .y = y + 1, .width = w - 4, .rows = h - 3 };
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
