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
void load_dir(UiContext *ui, const char *highlight) {
    char *name = highlight ? text_copy(highlight) : NULL;
    Result r = app_refresh(&ui->app);
    if (r.code == RESULT_OK) reset_selection(ui, name); else show_failure(ui, "Open directory", r);
    free(name);
}
bool navigate(UiContext *ui, const char *path, const char *highlight) {
    char *name = highlight ? text_copy(highlight) : NULL;
    Result r = app_navigate(&ui->app, path);
    if (r.code == RESULT_OK) reset_selection(ui, name); else show_failure(ui, "Open directory", r);
    free(name);
    return r.code == RESULT_OK;
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
    load_dir(ui, name); message(ui, directory ? "Directory created" : "File created"); return true;
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
    Result r = core_delete(path); free(path); load_dir(ui, NULL);
    if (r.code != RESULT_OK) show_failure(ui, "Delete", r); else message(ui, "Deleted");
}
bool transfer_path(UiContext *ui, bool move_it, const char *source, const char *directory, const char *name, char *warning, size_t size) {
    char *destination = NULL;
    Result r = core_transfer(move_it, source, directory, name, &destination);
    if (r.code != RESULT_OK) { operation_warning(r, warning, size); return false; }
    load_dir(ui, name);
    snprintf(ui->status, sizeof ui->status, "%s to %.450s", move_it ? "Moved" : "Copied", destination);
    free(destination); return true;
}
