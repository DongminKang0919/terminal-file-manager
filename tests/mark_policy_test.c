#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <unistd.h>
static void ok(Result r) {if(r.code)fprintf(stderr,"%s\n",r.detail);assert(!r.code);}
static void create(const char *d,const char *n) {ok(core_create(d,n,false));}
int main(void) {
    char root[]="/tmp/tfile-mark-policy-XXXXXX";assert(mkdtemp(root));create(root,"a cursor");create(root,"b marked");
    UiContext ui={0};ok(app_init(&ui.panels[0].app,root));ok(app_init(&ui.panels[1].app,root));
    assert(!strcmp(ui_operation_item(&ui)->name,"a cursor"));ok(app_mark_toggle(&ui.panels[0].app,"b marked"));
    assert(!strcmp(ui_operation_item(&ui)->name,"b marked")&&ui.panels[0].selected==0);mark_targets_message(&ui);assert(ui.mark_hint&&strstr(ui.status,"Targets: 1 marked"));
    // A stale/missing marked name never silently becomes the cursor item.
    app_marks_clear(&ui.panels[0].app);ok(app_mark_toggle(&ui.panels[0].app,"missing"));assert(!ui_operation_item(&ui));assert(strstr(ui.status,"no changes"));
    app_marks_clear(&ui.panels[0].app);ok(app_mark_toggle(&ui.panels[0].app,"a cursor"));ok(app_mark_toggle(&ui.panels[0].app,"b marked"));assert(!ui_operation_item(&ui));
    ok(app_set_filter(&ui.panels[0].app,FILTER_CONTAINS,"b marked"));assert(!ui.panels[0].app.marks_len);ok(app_mark_all(&ui.panels[0].app));assert(ui.panels[0].app.marks_len==1&&!strcmp(ui_operation_item(&ui)->name,"b marked"));
    ui.active=1;assert(!ui.panels[1].app.marks_len&&!strcmp(ui_operation_item(&ui)->name,"a cursor"));ok(app_mark_toggle(&ui.panels[1].app,"a cursor"));assert(ui.panels[0].app.marks_len==1);
    ui_free(&ui);ok(platform_remove(root));puts("PASS: single-mark target priority, absent/multiple mark refusal, filtered visible-only marks, independent panels and owned cleanup");
}
