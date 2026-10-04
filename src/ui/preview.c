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
    graphics_probe_poll(ui);
    if (rows < 1) { graphics_probe_cancel(ui); media_reset(ui); ui->preview_ready = false; return; }
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
    int media_rows = rows;
    rows = rows > PREVIEW_METADATA_ROWS ? rows - PREVIEW_METADATA_ROWS : 1;
    size_t start = ui->preview_offset;
    size_t limit = (size_t)rows + 1;
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
        start = 0; limit = (size_t)rows + 1;
    }
    if (ui->preview_result.code == RESULT_OK && preview_session_media(ui->preview_session) != PREVIEW_NOT_MEDIA) {
        media_prepare(ui, media_rows, changed);
        ui->preview_ready=true; ui->preview_start=start; ui->preview_limit=limit;
        return;
    }
    if (ui->media_kind != PREVIEW_NOT_MEDIA) media_reset(ui);
    if (ui->preview_result.code == RESULT_OK && ui->preview_session && (viewport || changed)) {
        preview_text_free(&ui->preview_page);
        ui->preview_result = preview_session_page(ui->preview_session, start, limit, &ui->preview_page);
        if (ui->preview_result.code == RESULT_OK && !ui->preview_page.more && !ui->preview_page.binary) {
            size_t total = ui->preview_page.skipped + ui->preview_page.len +
                (ui->preview_page.empty ? 1 : 0);
            size_t max = total > (size_t)rows ? total - (size_t)rows : 0;
            if (ui->preview_offset > max) {
                ui->preview_offset = max; start = max;
                limit = (size_t)rows + 1;
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
/* Wrap by terminal cells, never splitting a UTF-8 character. */
static void status_wrap(View *v, int *row, const char *text, int color, bool bold) {
    size_t at = 0;
    while (text && text[at] && *row < v->rows) {
        int cells = v->width < UI_INPUT_CAP / 4 ? v->width : UI_INPUT_CAP / 4;
        UiTextSpan span = ui_text_span(text, at, cells);
        if (span.end == at) break;
        size_t end = span.end;
        const char *newline = memchr(text + at, '\n', end - at);
        if (newline) end = (size_t)(newline - text);
        else if (span.more) {
            for (size_t i = end; i > at; --i)
                if (text[i - 1] == ' ') { end = i - 1; break; }
            if (end == at) end = span.end;
        }
        char line[UI_INPUT_CAP + 80];
        size_t len = end - at;
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, text + at, len); line[len] = 0;
        attron(COLOR_PAIR(color) | (bold ? A_BOLD : 0));
        draw_text(v->y + (*row)++, v->x, v->width, line);
        attroff(COLOR_PAIR(color) | (bold ? A_BOLD : 0));
        at = end; while (text[at] == ' ' || text[at] == '\n') at++;
    }
}
static void preview_status(UiContext *ui, View *v, const char *title,
                           const char *reason, const char *help, int color) {
    ui->preview_more = false;
    View block = *v;
    int pad = v->width >= 36 ? 2 : 0;
    block.x += pad; block.width -= pad * 2;
    int row = v->rows >= 9 ? v->rows / 3 : 0;
    status_wrap(&block, &row, title, color, true);
    if (v->rows >= 6 && row + 2 < v->rows) row++;
    status_wrap(&block, &row, reason, UI_BASE, false);
    if (help && v->rows >= 8 && v->width >= 28 && row + 2 < v->rows) {
        row++; status_wrap(&block, &row, help, UI_BASE, false);
    }
}
static void preview_header(UiContext *ui, View *v, const Item *it) {
    char text[UI_INPUT_CAP + 80];
    attron(A_BOLD);
    preview_line(ui, v, it->name, UI_BASE);
    attroff(A_BOLD);
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
}
static void preview_content(UiContext *ui, View *v, const Item *it) {
    char text[UI_INPUT_CAP + 80];
    if (!it->valid) {
        preview_status(ui,v,"[!] Preview failed","Cannot read file metadata.","Refresh to retry.",UI_SPECIAL); return;
    }
    if (ui->preview_result.code != RESULT_OK) {
        preview_status(ui,v,"[!] Preview failed",ui->preview_result.detail,"Refresh to retry.",UI_SPECIAL); return;
    }
    if ((it->kind == FILE_LINK)) {
        if (ui->preview_link) { preview_line(ui, v, "Link target:", UI_DIR); preview_line(ui, v, ui->preview_link, UI_BASE); }
        return;
    }
    if (it->kind == FILE_DIRECTORY) {
        if (!ui->preview_ready)
            preview_status(ui,v,"[...] Loading preview","Checking directory contents.",NULL,UI_DIR);
        else if (ui->preview_directory_empty)
            preview_status(ui,v,"[i] Empty directory","This directory contains no entries.",NULL,UI_DIR);
        else preview_status(ui,v,"[i] Preview unavailable","Directory contents are shown when opened.","Double-click or use Open to enter.",UI_DIR);
        return;
    }
    if (!(it->kind == FILE_REGULAR)) {
        preview_status(ui,v,"[i] Preview unavailable","This file type has no preview.",NULL,UI_DIR); return;
    }
    if (ui->media_kind != PREVIEW_NOT_MEDIA) {
        if (!ui->media_done) { preview_status(ui,v,"[...] Loading preview",ui->media_hint,NULL,UI_DIR); return; }
        if (ui->media_result.code != RESULT_OK) {
            bool setup = ui->media_result.code == RESULT_UNSUPPORTED;
            bool missing = setup && !strncmp(ui->media_result.detail,"Missing ",8);
            if (missing) snprintf(text,sizeof text,"%s\n%s",ui->media_result.detail,ui->media_hint);
            preview_status(ui,v,setup ? "[i] Preview unavailable" : "[!] Preview failed",
                missing ? text : ui->media_result.detail,missing ?
                    (strstr(ui->media_result.detail,"ImageMagick") ? "Install ImageMagick for image previews." : "Install Poppler for PDF preview tools.") :
                    setup ? NULL : "Refresh to retry.",setup ? (missing ? UI_PREVIEW_WARNING : UI_DIR) : UI_SPECIAL); return;
        }
        if (!ui->media_text && !ui->media_data) {
            preview_status(ui,v,!ui->image_auto ? "[i] Preview disabled" : "[i] Preview unavailable",
                ui->media_hint,!strcmp(ui->media_hint,"Panel too small for image preview") ?
                    "Enlarge the terminal to display the image." : "Open F7 to configure image preview.",UI_PREVIEW_WARNING); return;
        }
        if (ui->media_text) {
            if (!pdf_has_text(ui->media_data,ui->media_len)) {
                preview_status(ui,v,"[i] Preview unavailable","No text could be extracted from PDF page 1.",NULL,UI_DIR); return;
            }
            if (ui->media_fallback) preview_line(ui,v,ui->media_hint,UI_MUTED);
            preview_line(ui, v, "PDF page 1 (text, up to 64 KiB / 128 lines)", UI_MUTED);
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
    if (!ui->preview_ready) {
        preview_status(ui,v,"[...] Loading preview","Reading file contents.",NULL,UI_DIR); return;
    }
    const PreviewText *page = &ui->preview_page;
    if (page->empty) { preview_status(ui,v,"[i] Empty file","This file contains no data.",NULL,UI_DIR); return; }
    if (page->binary) { preview_status(ui,v,"[i] Preview unavailable","Binary content has no text preview.",NULL,UI_DIR); return; }
    v->line += page->skipped;
    for (size_t i = 0; i < page->len; i++) preview_line(ui, v, page->lines[i], UI_BASE);
    if (page->more) v->line++;

}
void preview(UiContext *ui, int x, int y, int w, int h) {
    if (!ui_panel(ui)->app.files.len || ui_panel(ui)->selected >= ui_panel(ui)->app.files.len || w < 4 || h < 4) return;
    const Item *it = &ui_panel(ui)->app.files.entries[ui_panel(ui)->selected];
    int pad = w >= 32 ? 2 : 1;
    for (int r = 1; r < h - 1; r++) mvhline(y + r, x + 1, ' ', w - 2);
    int content_top = h >= 10 ? 1 + PREVIEW_METADATA_ROWS : h >= 7 ? 5 : 3;
    View header = { .x = x + pad, .y = y + 1, .width = w - pad * 2, .rows = h >= 7 ? 3 : 1 };
    size_t offset = ui->preview_offset;
    ui->preview_offset = 0; preview_header(ui, &header, it); ui->preview_offset = offset;
    attron(COLOR_PAIR(UI_BORDER));
    mvhline(y + (h >= 7 ? 4 : 2), x + 1, ACS_HLINE, w - 2);
    attroff(COLOR_PAIR(UI_BORDER));
    View v = { .x = x + pad, .y = y + content_top,
               .width = w - pad * 2, .rows = h - (h >= 10 ? 2 : 1) - content_top };
    if (v.rows < 1) { ui->preview_more = false; return; }
    if (v.rows > PREVIEW_PAGE_MAX - 1) v.rows = PREVIEW_PAGE_MAX - 1;
    preview_content(ui, &v, it);
    ui->preview_more = v.line > ui->preview_offset + (size_t)v.rows;
    if (!v.line) { ui->preview_offset = 0; return; }
    if (!ui->preview_more) {
        size_t max = v.line > (size_t)v.rows ? v.line - (size_t)v.rows : 0;
        if (ui->preview_offset > max) {
            ui->preview_offset = max;
            for (int r = 0; r < v.rows; r++) mvhline(v.y + r,x + 1,' ',w - 2);
            v.line = 0; preview_content(ui,&v,it);
        }
    }
    if (!ui->preview_more && !ui->preview_offset) return;
    if (h < 10) return;
    char hint[96];
    snprintf(hint, sizeof hint, "Row %zu%s", ui->preview_offset + 1, ui->preview_more ? "" : " | End");
    attron(COLOR_PAIR(UI_MUTED)); mvhline(y + h - 2, x + 1, ' ', w - 2);
    draw_text(y + h - 2, x + 2, w - 4, hint); attroff(COLOR_PAIR(UI_MUTED));
}
