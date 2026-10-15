#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
static pid_t parent;
static bool enforcing;
static struct { volatile unsigned hold, entered, reads, opens, finished; } *shared;
static void pause_tick(void) { struct timespec ts={0,1000000}; nanosleep(&ts,NULL); }
static void gate(unsigned stage) {
    if(enforcing) assert(getpid()!=parent);
    if(shared->hold==stage) { shared->entered=stage; while(shared->hold==stage) pause_tick(); }
}
Result __real_platform_reader_open(const char *,PlatformReader **);
Result __wrap_platform_reader_open(const char *p,PlatformReader **r) { gate(1); shared->opens++;return __real_platform_reader_open(p,r); }
Result __real_platform_reader_peek(PlatformReader *,unsigned char *,size_t,size_t *);
Result __wrap_platform_reader_peek(PlatformReader *r,unsigned char *b,size_t n,size_t *out) { gate(2);return __real_platform_reader_peek(r,b,n,out); }
Result __real_platform_reader_line(PlatformReader *,char *,size_t,bool *);
Result __wrap_platform_reader_line(PlatformReader *r,char *b,size_t n,bool *end) { gate(3);shared->reads++;return __real_platform_reader_line(r,b,n,end); }
Result __real_platform_reader_changed(PlatformReader *,const char *,bool *);
Result __wrap_platform_reader_changed(PlatformReader *r,const char *p,bool *c) { gate(4);return __real_platform_reader_changed(r,p,c); }
Result __real_platform_directory_empty(const char *,bool *);
Result __wrap_platform_directory_empty(const char *p,bool *e) { gate(5);return __real_platform_directory_empty(p,e); }
bool __real_platform_task_send(PlatformTaskWriter *,unsigned,const void *,size_t);
bool __wrap_platform_task_send(PlatformTaskWriter *writer,unsigned type,const void *data,size_t len) {
    bool ok=__real_platform_task_send(writer,type,data,len);
    if(ok && type==4 && !len)shared->finished++; /* Buffered DONE, not yet polled by UI. */
    return ok;
}
static void select_name(UiContext *ui,const char *name) {
    for(size_t i=0;i<ui_panel(ui)->app.files.len;i++) if(!strcmp(ui_panel(ui)->app.files.entries[i].name,name)) { ui_panel(ui)->selected=i; return; }
    assert(false);
}
static void prepare(UiContext *ui) {
    uint64_t start=core_monotonic_ms();
    do { preview_prepare(ui,17); pause_tick(); assert(core_monotonic_ms()-start<3000); }
    while(ui->preview_busy || ui->preview_queued || !ui->preview_ready);
    if(ui->preview_result.code!=RESULT_OK) fprintf(stderr,"preview error: %s\n",ui->preview_result.detail);
    assert(ui->preview_result.code==RESULT_OK);
}
static void entered(UiContext *ui,unsigned stage) {
    uint64_t start=core_monotonic_ms();
    do { preview_prepare(ui,17); pause_tick(); assert(core_monotonic_ms()-start<1000); } while(shared->entered!=stage);
}
static unsigned fds(void) {
    DIR *dir=opendir("/proc/self/fd"); assert(dir);unsigned n=0;while(readdir(dir))n++;closedir(dir);return n;
}
static bool cancel_load(size_t n,bool complete,bool waiting,void *context) {
    (void)n;(void)waiting;unsigned *calls=context;if(!complete)(*calls)++;return complete;
}
int main(void) {
    parent=getpid(); shared=mmap(NULL,sizeof *shared,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);
    char root[]="/tmp/tfile-preview-async-XXXXXX";assert(mkdtemp(root));
    char a[256],b[256],d[256];snprintf(a,sizeof a,"%s/a",root);snprintf(b,sizeof b,"%s/b",root);snprintf(d,sizeof d,"%s/dir",root);
    FILE *f=fopen(a,"w");assert(f);for(int i=0;i<2000;i++)fprintf(f,"old-%04d\n",i);fclose(f);
    f=fopen(b,"w");assert(f);fputs("new-selection\n",f);fclose(f);assert(!mkdir(d,0700));
    FILE *out=tmpfile(),*in=tmpfile();assert(out&&in);SCREEN *screen=newterm("xterm-256color",out,in);assert(screen);init_theme();resizeterm(24,100);
    UiContext ui;assert(ui_init(&ui,root).code==RESULT_OK);ui_enable_async_reads(&ui);enforcing=true;
    select_name(&ui,"a");prepare(&ui);unsigned opened=shared->opens,read=shared->reads;
    for(int i=0;i<100;i++)preview_prepare(&ui,17);
    assert(shared->opens==opened && shared->reads==read);
    ui.preview_offset=200;prepare(&ui);read=shared->reads;
    ui.preview_offset=190;prepare(&ui);assert(shared->reads==read && shared->opens==opened);
    /* Hold each I/O independently, then replace the selection before release. */
    unsigned fd_count=0;
    for(unsigned i=0;i<40;i++) {
        unsigned stage=i%5+1;preview_reset(&ui);select_name(&ui,stage==5?"dir":"a");
        shared->entered=0;shared->hold=stage;entered(&ui,stage);
        if(i==0) {
            assert(!ui.preview_loading);
            while(core_monotonic_ms()-ui.preview_started<110) { preview_prepare(&ui,17);pause_tick(); }
            preview_prepare(&ui,17);assert(ui.preview_loading);
        }
        uint64_t start=core_monotonic_ms();select_name(&ui,"b");preview_prepare(&ui,17);
        assert(core_monotonic_ms()-start<100);shared->hold=0;prepare(&ui);
        assert(ui.preview_page.len==1 && !strcmp(ui.preview_page.lines[0],"new-selection"));
        if(i==2)fd_count=fds();
        if(i>2)assert(fds()==fd_count);
    }
    /* A complete old result already buffered in IPC must not replace B. */
    preview_reset(&ui);
    uint64_t cleanup_deadline=core_monotonic_ms()+1000;
    while(!core_preview_available()) { pause_tick();assert(core_monotonic_ms()<cleanup_deadline); }
    select_name(&ui,"a");unsigned finished=shared->finished;
    preview_prepare(&ui,17);uint64_t buffered_deadline=core_monotonic_ms()+1000;
    while(shared->finished==finished) { pause_tick();assert(core_monotonic_ms()<buffered_deadline); }
    assert(ui.preview_busy && !ui.preview_ready);
    select_name(&ui,"b");prepare(&ui);assert(!strcmp(ui.preview_page.lines[0],"new-selection"));
    /* Browsing/search have an independent slot even while preview I/O hangs. */
    preview_reset(&ui);select_name(&ui,"a");shared->hold=2;shared->entered=0;entered(&ui,2);
    ReadJob *search=NULL;assert(core_read_search_start(root,"b",search_default_limits(),&search).code==RESULT_OK);
    bool done=false;uint64_t deadline=core_monotonic_ms()+1000;
    while(!done) { assert(core_read_poll(search,&done).code==RESULT_OK);pause_tick();assert(core_monotonic_ms()<deadline); }
    assert(core_read_snapshot(search)->matches.len==1);core_read_close(search);
    assert(app_refresh(&ui_panel(&ui)->app).code==RESULT_OK);
    assert(ui.preview_busy);shared->hold=0;preview_reset(&ui);
    /* A blocked distant page can be cancelled by selection or geometry. */
    preview_reset(&ui);select_name(&ui,"a");prepare(&ui);shared->hold=3;shared->entered=0;ui.preview_offset=1800;entered(&ui,3);
    ui.preview_more=ui.preview_scrollable=true;
    for(unsigned i=0;i<110;i++) { preview_scroll(&ui,true);preview(&ui,50,2,50,20);assert(ui.preview_more && ui.preview_scrollable); }
    assert(ui.preview_offset==1910);
    select_name(&ui,"b");preview_prepare(&ui,17);shared->hold=0;prepare(&ui);
    assert(!strcmp(ui.preview_page.lines[0],"new-selection"));
    preview_reset(&ui);select_name(&ui,"a");prepare(&ui);shared->hold=3;shared->entered=0;ui.preview_offset=1800;entered(&ui,3);
    resizeterm(10,52);preview_prepare(&ui,3);shared->hold=0;select_name(&ui,"b");prepare(&ui);
    assert(!strcmp(ui.preview_page.lines[0],"new-selection"));
    resizeterm(8,49);preview_prepare(&ui,0);assert(!ui.preview_job && !ui.preview_page.lines);
    resizeterm(24,100);prepare(&ui);
    /* A failed metadata refresh clears retained graphics, as the old path did. */
    ui.media_kind=PREVIEW_PNG;ui.media_data=text_copy("retained pixels");assert(ui.media_data);ui.media_len=strlen(ui.media_data);
    assert(!unlink(b));ui.preview_checked=0;uint64_t error_deadline=core_monotonic_ms()+1000;
    do { preview_prepare(&ui,17);pause_tick();assert(core_monotonic_ms()<error_deadline); } while(ui.preview_busy || ui.preview_queued);
    assert(ui.preview_result.code==RESULT_NOT_FOUND && ui.media_kind==PREVIEW_NOT_MEDIA && !ui.media_data);
    f=fopen(b,"w");assert(f);fputs("new-selection\n",f);assert(!fclose(f));
    preview_reset(&ui);select_name(&ui,"a");ui.focus=UI_FOCUS_FILES;enter_item(&ui);
    assert(ui.preview_focus_requested && ui.focus==UI_FOCUS_FILES);
    prepare(&ui);preview_update_actions(&ui,50,20);assert(ui.focus==UI_FOCUS_PREVIEW && !ui.preview_focus_requested);
    ui.focus=UI_FOCUS_FILES;
    WINDOW *win=dialog_open(&ui,"Modal",7,50);assert(win);dialog_close(&ui,win);assert(ui.preview_job);
    assert(ui_set_mode(&ui,UI_LIST_ONLY).code==RESULT_OK && !ui.preview_job);
    /* The first candidate receives the asynchronous callback, and cancellation
       leaves ownership, marks, cursor, scroll, and mode in the source panel. */
    assert(app_mark_toggle(&ui_panel(&ui)->app,"b").code==RESULT_OK);
    char moved[300];snprintf(moved,sizeof moved,"%s-moved",root);assert(!rename(root,moved));
    assert(ui_set_mode(&ui,UI_LIST_LIST).code==RESULT_NOT_FOUND);
    assert(ui.mode==UI_LIST_ONLY && !ui.second_initialized && ui.active==0 && ui_panel(&ui)->app.marks_len==1);
    assert(!rename(moved,root));
    unsigned calls=0;ui_panel(&ui)->app.load_progress=cancel_load;ui_panel(&ui)->app.load_context=&calls;
    Item *items=ui_panel(&ui)->app.files.entries;size_t cursor=ui_panel(&ui)->selected,top=ui_panel(&ui)->top;
    assert(ui_set_mode(&ui,UI_LIST_LIST).code==RESULT_CANCELLED && calls);
    assert(ui.mode==UI_LIST_ONLY && !ui.second_initialized && ui.active==0 && ui_panel(&ui)->app.files.entries==items);
    assert(ui_panel(&ui)->selected==cursor && ui_panel(&ui)->top==top && ui_panel(&ui)->app.marks_len==1);
    ui_enable_async_reads(&ui);assert(ui_set_mode(&ui,UI_LIST_LIST).code==RESULT_OK && ui.second_initialized);
    ui_free(&ui);assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);enforcing=false;endwin();delscreen(screen);fclose(out);fclose(in);
    assert(!unlink(a)&&!unlink(b)&&!rmdir(d)&&!rmdir(root));munmap(shared,sizeof *shared);
    puts("PASS: async-only I/O, persistent cache, 40 cancellations, stale selections, page/resize cancellation, modal/mode cleanup, FD bound, transactional first dual creation");
}
