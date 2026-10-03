#include "../src/ui/ui.h"
#include <assert.h>
static bool fail_refresh, cancel_operation, cancel_confirmation;
static bool cancel_callback(const OperationProgress *p, void *context) {
    unsigned *calls = context;
    return ++*calls < 3 && !p->completed_items;
}
Result __real_run_file_operation(UiContext *, bool, const char *, const char *, const char *, char **);
Result __wrap_run_file_operation(UiContext *ui, bool copy, const char *source,
                                 const char *directory, const char *name, char **destination) {
    if (!cancel_operation) return __real_run_file_operation(ui,copy,source,directory,name,destination);
    unsigned calls = 0;
    return copy ? core_transfer_progress(false,source,directory,name,destination,cancel_callback,&calls) :
                  core_delete_progress(source,cancel_callback,&calls);
}
Result __real_app_refresh(AppState *app);
Result __wrap_app_refresh(AppState *app) {
    return fail_refresh ? result_make(RESULT_ACCESS, "Injected refresh failure") : __real_app_refresh(app);
}
bool __wrap_confirm(UiContext *ui, const char *name, bool directory) {
    (void)ui; (void)name; (void)directory; return !cancel_confirmation; /* Disposable fixture only. */
}
static void ok(Result r) { if (r.code) fprintf(stderr, "%s\n", r.detail); assert(r.code == RESULT_OK); }
static void preserved(UiContext *ui, FileInfo *files, size_t len, const char *success) {
    assert(ui_panel(ui)->app.files.entries == files && ui_panel(ui)->app.files.len == len);
    assert(ui->notice.present && ui->notice.visible && ui->notice.refresh_attempted);
    assert(ui->notice.refresh.code == RESULT_ACCESS);
    assert(strstr(ui->status, success) && strstr(ui->status, "list refresh failed"));
}
int main(int argc, char **argv) {
    assert(argc == 2); const char *root = argv[1];
    UiContext ui = {0}; ok(ui_init(&ui, root)); assert(ui.show_preview && ui.wheel_step==1);
    change_sort(&ui, (SortSettings){SORT_SIZE, true});
    FileInfo *files = ui_panel(&ui)->app.files.entries; size_t len = ui_panel(&ui)->app.files.len;
    ui_panel(&ui)->selected = 2; ui_panel(&ui)->top = 1; ui.preview_offset = 10;
    fail_refresh = true;
    assert(load_dir(&ui, NULL).code == RESULT_ACCESS && strstr(ui.status, "Refresh failed"));
    assert(ui_panel(&ui)->app.files.entries == files && ui_panel(&ui)->selected == 2 && ui_panel(&ui)->top == 1 && ui.preview_offset == 10);
    char warning[256];
    assert(create_named_entry(&ui, false, "created.txt", warning, sizeof warning));
    preserved(&ui, files, len, "File created");
    assert(ui.notice.operation.code == RESULT_OK && ui.notice.kind == NOTICE_SUCCESS);
    char *created = core_path_join(root, "created.txt"); FileInfo info;
    ok(core_info(created, &info)); file_info_free(&info);
    assert(create_named_entry(&ui, true, "created-dir", warning, sizeof warning));
    preserved(&ui, files, len, "Directory created");
    assert(transfer_path(&ui, false, created, root, "copy.txt", warning, sizeof warning));
    preserved(&ui, files, len, "Copied");
    cancel_operation = true;
    assert(!transfer_path(&ui, false, created, root, "cancelled-copy", warning, sizeof warning));
    preserved(&ui, files, len, "Copy cancelled");
    assert(strstr(ui.status, "incomplete files/directories kept") && strstr(ui.status, "completed: 0"));
    assert(ui.notice.operation.code == RESULT_CANCELLED && ui.notice.operation.partial);
    assert(ui.notice.kind == NOTICE_CANCELLED && ui.notice.operation.copied_bytes == 0);
    char *cancelled = core_path_join(root, "cancelled-copy");
    ok(core_info(cancelled, &info)); file_info_free(&info); free(cancelled);
    cancel_operation = false;
    assert(transfer_path(&ui, true, created, root, "moved.txt", warning, sizeof warning));
    preserved(&ui, files, len, "Moved");
    assert(core_info(created, &info).code == RESULT_NOT_FOUND);
    fail_refresh = false; ok(load_dir(&ui, "moved.txt"));
    assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name, "moved.txt"));
    assert(ui_panel(&ui)->app.sort.key == SORT_SIZE && ui_panel(&ui)->app.sort.descending);
    for (size_t i = 1; i < ui_panel(&ui)->app.files.len; i++)
        assert(main_file_compare(&ui_panel(&ui)->app.files.entries[i - 1], &ui_panel(&ui)->app.files.entries[i], ui_panel(&ui)->app.sort) <= 0);
    files = ui_panel(&ui)->app.files.entries; len = ui_panel(&ui)->app.files.len; fail_refresh = true;
    delete_entry(&ui); preserved(&ui, files, len, "Deleted");
    char *moved = core_path_join(root, "moved.txt"); assert(core_info(moved, &info).code == RESULT_NOT_FOUND);
    /* Failed deletion and failed refresh must both survive in the message. */
    delete_entry(&ui); assert(strstr(ui.status, "Delete: No changes") && strstr(ui.status, "list refresh failed"));
    assert(!strstr(ui.status, "Deleted"));
    assert(ui.notice.operation.code != RESULT_OK && ui.notice.refresh.code == RESULT_ACCESS);
    Result retained = ui.notice.operation;
    cancel_confirmation=true; delete_entry(&ui); cancel_confirmation=false;
    assert(!memcmp(&retained,&ui.notice.operation,sizeof retained) && ui.notice.visible);
    message(&ui, "Focus moved");
    assert(!memcmp(&retained, &ui.notice.operation, sizeof retained) && ui.notice.visible);
    notice_dismiss(&ui);
    assert(ui.notice.present && !ui.notice.visible);
    assert(!open_search_result(&ui, moved)); assert(strstr(ui.status, "Cannot open search result"));
    assert(!strstr(ui.status, "Opened search result") && ui_panel(&ui)->app.files.entries == files);
    fail_refresh=false; ok(load_dir(&ui,NULL));
    assert(create_named_entry(&ui,false,"raw\xff\n",warning,sizeof warning));
    char *raw=core_path_join(root,"raw\xff\n");
    files=ui_panel(&ui)->app.files.entries; size_t old_selected=ui_panel(&ui)->selected;
    assert(rename_named_entry(&ui,raw,"raw\xff\n",warning,sizeof warning));
    assert(ui_panel(&ui)->app.files.entries==files && ui_panel(&ui)->selected==old_selected); /* no refresh */
    assert(!rename_named_entry(&ui,raw,"copy.txt",warning,sizeof warning));
    assert(strstr(warning,"exists") && ui_panel(&ui)->app.files.entries==files);
    assert(rename_named_entry(&ui,raw,"renamed\xff\n",warning,sizeof warning));
    assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"renamed\xff\n"));
    free(raw); raw=core_path_join(root,"renamed\xff\n");
    files=ui_panel(&ui)->app.files.entries; len=ui_panel(&ui)->app.files.len; fail_refresh=true;
    assert(rename_named_entry(&ui,raw,"after-refresh-failure",warning,sizeof warning));
    preserved(&ui,files,len,"Renamed"); free(raw);
    fail_refresh=false;
    assert(!transfer_path(&ui,false,"/missing-source",root,"copy.txt",warning,sizeof warning));
    assert(ui.notice.operation.code==RESULT_EXISTS && ui.notice.refresh_attempted && ui.notice.refresh.code==RESULT_OK);
    assert(ui.notice.kind==NOTICE_ERROR);
    notice_clear(&ui); free(moved); free(created); preview_reset(&ui); app_free(&ui_panel(&ui)->app);
    puts("PASS: refresh errors preserve UI state; create/copy/move/delete success remains distinct from refresh failure");
}
