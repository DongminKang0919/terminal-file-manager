#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
static const char *denied;
Result __real_platform_directory_open(const char *,PlatformDirectory **);
Result __wrap_platform_directory_open(const char *path,PlatformDirectory **out) {
    if(denied && !strcmp(path,denied)) { *out=NULL; return result_make(RESULT_ACCESS,"Injected denied history"); }
    return __real_platform_directory_open(path,out);
}
static void ok(Result r) { assert(r.code==RESULT_OK); }
int main(void) {
    char root[]="/tmp/tfile-history-XXXXXX"; assert(mkdtemp(root));
    ok(core_create(root,"A",true)); ok(core_create(root,"B",true));
    char *a=core_path_join(root,"A"), *b=core_path_join(root,"B"); assert(a&&b);
    for(int i=0;i<40;i++) { char name[32]; snprintf(name,sizeof name,"f%02d",i); ok(core_create(a,name,false)); }
    ok(core_create(a,".hidden",false)); ok(core_create(a,"raw\xff\n",false));
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen); resizeterm(24,80);
    UiContext ui={0}; ok(ui_init(&ui,a)); ok(load_dir(&ui,"f30")); ui_panel(&ui)->top=20;
    size_t selected=ui_panel(&ui)->selected; assert(navigate(&ui,b,NULL));
    history_dir(&ui,false); assert(ui_panel(&ui)->selected==selected && ui_panel(&ui)->top==20);
    assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"f30") && !ui.preview_session);
    assert(navigate(&ui,b,NULL)); change_sort(&ui,(SortSettings){SORT_NAME,true});
    history_dir(&ui,false); assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"f30"));
    assert(ui_panel(&ui)->top<=ui_panel(&ui)->selected && ui_panel(&ui)->selected<ui_panel(&ui)->top+16);
    selected=ui_panel(&ui)->selected; assert(navigate(&ui,b,NULL));
    char *removed=core_path_join(a,"f30"); ok(core_delete(removed)); free(removed);
    history_dir(&ui,false); assert(ui_panel(&ui)->selected==selected && strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"f30"));
    ok(load_dir(&ui,"raw\xff\n")); assert(navigate(&ui,b,NULL)); history_dir(&ui,false);
    assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"raw\xff\n"));
    change_sort(&ui,(SortSettings){SORT_NAME,false}); ok(load_dir(&ui,".hidden"));
    assert(navigate(&ui,b,NULL)); ui_panel(&ui)->app.show_hidden=false; history_dir(&ui,false);
    assert(ui_panel(&ui)->selected==0 && !ui_panel(&ui)->app.files.entries[0].hidden);
    assert(navigate(&ui,b,NULL)); FileInfo *items=ui_panel(&ui)->app.files.entries;
    size_t at=ui_panel(&ui)->app.history_at,top=ui_panel(&ui)->top; selected=ui_panel(&ui)->selected;
    denied=a; history_dir(&ui,false); denied=NULL;
    assert(ui_panel(&ui)->app.history_at==at && ui_panel(&ui)->app.files.entries==items && ui_panel(&ui)->top==top && ui_panel(&ui)->selected==selected);
    assert(!strcmp(ui_panel(&ui)->app.directory,b) && strstr(ui.status,"denied history"));
    parent_dir(&ui); assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"B"));
    history_dir(&ui,false); assert(navigate(&ui,a,NULL));
    assert(app_history(&ui_panel(&ui)->app,true).code==RESULT_NOT_FOUND);
    for(int i=0;i<140;i++) assert(navigate(&ui,i%2?a:b,NULL));
    assert(ui_panel(&ui)->app.history_len==128 && ui_panel(&ui)->app.history_at==127);
    preview_reset(&ui); app_free(&ui_panel(&ui)->app); endwin(); delscreen(screen); fclose(out); fclose(in);
    free(a); free(b); ok(core_delete(root));
    puts("PASS: history identity/raw bytes/scroll, changed sort, missing/hidden fallback, failed navigation transaction, Parent, forward trim and 128-entry ownership");
}
