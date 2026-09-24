#include "../src/ui/ui.h"
#include <assert.h>
static bool fail_refresh;
Result __real_app_refresh(AppState *app);
Result __wrap_app_refresh(AppState *app) {
    return fail_refresh ? result_make(RESULT_ACCESS, "Injected refresh failure") : __real_app_refresh(app);
}
bool __wrap_confirm(UiContext *ui, const char *name, bool directory) {
    (void)ui; (void)name; (void)directory; return true; /* Disposable fixture only. */
}
static void ok(Result r) { if (r.code) fprintf(stderr, "%s\n", r.detail); assert(r.code == RESULT_OK); }
static void preserved(UiContext *ui, FileInfo *files, size_t len, const char *success) {
    assert(ui->app.files.entries == files && ui->app.files.len == len);
    assert(strstr(ui->status, success) && strstr(ui->status, "list refresh failed"));
}
int main(int argc, char **argv) {
    assert(argc == 2); const char *root = argv[1];
    UiContext ui = {0}; ok(app_init(&ui.app, root));
    FileInfo *files = ui.app.files.entries; size_t len = ui.app.files.len;
    ui.selected = 2; ui.top = 1; ui.preview_offset = 10;
    fail_refresh = true;
    assert(load_dir(&ui, NULL).code == RESULT_ACCESS && strstr(ui.status, "Refresh failed"));
    assert(ui.app.files.entries == files && ui.selected == 2 && ui.top == 1 && ui.preview_offset == 10);
    char warning[256];
    assert(create_named_entry(&ui, false, "created.txt", warning, sizeof warning));
    preserved(&ui, files, len, "File created");
    char *created = core_path_join(root, "created.txt"); FileInfo info;
    ok(core_info(created, &info)); file_info_free(&info);
    assert(create_named_entry(&ui, true, "created-dir", warning, sizeof warning));
    preserved(&ui, files, len, "Directory created");
    assert(transfer_path(&ui, false, created, root, "copy.txt", warning, sizeof warning));
    preserved(&ui, files, len, "Copied");
    assert(transfer_path(&ui, true, created, root, "moved.txt", warning, sizeof warning));
    preserved(&ui, files, len, "Moved");
    assert(core_info(created, &info).code == RESULT_NOT_FOUND);
    fail_refresh = false; ok(load_dir(&ui, "moved.txt"));
    assert(!strcmp(ui.app.files.entries[ui.selected].name, "moved.txt"));
    files = ui.app.files.entries; len = ui.app.files.len; fail_refresh = true;
    delete_entry(&ui); preserved(&ui, files, len, "Deleted");
    char *moved = core_path_join(root, "moved.txt"); assert(core_info(moved, &info).code == RESULT_NOT_FOUND);
    /* Failed deletion and failed refresh must both survive in the message. */
    delete_entry(&ui); assert(strstr(ui.status, "Delete: No changes") && strstr(ui.status, "list refresh failed"));
    assert(!strstr(ui.status, "Deleted"));
    assert(!open_search_result(&ui, moved)); assert(strstr(ui.status, "Cannot open search result"));
    assert(!strstr(ui.status, "Opened search result") && ui.app.files.entries == files);
    free(moved); free(created); preview_reset(&ui); app_free(&ui.app);
    puts("PASS: refresh errors preserve UI state; create/copy/move/delete success remains distinct from refresh failure");
}
