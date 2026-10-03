#include "ui.h"

void message(UiContext *ui, const char *text) {
    ui->status_priority=false;
    ui->status_kind = NOTICE_INFO; snprintf(ui->status, sizeof ui->status, "%s", text); }
static void show_failure(UiContext *ui, const char *action, Result r) {
    ui->status_kind = r.code == RESULT_CANCELLED ? NOTICE_CANCELLED : NOTICE_ERROR;
    snprintf(ui->status, sizeof ui->status, "%s: %.450s", action, r.detail);
}
static void reset_selection(UiContext *ui, const char *highlight) {
    ui->focus = UI_FOCUS_FILES;
    ui_panel(ui)->stale=false;
    if(ui->status_priority) { ui->status_priority=false; ui->status[0]=0; }
    ui_panel(ui)->selected = ui_panel(ui)->top = 0; preview_reset(ui);
    if (highlight) for (size_t i = 0; i < ui_panel(ui)->app.files.len; i++)
        if (!strcmp(ui_panel(ui)->app.files.entries[i].name, highlight)) { ui_panel(ui)->selected = i; break; }
}
void change_sort(UiContext *ui, SortSettings sort) {
    app_set_sort(&ui_panel(ui)->app, sort, &ui_panel(ui)->selected);
    fit_selection(ui, stdscr && LINES >= 9 ? ui_screen_layout(ui,COLS,LINES).list.list_rows : 1);
}
const char *sort_label(SortKey key) {
    static const char *const labels[] = {"Name", "Size", "Modified", "Kind"};
    return key >= SORT_NAME && key <= SORT_KIND ? labels[key] : "Name";
}
Result load_dir(UiContext *ui, const char *highlight) {
    Result r=ui_refresh_panel(ui,ui->active,highlight,ui->mode==UI_LIST_LIST);
    if(r.code==RESULT_OK) {
        preview_reset(ui);
        if(ui->status_priority) { ui->status_priority=false; ui->status[0]=0; }
    } else show_failure(ui,"Refresh failed",r);
    return r;
}

