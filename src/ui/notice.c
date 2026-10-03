#include "ui.h"

const char *notice_label(NoticeKind kind) {
    static const char *labels[] = {"Info", "Success", "Cancelled", "Warning", "Error"};
    return labels[kind];
}
void notice_clear(UiContext *ui) {
    free(ui->notice.source); free(ui->notice.destination);
    ui->notice = (OperationNotice){0};
}
void notice_dismiss(UiContext *ui) {
    ui->notice.visible = false;
    ui->status[0] = 0;
}
void notice_record(UiContext *ui, const char *action, Result result,
                   const char *source, const char *destination) {
    notice_clear(ui);
    OperationNotice *n = &ui->notice;
    n->present = n->visible = true; n->operation = result;
    n->kind = result.code == RESULT_CANCELLED ? NOTICE_CANCELLED :
              result.code != RESULT_OK ? NOTICE_ERROR : NOTICE_SUCCESS;
    if (result.partial && result.code != RESULT_CANCELLED) n->kind = NOTICE_WARNING;
    snprintf(n->action, sizeof n->action, "%s", action);
    n->source = source ? text_copy(source) : NULL;
    n->destination = destination ? text_copy(destination) : NULL;
    ui->status[0] = 0;
}

/* Rows wrap original bytes with the same safe-token cell widths as the main UI.
   Only diagnostic newlines are separators; path control bytes stay escaped. */
typedef struct { char text[512]; } NoticeRow;
typedef struct { NoticeRow *rows; size_t len; bool failed; int width; } NoticeRows;
static void add_text(NoticeRows *r, const char *text, bool diagnostic) {
    if (r->failed) return;
    do {
        size_t end = ui_text_span(text, 0, r->width).end;
        if (diagnostic) {
            const char *newline = strchr(text, '\n');
            if (newline && (size_t)(newline - text) < end) end = (size_t)(newline - text);
        }
        NoticeRow *rows = realloc(r->rows, (r->len + 1) * sizeof *rows);
        if (!rows) { r->failed = true; return; }
        r->rows = rows;
        snprintf(r->rows[r->len++].text, sizeof rows->text, "%.*s", (int)end, text);
        text += end;
        if (diagnostic && *text == '\n') text++;
        else if (!end && *text) { r->failed = true; return; }
    } while (*text);
}
static void result_rows(NoticeRows *rows, const char *title, Result r) {
    char line[160];
    snprintf(line, sizeof line, "%s: %s (code %d)%s", title,
             r.code == RESULT_OK ? "Success" : r.code == RESULT_CANCELLED ? "Cancelled" : "Error",
             r.code, r.partial ? " | Partial completion" : "");
    add_text(rows, line, false);
    add_text(rows, r.detail, true);
    if (*r.path) { add_text(rows, "Diagnostic path:", false); add_text(rows, r.path, false); }
}
void show_result(UiContext *ui) {
    if (LINES < 9 || COLS < 50) return;
    int h = LINES - 2, w = COLS - 8; if (w > 78) w = 78;
    NoticeRows text = {.width = w - 4};
    OperationNotice *n = &ui->notice;
    if (!n->present) add_text(&text, "No recent file operation result.", false);
    else {
        add_text(&text, n->action, false);
        result_rows(&text, "File operation", n->operation);
        char counts[160];
        snprintf(counts, sizeof counts, "Reported completed items: %llu | Copied bytes: %llu",
                 (unsigned long long)n->operation.completed_items, (unsigned long long)n->operation.copied_bytes);
        add_text(&text, counts, false);
        if (n->source) { add_text(&text, "Source:", false); add_text(&text, n->source, false); }
        if (n->destination) { add_text(&text, "Destination / requested destination:", false); add_text(&text, n->destination, false); }
        add_text(&text, "Counters are reported values; create/move may report 0. Bytes include successful writes to unfinished files.", false);
        if (n->operation.code != RESULT_OK) {
            add_text(&text, n->operation.partial ?
                     "Changes already made are kept. Incomplete copies may remain; deleted items are not restored." :
                     "No filesystem changes reported by this operation.", false);
            add_text(&text, "Remaining item count and total progress are unknown. No automatic retry or rollback.", false);
        }
        if (n->refresh_attempted) result_rows(&text, "List refresh (separate result)", n->refresh);
        else add_text(&text, "List refresh: not attempted.", false);
        add_text(&text, "Result limits: detail 255 bytes; diagnostic path 1023 bytes (may retain only tail). Stored diagnostics may already be shortened.", false);
        add_text(&text, "Only the latest file operation result is kept. Acknowledgement closes its display; details remain available.", false);
    }
    if (text.failed) { free(text.rows); message(ui, "Cannot allocate result display"); return; }
    size_t selected = ui->selected, top = ui->top, preview_offset = ui->preview_offset;
    UiFocus focus = ui->focus; bool preview_more = ui->preview_more;
    WINDOW *win = dialog_open(ui, "Recent operation result", h, w);
    if (!win) { free(text.rows); return; }
    size_t offset = 0, page = (size_t)(h - 4), max = text.len > page ? text.len - page : 0;
    for (;;) {
        dialog_frame(win, "Recent operation result");
        size_t end = offset + page < text.len ? offset + page : text.len;
        for (size_t i = offset; i < end; i++) draw_window_text(win, 1 + (int)(i-offset), 2, w-4, text.rows[i].text);
        char hint[96]; snprintf(hint, sizeof hint, "Lines %zu-%zu/%zu | %s %s", offset+1, end, text.len,
                               offset ? "^ more" : "Top", end < text.len ? "v more" : "End");
        draw_window_text(win, h-3, 2, w-4, hint);
        dialog_button(win, h-2, 2, "[ Ack ]", false, n->present);
        draw_window_text(win, h-2, 11, w-13, "a: ack  Esc: close");
        dialog_refresh(win);
        int key = input_key(win);
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y,x; getbegyx(win,y,x);
            if (!wenclose(win,e.y,e.x)) continue;
            if (n->present && mouse_click(&e) && e.y == y+h-2 && e.x >= x+2 && e.x < x+9) key='a';
            else if (e.bstate & BUTTON4_PRESSED) key=KEY_UP;
            else if (e.bstate & BUTTON5_PRESSED) key=KEY_DOWN;
        }
        if (key == 'a' || key == '\n' || key == KEY_ENTER) { if (n->present) notice_dismiss(ui); break; }
        if (key == 27 || key == 'x' || key == KEY_RESIZE) break;
        if (key == KEY_UP && offset) offset--;
        if (key == KEY_DOWN && offset < max) offset++;
        if (key == KEY_PPAGE) offset = offset > page ? offset-page : 0;
        if (key == KEY_NPAGE) offset = offset+page < max ? offset+page : max;
        if (key == KEY_HOME) offset=0;
        if (key == KEY_END) offset=max;
    }
    dialog_close(ui, win); free(text.rows);
    ui->selected = selected; ui->top = top; ui->preview_offset = preview_offset;
    ui->focus = focus; ui->preview_more = preview_more;
}
