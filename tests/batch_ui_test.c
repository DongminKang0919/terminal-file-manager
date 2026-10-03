#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

static size_t refreshes, dirs, reads, sorts, polls, draws;
static bool bad_refresh, cancelling;
static int mode,at;
static int script[UI_INPUT_CAP*2]; static size_t script_len,prepared,main_reads;
static const char *main_path;
static size_t windows_opened,windows_closed;
WINDOW *__real_newwin(int,int,int,int); int __real_delwin(WINDOW *);
WINDOW *__wrap_newwin(int h,int w,int y,int x) { WINDOW *win=__real_newwin(h,w,y,x); if(win) windows_opened++; return win; }
int __wrap_delwin(WINDOW *win) { windows_closed++; return __real_delwin(win); }

Result __real_batch_prepare(const AppState *,size_t,BatchAction,BatchJob *);
Result __wrap_batch_prepare(const AppState *a,size_t c,BatchAction k,BatchJob *j) { prepared++; return __real_batch_prepare(a,c,k,j); }
static int scripted(void) { assert((size_t)at<script_len); return script[at++]; }
static void push(int key) { assert(script_len<sizeof script/sizeof script[0]); script[script_len++]=key; }
static void replace_input(const char *text) {
    push(KEY_HOME); for(size_t i=0;i<UI_INPUT_CAP;i++) push(KEY_DC);
    for(size_t i=0;text[i];i++) push((unsigned char)text[i]);
}

