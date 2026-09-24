#include "ui.h"

void message(UiContext *ui, const char *text) {
    (void)ui; snprintf(ui->status, sizeof ui->status, "%s", text); }
static void show_failure(UiContext *ui, const char *action, Result r) {
    snprintf(ui->status, sizeof ui->status, "%s: %.450s", action, r.detail);
}
static void reset_selection(UiContext *ui, const char *highlight) {
    ui->selected = ui->top = 0; preview_reset(ui);
    if (highlight) for (size_t i = 0; i < ui->app.files.len; i++)
        if (!strcmp(ui->app.files.entries[i].name, highlight)) { ui->selected = i; break; }
}
void change_sort(UiContext *ui, SortSettings sort) {
    app_set_sort(&ui->app, sort, &ui->selected);
    fit_selection(ui, stdscr && LINES > 7 ? LINES - 7 : 1);
}
const char *sort_label(SortKey key) {
    static const char *const labels[] = {"Name", "Size", "Modified", "Kind"};
    return key >= SORT_NAME && key <= SORT_KIND ? labels[key] : "Name";
}
Result load_dir(UiContext *ui, const char *highlight) {
    size_t previous = ui->selected;
    const char *wanted = highlight ? highlight : previous < ui->app.files.len ? ui->app.files.entries[previous].name : NULL;
    /* Refresh replaces the list, so copy identity before calling core. */
    char *name = wanted ? text_copy(wanted) : NULL;
    if (wanted && !name) {
        Result r = result_make(RESULT_NO_MEMORY, "Out of memory"); show_failure(ui, "Refresh failed", r); return r;
    }
    Result r = app_refresh(&ui->app);
    if (r.code == RESULT_OK) {
        preview_reset(ui);
        ui->selected = ui->app.files.len ? (previous < ui->app.files.len ? previous : ui->app.files.len - 1) : 0;
        if (name) for (size_t i = 0; i < ui->app.files.len; i++)
            if (!strcmp(ui->app.files.entries[i].name, name)) { ui->selected = i; break; }
        fit_selection(ui, stdscr && LINES > 7 ? LINES - 7 : 1);
    } else show_failure(ui, "Refresh failed", r);
    free(name); return r;
}
bool navigate(UiContext *ui, const char *path, const char *highlight) {
    char *name = highlight ? text_copy(highlight) : NULL;
    if (highlight && !name) { message(ui, "Open directory: Out of memory"); return false; }
    Result r = app_navigate(&ui->app, path);
    if (r.code == RESULT_OK) reset_selection(ui, name); else show_failure(ui, "Open directory", r);
    free(name);
    return r.code == RESULT_OK;
}
bool open_search_result(UiContext *ui, const char *path) {
    size_t selected; bool revealed;
    Result r = app_open_search_result(&ui->app, path, &selected, &revealed);
    if (r.code != RESULT_OK) { show_failure(ui, "Cannot open search result", r); return false; }
    ui->selected = selected; ui->top = 0; preview_reset(ui);
    message(ui, revealed ? "Opened search result; hidden files shown" : "Opened search result");
    return true;
}
static void refresh_after_operation(UiContext *ui, const char *highlight, const char *success) {
    Result r = load_dir(ui, highlight);
    if (r.code == RESULT_OK) message(ui, success);
    else snprintf(ui->status, sizeof ui->status, "%.370s; list refresh failed: %.100s", success, r.detail);
}
void history_dir(UiContext *ui, bool forward) {
    Result r = app_history(&ui->app, forward);
    if (r.code == RESULT_OK) { reset_selection(ui, NULL); message(ui, forward ? "Forward" : "Back"); }
    else show_failure(ui, forward ? "Forward" : "Back", r);
}
void parent_dir(UiContext *ui) {
    char *parent = core_path_parent(ui->app.directory), *name = core_path_name(ui->app.directory);
    if (parent) navigate(ui, parent, name);
    free(parent); free(name);
}
void enter_item(UiContext *ui) {
    if (ui->selected >= ui->app.files.len) return;
    FileInfo *it = &ui->app.files.entries[ui->selected];
    if (it->directory_target) navigate(ui, it->path, NULL);
    else { preview_reset(ui); message(ui, "Preview on the right - scroll there with the mouse wheel"); }
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
    Result r = core_create(ui->app.directory, name, directory);
    if (r.code != RESULT_OK) { operation_warning(r, warning, size); return false; }
    refresh_after_operation(ui, name, directory ? "Directory created" : "File created"); return true;
}
void create_entry(UiContext *ui, bool directory) {
    (void)ui; char name[UI_INPUT_CAP]; new_entry_dialog(ui, directory, name, sizeof name); }
void delete_entry(UiContext *ui) {
    if (ui->selected >= ui->app.files.len) return;
    const FileInfo *it = &ui->app.files.entries[ui->selected];
    char *path = text_copy(it->path); if (!path) { message(ui, "Out of memory"); return; }
    if (!confirm(ui, it->name, it->kind == FILE_DIRECTORY)) {
        free(path); message(ui, "Delete cancelled"); return;
    }
    Result r = run_file_operation(ui, false, path, NULL, NULL, NULL); free(path);
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
    char *destination = NULL;
    Result r = move_it ? core_transfer(true, source, directory, name, &destination) :
                         run_file_operation(ui, true, source, directory, name, &destination);
    if (r.code != RESULT_OK) {
        operation_warning(r, warning, size);
        if (!move_it) {
            char summary[sizeof ui->status];
            snprintf(summary, sizeof summary, "%s; %s; completed: %llu; bytes: %llu; %.140s",
                     r.code == RESULT_CANCELLED ? "Copy cancelled" : "Copy failed",
                     r.partial ? "incomplete files/directories kept" : "no changes",
                     (unsigned long long)r.completed_items, (unsigned long long)r.copied_bytes, warning);
            refresh_after_operation(ui, NULL, summary);
            snprintf(warning, size, "%s", ui->status);
        }
        return false;
    }
    char success[sizeof ui->status];
    snprintf(success, sizeof success, "%s to %.450s", move_it ? "Moved" : "Copied", destination);
    if (!move_it) snprintf(success, sizeof success, "Copied; completed: %llu; bytes: %llu; to %.300s",
                           (unsigned long long)r.completed_items, (unsigned long long)r.copied_bytes, destination);
    refresh_after_operation(ui, name, success);
    free(destination); return true;
}
