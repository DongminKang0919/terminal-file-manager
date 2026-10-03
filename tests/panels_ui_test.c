#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
/* Track explicit application allocations without allocating in the tracker. */
static void *owned[32768];
static size_t live, calls, fail_at;
void *__real_malloc(size_t); void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t); void __real_free(void *);
static size_t slot(void *p) {
    for(size_t i=0;i<32768;i++) if(owned[i]==p) return i;
    return 32768;
}
static void keep(void *p) {
    if(!p) return;
    size_t i=slot(NULL); assert(i<32768); owned[i]=p; live++;
}
static bool fail(void) { return ++calls==fail_at; }
void *__wrap_malloc(size_t n) { if(fail()) return NULL; void *p=__real_malloc(n); keep(p); return p; }
void *__wrap_calloc(size_t n,size_t s) { if(fail()) return NULL; void *p=__real_calloc(n,s); keep(p); return p; }
void __wrap_free(void *p) {
    if(p) { size_t i=slot(p); if(i<32768) { owned[i]=NULL; live--; } }
    __real_free(p);
}
void *__wrap_realloc(void *p,size_t n) {
    if(fail()) return NULL;
    size_t i=p?slot(p):32768;
    void *q=__real_realloc(p,n);
    if(q || !n) { if(i<32768) { owned[i]=NULL; live--; } keep(q); }
    return q;
}
static size_t fds(void) {
    DIR *d=opendir("/proc/self/fd"); assert(d); size_t n=0; struct dirent *e;
    while((e=readdir(d))) if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")) n++;
    closedir(d); return n;
}