bool navigate(UiContext *ui, const char *path, const char *highlight) {
    char *name = highlight ? text_copy(highlight) : NULL;
    if (highlight && !name) { message(ui, "Open directory: Out of memory"); return false; }
    Result r = app_remember_selection(&ui_panel(ui)->app, ui_panel(ui)->selected, ui_panel(ui)->top);
    if (r.code == RESULT_OK) r = app_navigate(&ui_panel(ui)->app, path);
    if (r.code == RESULT_OK) reset_selection(ui, name); else show_failure(ui, "Open directory", r);
    free(name);
    return r.code == RESULT_OK;
}
bool open_search_result(UiContext *ui, const char *path) {
    size_t selected; bool revealed;
    Result r = app_remember_selection(&ui_panel(ui)->app, ui_panel(ui)->selected, ui_panel(ui)->top);
    if (r.code == RESULT_OK) r = app_open_search_result(&ui_panel(ui)->app, path, &selected, &revealed);
    if (r.code != RESULT_OK) { show_failure(ui, "Cannot open search result", r); return false; }
    ui_panel(ui)->stale=false;
    ui_panel(ui)->selected = selected; ui_panel(ui)->top = 0; preview_reset(ui);
    message(ui, revealed ? "Opened search result; hidden files shown" : "Opened search result");
    return true;
}
static void refresh_after_operation(UiContext *ui, const char *highlight, const char *success) {
    refresh_operation_lists(ui,highlight);
    Result r=ui->notice.refresh;
    if (r.code == RESULT_OK) message(ui, success);
    else snprintf(ui->status, sizeof ui->status, "%.370s; list refresh failed: %.100s", success, r.detail);
}
void history_dir(UiContext *ui, bool forward) {
    Result r = app_remember_selection(&ui_panel(ui)->app, ui_panel(ui)->selected, ui_panel(ui)->top);
    if (r.code == RESULT_OK) r = app_history(&ui_panel(ui)->app, forward);
    if (r.code == RESULT_OK) {
        const HistoryEntry *entry = &ui_panel(ui)->app.history[ui_panel(ui)->app.history_at];
        reset_selection(ui, NULL);
        size_t count = ui_panel(ui)->app.files.len;
        ui_panel(ui)->selected = count ? (entry->selected < count ? entry->selected : count - 1) : 0;
        if (entry->selected_name) for (size_t i = 0; i < count; i++) {
            if (!strcmp(ui_panel(ui)->app.files.entries[i].name, entry->selected_name)) { ui_panel(ui)->selected = i; break; }
        }
        int rows = stdscr && LINES >= 9 ? ui_screen_layout(ui,COLS,LINES).list.list_rows : 1;
        size_t max_top = count > (size_t)rows ? count - (size_t)rows : 0;
        ui_panel(ui)->top = entry->top < max_top ? entry->top : max_top;
        fit_selection(ui, rows);
        message(ui, forward ? "Forward" : "Back");
    } else show_failure(ui, forward ? "Forward" : "Back", r);
}
void parent_dir(UiContext *ui) {
    char *parent = core_path_parent(ui_panel(ui)->app.directory), *name = core_path_name(ui_panel(ui)->app.directory);
    if (parent) navigate(ui, parent, name);
    free(parent); free(name);
}
void enter_item(UiContext *ui) {
    if (ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) return;
    FileInfo *it = &ui_panel(ui)->app.files.entries[ui_panel(ui)->selected];
    if (it->directory_target) navigate(ui, it->path, NULL);
    else { ui_set_mode(ui,UI_LIST_PREVIEW); ui->focus = UI_FOCUS_PREVIEW; }
}
/* Fixed-size buffers belong only to the terminal forms, never to core paths. */
int join(char *out, size_t size, const char *directory, const char *name) {
    char *path = core_path_join(directory, name);
    if (!path) return -1;
    size_t n = strlen(path);
    if (n < size) memcpy(out, path, n + 1);
    free(path); return n < size ? 0 : -1;
}
static void operation_warning(Result r, char *warning, size_t size) {
    const char *text = r.detail;
    if (*r.path) { snprintf(warning, size, "%s", r.detail); return; }
    switch (r.code) {
        case RESULT_EXISTS: text = "Name already exists. Choose another name or directory."; break;
        case RESULT_INVALID_NAME: text = "Use a single name, not a path or parent-directory name."; break;
        case RESULT_SELF_TRANSFER: text = "Cannot copy or move a directory into itself."; break;
        case RESULT_CROSS_DEVICE: text = "Different drive: copy first, then delete the original."; break;
        default: break;
    }
    snprintf(warning, size, "%s", text);
}
bool create_named_entry(UiContext *ui, bool directory, const char *name, char *warning, size_t size) {
    if(!ui_operation_allowed(ui,warning,size)) return false;
    Result r = core_create(ui_panel(ui)->app.directory, name, directory);
    char *target = core_path_join(ui_panel(ui)->app.directory, name);
    notice_record(ui, directory ? "Create directory" : "Create file", r, NULL, target);
    free(target);
    if (r.code != RESULT_OK) { operation_warning(r, warning, size); refresh_peer_after_operation(ui); return false; }
    refresh_after_operation(ui, name, directory ? "Directory created" : "File created"); return true;
}
bool rename_named_entry(UiContext *ui, const char *source, const char *name, char *warning, size_t size) {
    if(!ui_operation_allowed(ui,warning,size)) return false;
    char *original = core_path_name(source);
    if (!original) { snprintf(warning, size, "Out of memory"); return false; }
    bool unchanged = !strcmp(original, name); free(original);
    if (unchanged) return true;
    char *requested=core_path_join(ui_panel(ui)->app.directory,name);
    OperationNotice prepared;
    Result r=requested ? notice_prepare(&prepared,"Rename",source,requested) : result_make(RESULT_NO_MEMORY,"Out of memory; no changes");
    free(requested);
    if(r.code!=RESULT_OK) { operation_warning(r,warning,size);return false; }
    char *destination = NULL;
    r = core_transfer(true, source, ui_panel(ui)->app.directory, name, &destination);
    notice_commit(ui,&prepared,r);
    free(destination);
    if (r.code != RESULT_OK) { operation_warning(r, warning, size); refresh_peer_after_operation(ui); return false; }
    const char *old=strrchr(source,'/'); app_unmark(&ui_panel(ui)->app,old?old+1:source);
    refresh_after_operation(ui, name, "Renamed"); return true;
}
void create_entry(UiContext *ui, bool directory) {
    if(!ui_operation_allowed(ui,NULL,0)) return;
    (void)ui; char name[UI_INPUT_CAP]; new_entry_dialog(ui, directory, name, sizeof name); }
