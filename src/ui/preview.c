#include "ui.h"


void preview_reset(UiContext *ui) {
    media_reset(ui);
    preview_session_close(ui->preview_session); ui->preview_session = NULL;
    preview_text_free(&ui->preview_page); free(ui->preview_link); ui->preview_link = NULL;
    ui->preview_ready = false; ui->preview_directory_empty = false; ui->preview_result = result_make(RESULT_OK, NULL);
    ui->preview_offset = 0; ui->preview_more = false; free(ui->preview_path); ui->preview_path = NULL;
}
void preview_scroll(UiContext *ui, bool down) {
    if (down) { if (ui->preview_more) ui->preview_offset += (size_t)ui->wheel_step; }
    else ui->preview_offset = ui->preview_offset > (size_t)ui->wheel_step ? ui->preview_offset - ui->wheel_step : 0;
}

/* Preparation owns all I/O; rendering below only consumes the prepared page.
   No idle timer: file metadata or directory emptiness is checked on viewport
   changes or the next redraw after one second. Explicit refresh resets even
   a latched error. Directory checks stop at the first actual entry. */
void preview_prepare(UiContext *ui, int rows) {
    core_media_reap();
    if (rows < 1) { media_reset(ui); ui->preview_ready = false; return; }
    if (!ui_preview_enabled(ui) || ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) { preview_reset(ui); return; }
    const Item *it = &ui_panel(ui)->app.files.entries[ui_panel(ui)->selected];
    bool fresh = !ui->preview_path || strcmp(ui->preview_path, it->path);
    if (fresh) {
        preview_reset(ui); ui->preview_path = text_copy(it->path);
        if (!ui->preview_path) { ui->preview_result = result_make(RESULT_NO_MEMORY, "Out of memory"); return; }
        if (it->valid && it->kind == FILE_REGULAR)
            ui->preview_result = preview_session_open(it->path, &ui->preview_session);
        if (it->valid && it->kind == FILE_LINK)
            ui->preview_result = core_link_target(it->path, &ui->preview_link);
    }
    if (it->valid && it->kind == FILE_DIRECTORY) {
        uint64_t now = core_monotonic_ms();
        bool viewport = !ui->preview_ready || ui->preview_limit != (size_t)(rows > 0 ? rows : 0) ||
            ui->preview_start != ui->preview_offset;
        if (ui->preview_result.code == RESULT_OK &&
            (viewport || now - ui->preview_checked >= 1000)) {
            ui->preview_result = core_preview_directory_empty(it->path, &ui->preview_directory_empty);
            ui->preview_checked = now;
            ui->preview_ready = true;
            ui->preview_limit = (size_t)(rows > 0 ? rows : 0);
            ui->preview_start = ui->preview_offset;
        }
        return;
    }
    if (rows < 1 || it->kind != FILE_REGULAR || !it->valid) return;
    if (rows > PREVIEW_PAGE_MAX - 1) rows = PREVIEW_PAGE_MAX - 1;
    size_t content = PREVIEW_METADATA_ROWS;
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
        media_reset(ui);
        ui->preview_offset = 0; ui->preview_ready = false;
        start = 0; limit = (size_t)rows + 1 > content ? (size_t)rows + 1 - content : 0;
    }
    if (ui->preview_result.code == RESULT_OK && preview_session_media(ui->preview_session) != PREVIEW_NOT_MEDIA) {
        media_prepare(ui, rows, changed);
        ui->preview_ready=true; ui->preview_start=start; ui->preview_limit=limit;
        return;
    }
    if (ui->media_kind != PREVIEW_NOT_MEDIA) media_reset(ui);
    if (ui->preview_result.code == RESULT_OK && ui->preview_session && (viewport || changed)) {
        preview_text_free(&ui->preview_page);
        ui->preview_result = preview_session_page(ui->preview_session, start, limit, &ui->preview_page);
        if (ui->preview_result.code == RESULT_OK && !ui->preview_page.more && !ui->preview_page.binary) {
            size_t total = content + ui->preview_page.skipped + ui->preview_page.len +
                (ui->preview_page.empty ? 1 : 0);
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
    if (ui->preview_result.code != RESULT_OK &&
        (ui->preview_session || ui->preview_page.lines)) {
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
static bool pdf_has_text(const char *data, size_t len) {
    for (size_t i=0; i<len; i++) {
        unsigned char c=(unsigned char)data[i];
        if (c!=' ' && c!='\t' && c!='\n' && c!='\r' && c!='\f' && c!='\v') return true;
    }
    return false;
}
static void preview_content(UiContext *ui, View *v, const Item *it) {
    char text[UI_INPUT_CAP + 80];
    attron(A_BOLD);
    preview_line(ui, v, it->name, UI_BASE);
    attroff(A_BOLD);
    if (!it->valid) { preview_line(ui, v, "Cannot verify contents: cannot read metadata", UI_SPECIAL); return; }
    char size[32]; ui_size(it, size, sizeof size);
    const char *kind = it->kind == FILE_DIRECTORY ? "Directory" : ui_kind(it);
    if (it->kind == FILE_DIRECTORY) snprintf(text, sizeof text, "%s", kind);
    else snprintf(text, sizeof text, "%s | %s", kind, size);
    size_t used = strlen(text);
    if (it->has_posix_mode && (int)used + 12 <= v->width)
        snprintf(text + used, sizeof text - used, " | Mode %04o", it->posix_mode);
    preview_line(ui, v, text, UI_MUTED);
    time_t modified = (time_t)it->modified;
    struct tm *tm = localtime(&modified);
    char date[64] = "unknown";
    if (tm) strftime(date, sizeof date, v->width >= 26 ? "%Y-%m-%d %H:%M" : "%Y-%m-%d", tm);
    snprintf(text, sizeof text, "%s %s", v->width >= 20 ? "Modified:" : "Mod:", date);
    preview_line(ui, v, text, UI_MUTED);
    if (ui->preview_result.code != RESULT_OK) {
        preview_line(ui, v, "Cannot verify contents", UI_SPECIAL);
        preview_line(ui, v, ui->preview_result.detail, UI_SPECIAL);
        preview_line(ui, v, "Refresh to retry", UI_MUTED); return;
    }
    if ((it->kind == FILE_LINK)) {
        if (ui->preview_link) { preview_line(ui, v, "Link target:", UI_DIR); preview_line(ui, v, ui->preview_link, UI_BASE); }
        return;
    }
    if (it->kind == FILE_DIRECTORY) {
        if (ui->preview_ready && ui->preview_directory_empty)
            preview_line(ui, v, "Empty directory", UI_MUTED);
        preview_line(ui, v, "Double-click or use Open to enter", UI_MUTED); return;
    }
    if (!(it->kind == FILE_REGULAR)) return;
    if (ui->media_kind != PREVIEW_NOT_MEDIA) {
        preview_line(ui, v, ui->media_hint, UI_MUTED);
        if (!ui->media_done) { preview_line(ui, v, "Loading preview...", UI_MUTED); return; }
        if (ui->media_result.code != RESULT_OK) {
            preview_line(ui, v, "Cannot display preview", UI_SPECIAL);
            preview_line(ui, v, ui->media_result.detail, UI_SPECIAL);
            preview_line(ui, v, "Refresh to retry", UI_MUTED); return;
        }
        if (ui->media_text) {
            preview_line(ui, v, "PDF page 1 (text, up to 64 KiB / 128 lines)", UI_MUTED);
            if (!pdf_has_text(ui->media_data,ui->media_len)) { preview_line(ui,v,"No text on PDF page 1",UI_MUTED); return; }
            const char *p=ui->media_data,*end=p+ui->media_len;
            for (size_t i=0; i<128 && p<end; i++) {
                const char *nl=memchr(p, '\n', (size_t)(end-p));
                size_t len=nl ? (size_t)(nl-p) : (size_t)(end-p);
                size_t used=0;
                for (size_t j=0; j<len && used<sizeof text-1; j++)
                    if (p[j]!='\f') text[used++]=p[j];
                text[used]=0;
                preview_line(ui,v,text,UI_BASE);
                p=nl ? nl+1 : end;
            }
        }
        return;
    }
    const PreviewText *page = &ui->preview_page;
    if (page->empty) { preview_line(ui, v, "Empty file", UI_MUTED); return; }
    if (page->binary) { preview_line(ui, v, "Binary file - no text preview", UI_MUTED); return; }
    v->line += page->skipped;
    for (size_t i = 0; i < page->len; i++) preview_line(ui, v, page->lines[i], UI_BASE);
    if (page->more) v->line++;

}
void preview(UiContext *ui, int x, int y, int w, int h) {
    if (!ui_panel(ui)->app.files.len || ui_panel(ui)->selected >= ui_panel(ui)->app.files.len || w < 4 || h < 4) return;
    const Item *it = &ui_panel(ui)->app.files.entries[ui_panel(ui)->selected];
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
    if (!ui->preview_more && !ui->preview_offset) return;
    char hint[96];
    snprintf(hint, sizeof hint, "Row %zu%s", ui->preview_offset + 1, ui->preview_more ? "" : " | End");
    attron(COLOR_PAIR(UI_MUTED)); mvhline(y + h - 2, x + 1, ' ', w - 2);
    draw_text(y + h - 2, x + 2, w - 4, hint); attroff(COLOR_PAIR(UI_MUTED));
}