static UiContext *current;
static size_t directories,sorts,reads,refreshes[2],created,destroyed;
static unsigned denied_refresh;
static bool denied_init;
static const char *find_text;
static size_t find_at;
static const char *form_keys; static size_t form_at;
static bool form_review;
static bool inspect_results; static unsigned result_seen, result_at;
Result __real_app_init(AppState *,const char *);
Result __wrap_app_init(AppState *app,const char *p) {
    return denied_init ? result_make(RESULT_ACCESS,"Injected second panel initialization failure") : __real_app_init(app,p);
}
Result __real_app_refresh(AppState *);
Result __wrap_app_refresh(AppState *app) {
    unsigned index=app==&current->panels[1].app?1:0; refreshes[index]++;
    return (denied_refresh&(1u<<index)) ? result_make(RESULT_ACCESS,index?"Right denied":"Left denied") : __real_app_refresh(app);
}
Result __real_platform_directory_open(const char *,PlatformDirectory **);
Result __wrap_platform_directory_open(const char *p,PlatformDirectory **out) { directories++; return __real_platform_directory_open(p,out); }
Result __real_platform_reader_line(PlatformReader *,char *,size_t,bool *);
Result __wrap_platform_reader_line(PlatformReader *r,char *line,size_t n,bool *end) { reads++; return __real_platform_reader_line(r,line,n,end); }
void __real_qsort(void *,size_t,size_t,int (*)(const void *,const void *));
void __wrap_qsort(void *p,size_t n,size_t size,int (*cmp)(const void *,const void *)) { sorts++; __real_qsort(p,n,size,cmp); }
WINDOW *__real_newwin(int,int,int,int);
WINDOW *__wrap_newwin(int h,int w,int y,int x) { WINDOW *win=__real_newwin(h,w,y,x); if(win) created++; return win; }
int __real_delwin(WINDOW *);
int __wrap_delwin(WINDOW *win) { destroyed++; return __real_delwin(win); }
int __wrap_input_wide(WINDOW *win,wint_t *key) {
    if(wgetdelay(win)==0) {
        unsigned active=current->active;
        assert(!ui_activate_panel(current,active^1u) && current->active==active);
        return ERR;
    }
    if(form_keys) {
        unsigned active=current->active;
        assert(!ui_activate_panel(current,active^1u));
        assert(form_keys[form_at]); *key=(unsigned char)form_keys[form_at++]; return OK;
    }
    assert(find_text); *key=(unsigned char)find_text[find_at++]; return OK;
}
int __wrap_input_key(WINDOW *win) {
    if(form_review) { static bool tab; tab=!tab; return tab?'\t':'\n'; }
    if(inspect_results) {
        for(int y=1;y<getmaxy(win)-3;y++) {
            char text[160];mvwinnstr(win,y,2,text,sizeof text-1);
            if(strstr(text,"Left list refresh")) result_seen|=1;
            if(strstr(text,"Right list refresh")) result_seen|=2;
        }
        return result_at++<100?KEY_DOWN:27;
    }
    return 27;
}
bool __wrap_confirm(UiContext *ui,const char *name,bool dir) { (void)ui;(void)name;(void)dir;return true; }
static void ok(Result r) { if(r.code) fprintf(stderr,"%s\n",r.detail); assert(r.code==RESULT_OK); }
static void select_name(UiContext *ui,const char *name) {
    for(size_t i=0;i<ui_panel(ui)->app.files.len;i++) if(!strcmp(ui_panel(ui)->app.files.entries[i].name,name)) { ui_panel(ui)->selected=i; return; }
    assert(0);
}
static bool present(UiFilePanel *p,const char *name) {
    for(size_t i=0;i<p->app.files.len;i++) if(!strcmp(p->app.files.entries[i].name,name)) return true;
    return false;
}
static bool stop_bytes(const BatchProgress *p,void *unused) { (void)unused; return !p->progress.copied_bytes; }
static void reset_refresh(void) { refreshes[0]=refreshes[1]=0; }
static void both_refreshed(UiContext *ui) {
    assert(refreshes[0]==1 && refreshes[1]==1);
    assert(ui->notice.refresh_attempted&&ui->notice.peer_refresh_attempted);
}
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));
    char root[]="/tmp/tfile-panels-XXXXXX"; assert(mkdtemp(root));
    char a[256],b[256],target[300],raw[300];
    snprintf(a,sizeof a,"%s/A",root);snprintf(b,sizeof b,"%s/B",root);
    assert(!mkdir(a,0700)&&!mkdir(b,0700));
    for(int i=0;i<32;i++) { char name[16];snprintf(name,sizeof name,"f%02d",i);ok(core_create(a,name,false)); }
    ok(core_create(a,".hidden",false));ok(core_create(b,"raw한글\xff\n",false));
    snprintf(raw,sizeof raw,"%s/raw한글\xff\n",b);
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in);assert(screen);init_theme();
    size_t initial_fds=fds(),baseline=live;
    for(int size=0;size<3;size++) {
        resizeterm(size==0?9:24,size==0?50:size==1?80:160);
        UiContext ui={0};current=&ui;directories=sorts=reads=0;
        ok(ui_init(&ui,a));assert(directories==2&&sorts==1);
        select_name(&ui,"f20");draw(&ui);assert(ui.preview_session);
        FileInfo *left_list=ui.panels[0].app.files.entries;size_t left_selected=ui.panels[0].selected,left_top=ui.panels[0].top;
        notice_record(&ui,"Previous result",result_make(RESULT_OK,NULL),NULL,NULL);
        denied_init=true;assert(ui_set_mode(&ui,UI_LIST_LIST).code==RESULT_ACCESS);denied_init=false;
        draw_cached(&ui);char status_line[512];mvinnstr(LINES-2,0,status_line,sizeof status_line-1);
        assert(strstr(status_line,"Injected second")&&ui.notice.present&&ui.notice.visible&&ui.notice.operation.code==RESULT_OK);
        assert(ui.mode==UI_LIST_PREVIEW&&!ui.second_initialized&&ui.preview_session&&ui.panels[0].app.files.entries==left_list&&ui.panels[0].selected==left_selected&&ui.panels[0].top==left_top);
        size_t d=directories,s=sorts;ok(ui_set_mode(&ui,UI_LIST_LIST));assert(directories==d+2&&sorts==s+1);
        assert(!ui.preview_session&&!ui.preview_page.lines&&ui.active==0);
        assert(ui.panels[0].selected==left_selected&&ui.panels[0].top==left_top);
        assert(ui.panels[0].app.files.entries!=ui.panels[1].app.files.entries&&ui.panels[0].app.directory!=ui.panels[1].app.directory&&ui.panels[0].app.history!=ui.panels[1].app.history);
        for(size_t i=0;i<ui.panels[0].app.files.len;i++) assert(ui.panels[0].app.files.entries[i].name!=ui.panels[1].app.files.entries[i].name);
        ok(app_mark_toggle(&ui.panels[0].app,"f00"));ok(app_mark_toggle(&ui.panels[1].app,"f01"));
        d=directories;s=sorts;size_t r=reads;
        for(int i=0;i<100;i++) { assert(panel_key(&ui,i%2?'\t':KEY_BTAB,LINES));draw(&ui); }
        assert(directories==d&&sorts==s&&reads==r&&ui.active==0);
        WINDOW *popup=dialog_open(&ui,"Modal isolation",7,52);assert(popup);
        assert(!ui_activate_panel(&ui,1)&&ui.active==0);dialog_close(&ui,popup);assert(!ui.modal_depth&&created==destroyed);
        assert(ui_activate_panel(&ui,1));assert(navigate(&ui,b,NULL));assert(!strcmp(ui.panels[0].app.directory,a)&&ui.panels[0].app.files.entries==left_list);
        history_dir(&ui,false);assert(!strcmp(ui.panels[1].app.directory,a));history_dir(&ui,true);assert(!strcmp(ui.panels[1].app.directory,b));
        change_sort(&ui,(SortSettings){SORT_KIND,true});assert(ui.panels[0].app.sort.key==SORT_NAME&&!ui.panels[0].app.sort.descending);
        ui.panels[1].app.show_hidden=false;ok(load_dir(&ui,NULL));assert(ui.panels[0].app.show_hidden);
        assert(navigate(&ui,a,NULL));find_text="f17\n";find_at=0;quick_find(&ui);find_text=NULL;
        assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"f17"));assert(ui.panels[0].selected==left_selected);
        assert(open_search_result(&ui,raw));assert(!strcmp(ui.panels[1].app.directory,b)&&!strcmp(ui.panels[0].app.directory,a));
        size_t history=ui.panels[0].app.history_len;
        ok(ui_set_mode(&ui,UI_LIST_ONLY));assert(ui.active==1&&!ui.panels[0].app.marks_len&&ui.panels[0].app.history_len==history);
        denied_refresh=1;left_list=ui.panels[0].app.files.entries;
        assert(ui_set_mode(&ui,UI_LIST_LIST).code==RESULT_ACCESS);
        assert(ui.mode==UI_LIST_LIST&&ui.panels[0].stale&&ui.panels[0].app.files.entries==left_list&&strstr(ui.status,"stale"));
        assert(ui_activate_panel(&ui,0));char warning[512];Result retained=ui.notice.operation;d=directories;
        assert(!create_named_entry(&ui,false,"blocked",warning,sizeof warning));assert(strstr(warning,"Stale")&&directories==d&&ui.notice.operation.code==retained.code);
        size_t windows_before=created;
        create_entry(&ui,false);rename_entry(&ui);transfer_entry(&ui,false);transfer_entry(&ui,true);delete_entry(&ui);batch_entry(&ui,BATCH_COPY);
        assert(created==windows_before&&directories==d&&ui.notice.operation.code==retained.code);
        denied_refresh=0;ok(load_dir(&ui,NULL));assert(!ui.panels[0].stale&&!present(&ui.panels[0],"blocked"));
        ok(app_mark_toggle(&ui.panels[0].app,"f00"));ok(app_mark_toggle(&ui.panels[1].app,"raw한글\xff\n"));
        size_t marks0=ui.panels[0].app.marks_len,marks1=ui.panels[1].app.marks_len;
        d=directories;s=sorts;r=reads;resizeterm(9,50);draw(&ui);
        UiScreenLayout layout=ui_screen_layout(&ui,50,9);assert(layout.visible[0]&&!layout.visible[1]&&layout.width[0]==50);
        assert(ui_panel_at(&layout,25,4)==0&&ui_panel_at(&layout,0,4)==-1);
        assert(panel_key(&ui,'\t',9));draw(&ui);layout=ui_screen_layout(&ui,50,9);
        assert(!layout.visible[0]&&layout.visible[1]&&ui_panel_at(&layout,25,4)==1);
        resizeterm(24,160);draw(&ui);layout=ui_screen_layout(&ui,160,24);
        assert(layout.visible[0]&&layout.visible[1]&&ui_panel_at(&layout,1,5)==0&&ui_panel_at(&layout,81,5)==1&&ui_panel_at(&layout,80,5)==-1);
        assert(ui.panels[0].app.marks_len==marks0&&ui.panels[1].app.marks_len==marks1&&directories==d&&sorts==s&&reads==r);
        select_name(&ui,"raw한글\xff\n");enter_item(&ui);assert(ui.mode==UI_LIST_PREVIEW&&ui.active==1&&ui.focus==UI_FOCUS_PREVIEW&&!ui.panels[0].app.marks_len);
        assert(panel_key(&ui,27,24)&&ui.focus==UI_FOCUS_FILES);ok(ui_set_mode(&ui,UI_LIST_LIST));
        assert(navigate(&ui,a,NULL));assert(ui_activate_panel(&ui,0));
        app_marks_clear(&ui.panels[0].app);app_marks_clear(&ui.panels[1].app);
        ok(app_mark_toggle(&ui.panels[0].app,"f00"));ok(app_mark_toggle(&ui.panels[0].app,"f01"));ok(app_mark_toggle(&ui.panels[1].app,"f02"));
        BatchJob job;ok(batch_prepare(&ui_panel(&ui)->app,ui_panel(&ui)->selected,BATCH_COPY,&job));assert(job.len==2&&!strcmp(job.targets[0].name,"f00")&&!strcmp(job.targets[1].name,"f01"));ok(batch_destination(&job,b));
        reset_refresh();run_batch_operation(&ui,&job);assert(job.result.code==RESULT_OK);batch_finish(&ui,&job);both_refreshed(&ui);
        assert(!ui.panels[0].app.marks_len&&app_marked(&ui.panels[1].app,"f02")&&ui.notice.source_panel==0);
        ok(app_mark_toggle(&ui.panels[0].app,"f00"));ok(app_mark_toggle(&ui.panels[0].app,"f01"));
        ok(batch_prepare(&ui_panel(&ui)->app,0,BATCH_COPY,&job));ok(batch_destination(&job,b));reset_refresh();run_batch_operation(&ui,&job);batch_finish(&ui,&job);both_refreshed(&ui);
        assert(ui.notice.operation.code==RESULT_EXISTS&&ui.panels[0].app.marks_len==2);
        snprintf(target,sizeof target,"%s/f00",b);ok(core_delete(target));
        ok(batch_prepare(&ui_panel(&ui)->app,0,BATCH_COPY,&job));ok(batch_destination(&job,b));reset_refresh();run_batch_operation(&ui,&job);batch_finish(&ui,&job);both_refreshed(&ui);
        assert(ui.notice.operation.partial&&ui.notice.batch.succeeded==1&&ui.panels[0].app.marks_len==1);
        app_marks_clear(&ui.panels[0].app);ok(app_mark_toggle(&ui.panels[0].app,"f04"));
        snprintf(target,sizeof target,"%s/f04",a);FILE *f=fopen(target,"w");assert(f);fputs("cancel bytes",f);fclose(f);
        ok(batch_prepare(&ui_panel(&ui)->app,0,BATCH_COPY,&job));ok(batch_destination(&job,b));reset_refresh();batch_execute(&job,stop_bytes,NULL);batch_finish(&ui,&job);both_refreshed(&ui);
        assert(ui.notice.operation.code==RESULT_CANCELLED&&ui.notice.operation.partial&&ui.panels[0].app.marks_len==1&&app_marked(&ui.panels[1].app,"f02"));
        /* Both panels in the source directory reconcile a moved/deleted mark. */
        ok(app_mark_toggle(&ui.panels[1].app,"f03"));snprintf(target,sizeof target,"%s/f03",a);reset_refresh();
        assert(transfer_path(&ui,true,target,b,"moved",warning,sizeof warning));both_refreshed(&ui);
        assert(!present(&ui.panels[0],"f03")&&!present(&ui.panels[1],"f03")&&!app_marked(&ui.panels[1].app,"f03"));
        select_name(&ui,"f05");app_marks_clear(&ui.panels[0].app);reset_refresh();delete_entry(&ui);both_refreshed(&ui);assert(!present(&ui.panels[0],"f05")&&!present(&ui.panels[1],"f05"));
        for(unsigned mask=1;mask<=3;mask++) {
            denied_refresh=mask;reset_refresh();char name[24];snprintf(name,sizeof name,"created-%d-%u",size,mask);
            FileInfo *old0=ui.panels[0].app.files.entries,*old1=ui.panels[1].app.files.entries;
            assert(create_named_entry(&ui,false,name,warning,sizeof warning));both_refreshed(&ui);
            assert(ui.notice.operation.code==RESULT_OK&&ui.notice.refresh.code==((mask&1)?RESULT_ACCESS:RESULT_OK)&&ui.notice.peer_refresh.code==((mask&2)?RESULT_ACCESS:RESULT_OK));
            assert(ui.panels[0].stale==((mask&1)!=0)&&ui.panels[1].stale==((mask&2)!=0));
            if(mask&1) assert(ui.panels[0].app.files.entries==old0);
            if(mask&2) assert(ui.panels[1].app.files.entries==old1);
            denied_refresh=0;ok(load_dir(&ui,NULL));ok(ui_refresh_panel(&ui,1,NULL,true));
        }
        assert(ui_activate_panel(&ui,1));assert(navigate(&ui,b,NULL));reset_refresh();
        assert(create_named_entry(&ui,false,"peer-other-directory",warning,sizeof warning));both_refreshed(&ui);assert(ui.notice.source_panel==1);
        assert(present(&ui.panels[1],"peer-other-directory")&&!present(&ui.panels[0],"peer-other-directory"));
        select_name(&ui,"raw한글\xff\n");ok(app_mark_toggle(&ui.panels[1].app,"raw한글\xff\n"));
        assert(ui_activate_panel(&ui,0));snprintf(target,sizeof target,"%s/f06",a);reset_refresh();
        assert(transfer_path(&ui,false,target,b,"copied-visible",warning,sizeof warning));both_refreshed(&ui);
        assert(present(&ui.panels[0],"f06")&&present(&ui.panels[1],"copied-visible"));
        snprintf(target,sizeof target,"%s/f07",a);ok(app_mark_toggle(&ui.panels[0].app,"f07"));reset_refresh();
        assert(transfer_path(&ui,true,target,b,"moved-visible",warning,sizeof warning));both_refreshed(&ui);
        assert(!present(&ui.panels[0],"f07")&&present(&ui.panels[1],"moved-visible")&&!app_marked(&ui.panels[0].app,"f07"));
        select_name(&ui,"f08");reset_refresh();delete_entry(&ui);both_refreshed(&ui);
        assert(!present(&ui.panels[0],"f08")&&present(&ui.panels[1],"raw한글\xff\n")&&app_marked(&ui.panels[1].app,"raw한글\xff\n"));
        assert(!strcmp(ui.panels[1].app.files.entries[ui.panels[1].selected].name,"raw한글\xff\n"));
        assert(ui_activate_panel(&ui,1)&&ui.notice.source_panel==0);
        unsigned active=ui.active;UiFocus focus=ui.focus;d=directories;s=sorts;r=reads;inspect_results=true;result_seen=result_at=0;show_result(&ui);inspect_results=false;assert(result_seen==3);
        assert(ui.active==active&&ui.focus==focus&&directories==d&&sorts==s&&reads==r&&!ui.modal_depth);
        assert(ui_activate_panel(&ui,1));
        for(int cycle=0;cycle<30;cycle++) { ok(ui_set_mode(&ui,cycle%2?UI_LIST_PREVIEW:UI_LIST_ONLY));ok(ui_set_mode(&ui,UI_LIST_LIST));WINDOW *w=dialog_open(&ui,"Return to active panel",7,52);assert(w);dialog_close(&ui,w);assert(ui.active==1&&!ui.modal_depth); }
        ui_free(&ui);assert(live==baseline&&fds()==initial_fds&&created==destroyed);
        ok(core_delete(b));assert(!mkdir(b,0700));ok(core_create(b,"raw한글\xff\n",false));
        /* Restore fixture items removed by actual move/delete. */
        ok(core_create(a,"f03",false));ok(core_create(a,"f05",false));ok(core_create(a,"f07",false));ok(core_create(a,"f08",false));
    }
    /* Real forms use frozen opposite defaults in both directions, including
       the hidden peer at 50x9; only the source marks are consumed. */
    for(int size=0;size<3;size++) for(unsigned side=0;side<2;side++)
    for(int move=0;move<2;move++) for(int batch=0;batch<2;batch++) {
        resizeterm(size==0?9:24,size==0?50:size==1?80:160);
        char l[300],r[300]; snprintf(l,sizeof l,"%s/form-left",root);snprintf(r,sizeof r,"%s/form-right",root);
        assert(!mkdir(l,0700)&&!mkdir(r,0700));
        const char *src=side?r:l,*dst=side?l:r;
        ok(core_create(src,"first",false));if(batch) ok(core_create(src,"second",false));
        ok(core_create(dst,"peer-mark",false));
        UiContext ui; current=&ui;ok(ui_init(&ui,l));ok(ui_set_mode(&ui,UI_LIST_LIST));
        assert(ui_activate_panel(&ui,1));assert(navigate(&ui,r,NULL));assert(ui_activate_panel(&ui,side));
        select_name(&ui,"first");if(batch) ok(app_mark_all(&ui_panel(&ui)->app));
        else if(move) ok(app_mark_toggle(&ui_panel(&ui)->app,"first"));
        ok(app_mark_toggle(&ui.panels[side^1u].app,"peer-mark"));
        UiTransferContext frozen;assert(ui_transfer_context(&ui,&frozen));
        assert(frozen.source_panel==side&&frozen.dual&&!strcmp(frozen.base,src)&&!strcmp(frozen.destination,dst));
        assert(frozen.destination!=ui.panels[side^1u].app.directory);ui_transfer_context_free(&frozen);
        size_t mem=live,fd=fds();
        for(size_t fault=1;fault<=2;fault++) {
            calls=0;fail_at=fault;assert(!ui_transfer_context(&ui,&frozen));fail_at=0;
            assert(live==mem&&fds()==fd);
        }
        for(int repeat=0;repeat<20;repeat++) {
            form_keys="\033";form_at=0;transfer_entry(&ui,move);
            assert(form_at==1&&live==mem&&fds()==fd&&ui.active==side&&!ui.modal_depth);
        }
        /* Old refresh failure is advisory: currently valid destination runs. */
        ui.panels[side^1u].stale=true;
        reset_refresh();form_keys=batch?"\n":"\n\n\n";form_at=0;form_review=batch;
        transfer_entry(&ui,move);form_keys=NULL;form_review=false;
        assert(ui.notice.operation.code==RESULT_OK&&ui.active==side);both_refreshed(&ui);
        assert(!ui_panel(&ui)->app.marks_len&&app_marked(&ui.panels[side^1u].app,"peer-mark"));
        assert(!strcmp(ui.panels[side^1u].app.directory,dst));
        for(int item=0;item<=batch;item++) {
            char *from=core_path_join(src,item?"second":"first"),*to=core_path_join(dst,item?"second":"first");
            assert(!access(to,F_OK)&&(move?access(from,F_OK)!=0:access(from,F_OK)==0));free(from);free(to);
        }
        ui_free(&ui);assert(live==baseline&&fds()==initial_fds&&created==destroyed);
        ok(core_delete(l));ok(core_delete(r));
    }
    /* Every second-panel allocation failure either commits a complete panel or
       preserves the first panel and releases all partial candidate ownership. */
    for(size_t fault=1;fault<=100;fault++) {
        UiContext ui={0};current=&ui;ok(ui_init(&ui,a));FileInfo *original=ui.panels[0].app.files.entries;
        calls=0;fail_at=fault;Result result=ui_set_mode(&ui,UI_LIST_LIST);fail_at=0;
        if(result.code!=RESULT_OK) assert(ui.mode==UI_LIST_PREVIEW&&!ui.second_initialized&&ui.panels[0].app.files.entries==original);
        ui_free(&ui);assert(live==baseline&&fds()==initial_fds);
        ok(ui_init(&ui,a));ok(ui_set_mode(&ui,UI_LIST_LIST));ok(ui_set_mode(&ui,UI_LIST_ONLY));
        original=ui.panels[1].app.files.entries;
        calls=0;fail_at=fault;result=ui_set_mode(&ui,UI_LIST_LIST);fail_at=0;
        if(result.code!=RESULT_OK) assert(ui.mode==UI_LIST_LIST&&ui.panels[1].stale&&ui.panels[1].app.files.entries==original);
        ui_free(&ui);assert(live==baseline&&fds()==initial_fds);
        calls=0;fail_at=fault;result=ui_init(&ui,a);fail_at=0;
        ui_free(&ui);assert(live==baseline&&fds()==initial_fds);
    }
    endwin();delscreen(screen);fclose(out);fclose(in);ok(core_delete(root));assert(!live);
    puts("PASS: independent panels/modes/history/find/search/marks; 2 initial directory opens + 1 sort per panel, zero focus I/O; narrow geometry/marks; modal isolation; one refresh per panel, failures/stale write guard; batch outcomes; 100 initial/second/reopen allocation failure positions; owned memory/windows/FD cleanup");
}