void delete_entry(UiContext *ui) {
    if(!ui_operation_allowed(ui,NULL,0)) return;
    if (ui_panel(ui)->app.marks_len > 1) { batch_entry(ui, BATCH_DELETE); return; }
    if (ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) return;
    const FileInfo *it = &ui_panel(ui)->app.files.entries[ui_panel(ui)->selected];
    if (ui_panel(ui)->app.marks_len) for (size_t i=0;i<ui_panel(ui)->app.files.len;i++)
        if (app_marked(&ui_panel(ui)->app,ui_panel(ui)->app.files.entries[i].name)) { it=&ui_panel(ui)->app.files.entries[i]; break; }
    char *path = text_copy(it->path); if (!path) { message(ui, "Out of memory"); return; }
    if (!confirm(ui, it->name, it->kind == FILE_DIRECTORY)) {
        /* Closing a confirmation did no file work; preserve an unread result. */
        if (!(ui->notice.present && ui->notice.visible))
            notice_record(ui, "Delete", result_make(RESULT_CANCELLED, "Confirmation cancelled; no changes"), path, NULL);
        free(path); message(ui, "Delete cancelled"); return;
    }
    OperationNotice prepared;
    Result r=notice_prepare(&prepared,"Delete",path,NULL);
    if(r.code!=RESULT_OK) { message(ui,r.detail);free(path);return; }
    r = run_file_operation(ui, false, path, NULL, NULL, NULL);
    notice_commit(ui,&prepared,r);
    if (r.code == RESULT_OK) { const char *name=strrchr(path,'/'); app_unmark(&ui_panel(ui)->app,name?name+1:path); }
    free(path);
    char summary[sizeof ui->status];
    snprintf(summary, sizeof summary, "%s%.190s; completed: %llu%s",
             r.code == RESULT_OK ? "Deleted" : "Delete: ", r.code == RESULT_OK ? "" : r.detail,
             (unsigned long long)r.completed_items,
             r.partial ? "; some items already deleted; no rollback" : "");
    if (r.code == RESULT_CANCELLED)
        snprintf(summary, sizeof summary, "Delete cancelled; deleted: %llu%s; %.190s",
                 (unsigned long long)r.completed_items,
                 r.partial ? "; already deleted items are not restored" : "; no changes", r.detail);
    refresh_after_operation(ui, NULL, summary);
}
bool transfer_path(UiContext *ui, bool move_it, const char *source, const char *directory, const char *name, char *warning, size_t size) {
    if(!ui_operation_allowed(ui,warning,size)) return false;
    char *requested=core_path_join(directory,name);
    OperationNotice prepared;
    Result r=requested ? notice_prepare(&prepared,move_it?"Move":"Copy",source,requested) : result_make(RESULT_NO_MEMORY,"Out of memory; no changes");
    free(requested);
    if(r.code!=RESULT_OK) { operation_warning(r,warning,size);return false; }
    char *destination = NULL;
    r = move_it ? core_transfer(true, source, directory, name, &destination) :
                         run_file_operation(ui, true, source, directory, name, &destination);
    notice_commit(ui,&prepared,r);
    if (r.code != RESULT_OK) {
        free(destination);
        operation_warning(r, warning, size);
        if (!move_it) {
            char summary[sizeof ui->status];
            snprintf(summary, sizeof summary, "%s; %s; completed: %llu; bytes: %llu; %.140s",
                     r.code == RESULT_CANCELLED ? "Copy cancelled" : "Copy failed",
                     r.partial ? "incomplete files/directories kept" : "no changes",
                     (unsigned long long)r.completed_items, (unsigned long long)r.copied_bytes, warning);
            refresh_after_operation(ui, NULL, summary);
            /* Keep the actionable cause in the form. The full outcome/counters
               remain in the retained notice, rather than hiding the cause. */
            if (r.code == RESULT_CANCELLED)
                snprintf(warning, size, "Copy cancelled; %s", r.partial ? "changes kept" : "no changes");
        } else refresh_peer_after_operation(ui);
        return false;
    }
    char success[sizeof ui->status];
    snprintf(success, sizeof success, "%s to %.450s", move_it ? "Moved" : "Copied", destination);
    if (!move_it) snprintf(success, sizeof success, "Copied; completed: %llu; bytes: %llu; to %.300s",
                           (unsigned long long)r.completed_items, (unsigned long long)r.copied_bytes, destination);
    const char *source_name=strrchr(source,'/'); app_unmark(&ui_panel(ui)->app,source_name?source_name+1:source);
    refresh_after_operation(ui, name, success);
    free(destination); return true;
}

/* Panel-only keys never perform list navigation while preview has focus. */
bool panel_key(UiContext *ui, int key, int height) {
    if (!ui_preview_enabled(ui)) ui->focus = UI_FOCUS_FILES;
    if (key == '\t' || key == KEY_BTAB) {
        if(ui->mode==UI_LIST_LIST) { ui_activate_panel(ui,ui->active^1u); return true; }
        if (ui_preview_enabled(ui)) ui->focus = ui->focus == UI_FOCUS_FILES ? UI_FOCUS_PREVIEW : UI_FOCUS_FILES;
        return true;
    }
    if (ui->focus != UI_FOCUS_PREVIEW) return false;
    size_t page = height > 7 ? (size_t)(height - 7) : 1;
    if (page > PREVIEW_PAGE_MAX - 1) page = PREVIEW_PAGE_MAX - 1;
    switch (key) {
        case 27: ui->focus = UI_FOCUS_FILES; return true;
        case KEY_UP: case 'k':
            if (ui->preview_offset) ui->preview_offset--;
            return true;
        case KEY_DOWN: case 'j':
            if (ui->preview_more) ui->preview_offset++;
            return true;
        case KEY_PPAGE:
            ui->preview_offset = ui->preview_offset > page ? ui->preview_offset - page : 0;
            return true;
        case KEY_NPAGE:
            if (ui->preview_more) ui->preview_offset += page;
            return true;
        case KEY_HOME: ui->preview_offset = 0; return true;
        case ' ': return true;
        case KEY_END: case KEY_LEFT: case KEY_RIGHT: case KEY_BACKSPACE:
        case 127: case 8: case '\n': case KEY_ENTER: return true;
        default: return false;
    }
}