static size_t allocations,fail_at;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n) { return ++allocations==fail_at?NULL:__real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { return ++allocations==fail_at?NULL:__real_calloc(n,s); }
void *__wrap_realloc(void *p,size_t n) { return ++allocations==fail_at?NULL:__real_realloc(p,n); }
Result __real_app_refresh(AppState *);
Result __wrap_app_refresh(AppState *app) { refreshes++;return bad_refresh?result_make(RESULT_ACCESS,"Separate refresh error"):__real_app_refresh(app); }
Result __real_platform_directory_open(const char *,PlatformDirectory **);
Result __wrap_platform_directory_open(const char *p,PlatformDirectory **d) { dirs++;if(main_path&&!strcmp(p,main_path)) main_reads++;return __real_platform_directory_open(p,d); }
Result __real_platform_reader_line(PlatformReader *,char *,size_t,bool *);
Result __wrap_platform_reader_line(PlatformReader *r,char *p,size_t n,bool *e) { reads++;return __real_platform_reader_line(r,p,n,e); }
void __real_qsort(void *,size_t,size_t,int (*)(const void *,const void *));
void __wrap_qsort(void *p,size_t n,size_t s,int (*cmp)(const void *,const void *)) { sorts++;__real_qsort(p,n,s,cmp); }
uint64_t __wrap_core_monotonic_ms(void) { return 1000; }
int __real_wrefresh(WINDOW *);
int __wrap_wrefresh(WINDOW *w) { if(wgetdelay(w)==0)draws++;return __real_wrefresh(w); }
static short pair(WINDOW *w,int y,int x) { cchar_t c;wchar_t t[CCHARW_MAX];attr_t a;short p;assert(mvwin_wch(w,y,x,&c)==OK);getcchar(&c,t,&a,&p,NULL);return p; }
int __wrap_input_key(WINDOW *w) {
    if(mode==5) return scripted();
    if(mode==1) { /* Scrolling leaves default Cancel selected. */
        assert(pair(w,getmaxy(w)-2,2)==UI_SELECTED);
        const int keys[]={KEY_NPAGE,KEY_END,KEY_HOME,KEY_DOWN,'\n'};assert(at<5);return keys[at++];
    }
    if(mode==2) { const int keys[]={'\t','\n'};assert(at<2);return keys[at++]; }
    if(mode==3) { if(at++<200)return KEY_DOWN;return 27; }
    if(mode==4) { return 27; }
    assert(0);return ERR;
}
int __wrap_input_wide(WINDOW *w,wint_t *key) {
    (void)w;polls++;
    if(mode==5) { int k=scripted(); *key=k; return k>=KEY_MIN?KEY_CODE_YES:OK; }
    if(cancelling&&polls==4) { *key=27;return OK; }
    return ERR;
}
static void ok(Result r) { assert(r.code==RESULT_OK); }
static char *path_join(const char *p,const char *n) { char *s=core_path_join(p,n);assert(s);return s; }
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));char root[]="/tmp/tfile-batch-ui-XXXXXX";assert(mkdtemp(root));
    char *src=path_join(root,"source"),*dst=path_join(root,"target");assert(!mkdir(src,0700)&&!mkdir(dst,0700));
    char *a=path_join(src,"a한글\n\xff"),*b=path_join(src,"b");FILE *f=fopen(a,"w");assert(f);for(int i=0;i<262144;i++)fputc('x',f);fclose(f);ok(core_create(src,"b",false));
    FILE *out=tmpfile(),*in=tmpfile();assert(out&&in);SCREEN *screen=newterm("xterm-256color",out,in);assert(screen);init_theme();
    for(int size=0;size<3;size++) {
        resizeterm(size==0?9:24,size==0?50:size==1?80:160);
        UiContext ui={0};ok(app_init(&ui.app,src));draw(&ui);
        size_t d=dirs,r=reads,s=sorts;ok(app_mark_toggle(&ui.app,ui.app.files.entries[0].name));
        draw_cached(&ui);assert(dirs==d&&reads==r&&sorts==s);assert((mvinch(ui_layout(COLS,LINES,true).list_y,2)&A_CHARTEXT)=='*');
        assert(panel_key(&ui,'\t',LINES));assert(panel_key(&ui,' ',LINES));assert(ui.app.marks_len==1);assert(panel_key(&ui,27,LINES));
        ok(app_mark_all(&ui.app));
        allocations=0;fail_at=1;size_t before_dirs=dirs;
        batch_entry(&ui,BATCH_COPY);fail_at=0;
        assert(ui.notice.operation.code==RESULT_NO_MEMORY&&ui.notice.kind==NOTICE_ERROR&&!ui.notice.refresh_attempted&&dirs==before_dirs&&ui.app.marks_len==2);
        /* Editing, review return, picker cancel and resize never refresh or
           recollect targets, and preserve the previous notice and all marks. */
        main_path=src;
        for(int flow=0;flow<4;flow++) {
            mode=5;at=0;script_len=0;prepared=main_reads=refreshes=0;
            if(flow==0) { replace_input("missing"); push('\n'); push(KEY_BACKSPACE); push('x'); push(27); }
            if(flow==1) { replace_input(dst); push('\n'); push(27); push(27); }
            if(flow==2) { push('\t'); push('\n'); push(27); push('\n'); push(KEY_RESIZE); }
            if(flow==3) { push('\n'); push(KEY_RESIZE); }
            BatchTarget *retained=ui.notice.batch.targets; Result previous=ui.notice.operation;
            batch_entry(&ui,BATCH_COPY);
            assert((size_t)at==script_len&&prepared==1&&!refreshes&&!ui.modal_depth&&ui.app.marks_len==2);
            assert(ui.notice.batch.targets==retained&&!memcmp(&previous,&ui.notice.operation,sizeof previous));
            assert(main_reads==(flow==2?4:flow==3?1:0));
            assert(windows_opened==windows_closed);
        }
        main_path=NULL;mode=0;
        BatchJob job;ok(batch_prepare(&ui.app,ui.selected,BATCH_DELETE,&job));
        mode=1;at=0;assert(!batch_review(&ui,&job));assert(at==5);assert(ui.app.marks_len==2);batch_cancel(&job);batch_finish(&ui,&job);assert(!ui.notice.refresh_attempted&&ui.notice.operation.code==RESULT_CANCELLED);
        ok(batch_prepare(&ui.app,ui.selected,BATCH_COPY,&job));ok(batch_destination(&job,dst));mode=2;at=0;assert(batch_review(&ui,&job));
        bad_refresh=true;refreshes=0;cancelling=false;polls=draws=0;run_batch_operation(&ui,&job);assert(job.result.code==RESULT_OK&&job.succeeded==2&&draws==1&&polls>=8);
        batch_finish(&ui,&job);assert(refreshes==1&&ui.notice.operation.code==RESULT_OK&&ui.notice.refresh.code==RESULT_ACCESS&&!ui.app.marks_len);
        /* Full success and refresh error are retained, and details cause no I/O. */
        /* Required single-target notice paths must fail before the file API. */
        for(size_t failure=1;failure<=3;failure++) {
            char warning[256];size_t previous_dirs=dirs;BatchTarget *retained=ui.notice.batch.targets;
            allocations=0;fail_at=failure;
            assert(!transfer_path(&ui,false,a,dst,"new-single",warning,sizeof warning));fail_at=0;
            assert(dirs==previous_dirs&&ui.notice.batch.targets==retained);
            char *absent=path_join(dst,"new-single");assert(access(absent,F_OK));free(absent);
        }
        ui.focus=UI_FOCUS_PREVIEW;size_t cursor=ui.selected,top=ui.top,offset=ui.preview_offset;PreviewSession *session=ui.preview_session;d=dirs;r=reads;s=sorts;
        mode=3;at=0;show_result(&ui);assert(dirs==d&&reads==r&&sorts==s&&cursor==ui.selected&&top==ui.top&&offset==ui.preview_offset&&session==ui.preview_session&&ui.focus==UI_FOCUS_PREVIEW);
        notice_dismiss(&ui);assert(!ui.notice.visible&&ui.notice.batch.len==2);mode=4;show_result(&ui);assert(ui.notice.batch.len==2);
        ok(app_mark_all(&ui.app));ok(batch_prepare(&ui.app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));refreshes=0;run_batch_operation(&ui,&job);assert(job.result.code==RESULT_EXISTS&&job.targets[0].status==BATCH_FAILED&&job.targets[1].status==BATCH_UNEXECUTED);batch_finish(&ui,&job);assert(refreshes==1&&ui.notice.operation.code==RESULT_EXISTS&&ui.notice.refresh.code==RESULT_ACCESS&&ui.app.marks_len==2);
        bad_refresh=false;ok(load_dir(&ui,NULL));assert(ui.app.marks_len==2);ok(core_delete(dst));assert(!mkdir(dst,0700));
        /* A middle collision drops only the successful source mark, even if refresh fails. */
        ok(core_create(dst,"b",false));ok(batch_prepare(&ui.app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));
        bad_refresh=true;cancelling=false;refreshes=0;run_batch_operation(&ui,&job);
        assert(job.targets[0].status==BATCH_SUCCESS&&job.targets[1].status==BATCH_FAILED&&job.result.partial);
        batch_finish(&ui,&job);assert(refreshes==1&&ui.app.marks_len==1&&app_marked(&ui.app,"b")&&!app_marked(&ui.app,ui.app.files.entries[0].name));
        assert(ui.notice.kind==NOTICE_WARNING&&ui.notice.refresh.code==RESULT_ACCESS);
        bad_refresh=false;ok(load_dir(&ui,NULL));assert(ui.app.marks_len==1);ok(app_mark_all(&ui.app));ok(core_delete(dst));assert(!mkdir(dst,0700));
        /* Cancel during the first file, keeps unfinished bytes and remaining mark. */
        ok(batch_prepare(&ui.app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));cancelling=true;polls=draws=0;refreshes=0;run_batch_operation(&ui,&job);assert(job.result.code==RESULT_CANCELLED&&job.result.partial&&job.targets[1].status==BATCH_UNEXECUTED);batch_finish(&ui,&job);assert(refreshes==1&&ui.app.marks_len==2);ok(core_delete(dst));assert(!mkdir(dst,0700));
        notice_clear(&ui);preview_reset(&ui);app_free(&ui.app);
    }
    endwin();delscreen(screen);fclose(in);fclose(out);ok(core_delete(root));free(a);free(b);free(src);free(dst);
    puts("PASS: marks without I/O/sort, focus, safe Cancel paging, batch outcomes/one refresh/failure, frozen draw with polling, details preserve cursor/scroll/focus/cache at 50x9/80x24/160x24");
}
