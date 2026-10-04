#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static uint64_t clock_offset;
uint64_t __real_platform_monotonic_ms(void);
uint64_t __wrap_platform_monotonic_ms(void) { return __real_platform_monotonic_ms()+clock_offset; }
static void pause_tick(void) { struct timespec ts={0,10000000}; nanosleep(&ts,NULL); }
static size_t count(const char *path,const char *prefix) {
    DIR *d=opendir(path); assert(d); size_t n=0; struct dirent *e;
    while((e=readdir(d))) if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")&&(!prefix||!strncmp(e->d_name,prefix,strlen(prefix)))) n++;
    closedir(d); return n;
}
static void source(const char *path,PreviewMediaKind kind,const char *mode) {
    FILE *f=fopen(path,"wb"); assert(f);
    if(kind==PREVIEW_PNG) fwrite("\211PNG\r\n\032\n",1,8,f);
    if(kind==PREVIEW_JPEG) fwrite("\377\330\377",1,3,f);
    if(kind==PREVIEW_PDF) fputs("%PDF-1.4\n",f);
    fputs(mode,f); assert(!fclose(f));
}
static Result opened(const char *path,PreviewMediaKind kind,bool text,MediaPreview **out) {
    Result r;
    for(int i=0;i<200;i++) {
        r=core_media_open(path,kind,text,120,90,out);
        if(r.code!=RESULT_CANCELLED) return r;
        pause_tick();
    }
    assert(!"Cancelled converter failed to retire"); return r;
}
static Result completed(MediaPreview *job,char **data,size_t *len) {
    Result r; bool done=false;
    for(int i=0;i<300;i++) {
        r=core_media_poll(job,&done,data,len);
        if(done) return r;
        assert(r.code==RESULT_OK); pause_tick();
    }
    assert(!"Converter did not finish"); return r;
}
static void select_name(UiContext *ui,const char *name) {
    for(size_t i=0;i<ui_panel(ui)->app.files.len;i++) if(!strcmp(ui_panel(ui)->app.files.entries[i].name,name)) {
        ui_panel(ui)->selected=i; return;
    }
    assert(!"Missing fixture");
}
static void prepared(UiContext *ui) {
    for(int i=0;i<300;i++) {
        preview_prepare(ui,17);
        if(ui->media_done) return;
        pause_tick();
    }
    assert(!"UI conversion did not finish");
}
static bool contains_screen(const char *text) {
    char row[100];
    for(int y=3;y<20;y++) { mvinnstr(y,57,row,40); if(strstr(row,text)) return true; }
    return false;
}
int main(int argc,char **argv) {
    if(argc==6) {
        MediaPreview *job=NULL; char *data=NULL; size_t len=0;
        PreviewSession *reader=NULL;
        assert(preview_session_open(argv[1],&reader).code==RESULT_OK);
        PreviewMediaKind kind=preview_session_media(reader); preview_session_close(reader);
        Result r=core_media_open(argv[1],kind,atoi(argv[3])!=0,(unsigned)atoi(argv[4]),(unsigned)atoi(argv[5]),&job);
        if(r.code==RESULT_OK) {
            bool done=false;
            while(!done) { r=core_media_poll(job,&done,&data,&len); if(!done) pause_tick(); }
            if(r.code==RESULT_OK && !atoi(argv[3]) && !graphics_validate(data,len,(unsigned)atoi(argv[4]),(unsigned)atoi(argv[5])))
                r=result_make(RESULT_IO,"Invalid Sixel");
        }
        if(r.code==RESULT_OK) { FILE *f=fopen(argv[2],"wb"); assert(f); fwrite(data,1,len,f); fclose(f); }
        printf("Result %d (%zu bytes): %s\n",r.code,len,r.detail);
        free(data); core_media_close(job); core_media_shutdown(); return r.code==RESULT_OK?0:1;
    }
    assert(argc==2); const char *tools=argv[1];
    const char *valid="\033Pq\"1;1;1;1@\033\\";
    assert(graphics_validate(valid,strlen(valid),1,1));
    const char *invalid[]={"\033Pq\"1;1;1;1A\033\\", "\033Pq\"1;1;1;1!9999999~\033\\",
        "\033Pq\"2;1;1;1@\033\\", "\033Pq\"1;1;1;1@\033\\\n\n", "\033Pq\"1;1;1;1@\033\\\033[2J"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++) assert(!graphics_validate(invalid[i],strlen(invalid[i]),1,1));
    unsigned random=12345;
    for(size_t i=0;i<10000;i++) {
        char fuzz[64];
        for(size_t j=0;j<sizeof fuzz;j++) { random=random*1664525u+1013904223u; fuzz[j]=(char)(random>>24); }
        if(i%2) memcpy(fuzz,"\033Pq\"1;1;1;1",12);
        (void)graphics_validate(fuzz,i%sizeof fuzz,512,384);
    }
    assert(!setenv("PATH",tools,1));
    size_t fds=count("/proc/self/fd",NULL),temps=count("/tmp","tfile-media-");
    char dir[]="/tmp/tfile-media-test-XXXXXX"; assert(mkdtemp(dir));
    char path[256],next[256],fifo[256];
    snprintf(path,sizeof path,"%s/- 한글 'quote\" [0].png",dir);
    snprintf(next,sizeof next,"%s/next",dir); snprintf(fifo,sizeof fifo,"%s/pipe",dir);
    MediaPreview *job=NULL; char *data=NULL; size_t len=0; Result r;
    for(PreviewMediaKind kind=PREVIEW_PNG;kind<=PREVIEW_PDF;kind++) {
        source(path,kind,"ok"); assert(opened(path,kind,false,&job).code==RESULT_OK);
        r=completed(job,&data,&len); assert(r.code==RESULT_OK && graphics_validate(data,len,120,90));
        free(data); core_media_close(job);
    }
    source(path,PREVIEW_PDF,"ok"); assert(opened(path,PREVIEW_PDF,true,&job).code==RESULT_OK);
    assert(completed(job,&data,&len).code==RESULT_OK && strstr(data,"First page text")); free(data); core_media_close(job);
    source(path,PREVIEW_PDF,"limits"); assert(opened(path,PREVIEW_PDF,true,&job).code==RESULT_OK);
    assert(completed(job,&data,&len).code==RESULT_OK);
    long long address,cpu,size,core;
    assert(sscanf(data,"%lld;%lld;%lld;%lld",&address,&cpu,&size,&core)==4);
#ifndef __has_feature
#define __has_feature(feature) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    assert(address!=512LL*1024*1024);
#else
    assert(address==512LL*1024*1024);
#endif
    assert(cpu==5 && size==PREVIEW_PDF_TEXT_BYTES && core==0);
    printf("Child soft limits: AS=%lld CPU=%lld FSIZE=%lld CORE=%lld\n",address,cpu,size,core);
    free(data);core_media_close(job);
    for(int i=0;i<4;i++) {
        source(path,PREVIEW_PNG,i==0?"fail":i==1?"huge":i==2?"bad":"overflow");
        assert(opened(path,PREVIEW_PNG,false,&job).code==RESULT_OK);
        r=completed(job,&data,&len);
        if(i<2) assert(r.code==RESULT_IO);
        else { assert(r.code==RESULT_OK && !graphics_validate(data,len,120,90)); free(data); }
        core_media_close(job);
    }
    source(path,PREVIEW_PDF,"encrypted"); assert(opened(path,PREVIEW_PDF,false,&job).code==RESULT_OK);
    r=completed(job,&data,&len); assert(r.code==RESULT_IO && strstr(r.detail,"Encrypted PDF")); core_media_close(job);
    source(path,PREVIEW_PNG,"slow"); assert(opened(path,PREVIEW_PNG,false,&job).code==RESULT_OK);
    clock_offset=9000; r=completed(job,&data,&len); assert(r.code==RESULT_IO && strstr(r.detail,"timed out"));
    core_media_close(job); clock_offset=0;
    for(int i=0;i<50;i++) {
        source(path,PREVIEW_PNG,"slow"); assert(opened(path,PREVIEW_PNG,false,&job).code==RESULT_OK);
        core_media_close(job);
    }
    core_media_shutdown(); assert(count("/proc/self/fd",NULL)==fds);
    source(path,PREVIEW_PNG,"late"); assert(opened(path,PREVIEW_PNG,false,&job).code==RESULT_OK);
    source(next,PREVIEW_PNG,"ok"); assert(!rename(next,path));
    r=completed(job,&data,&len); assert(r.code==RESULT_IO && strstr(r.detail,"File changed")); core_media_close(job);
    assert(!mkfifo(fifo,0600)); assert(core_media_open(fifo,PREVIEW_PNG,false,120,90,&job).code==RESULT_UNSUPPORTED && !job);
    assert(!unlink(fifo));
    source(path,PREVIEW_NOT_MEDIA,"ordinary text with .png extension");
    PreviewSession *s=NULL; assert(preview_session_open(path,&s).code==RESULT_OK);
    assert(preview_session_media(s)==PREVIEW_NOT_MEDIA); preview_session_close(s);
    FILE *zero=fopen(path,"w"); assert(zero); fclose(zero);
    assert(preview_session_open(path,&s).code==RESULT_OK && preview_session_media(s)==PREVIEW_NOT_MEDIA);
    PreviewText page; assert(preview_session_page(s,0,10,&page).code==RESULT_OK && page.empty); preview_text_free(&page); preview_session_close(s);
    source(path,PREVIEW_PNG,"ok"); assert(!setenv("PATH","/nonexistent",1));
    r=core_media_open(path,PREVIEW_PNG,false,120,90,&job); assert(r.code==RESULT_UNSUPPORTED && strstr(r.detail,"Missing ImageMagick"));
    assert(!setenv("PATH",tools,1));
    source(next,PREVIEW_PDF,"ok");
    UiContext ui={0}; assert(ui_init(&ui,dir).code==RESULT_OK);
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen); resizeterm(24,100); init_theme();
    ui.sixel_confirmed=true; ui.cell_width=8; ui.cell_height=16;
    select_name(&ui,"- 한글 'quote\" [0].png"); prepared(&ui);
    assert(ui.media_done && !ui.media_text && ui.media_data && ui.media_result.code==RESULT_OK);
    source(path,PREVIEW_PNG,"slow");preview_reset(&ui);preview_prepare(&ui,17);
    assert(ui.media_job && media_pending(&ui));
    preview_prepare(&ui,0);assert(!ui.media_job && !media_pending(&ui));
    for(int i=0;i<300 && core_media_cleanup_pending();i++) {preview_prepare(&ui,0);pause_tick();}
    assert(!core_media_cleanup_pending());
    for(int i=0;i<100;i++) {preview_prepare(&ui,0);assert(!media_pending(&ui));}
    preview_prepare(&ui,17);assert(ui.media_job && media_pending(&ui));
    select_name(&ui,"next");prepared(&ui);assert(ui.media_kind==PREVIEW_PDF);
    source(path,PREVIEW_PNG,"ok");select_name(&ui,"- 한글 'quote\" [0].png");prepared(&ui);
    char *cache=ui.media_data;
    for(int i=0;i<100;i++) { preview_prepare(&ui,17); assert(ui.media_data==cache && !ui.media_job); }
    ui.focus=UI_FOCUS_PREVIEW; preview_prepare(&ui,17); assert(ui.media_data==cache);
    ui.modal_depth=1; draw_cached(&ui); assert(!ui.graphics_visible && ui.media_data==cache);
    ui.modal_depth=0; /* Suppress actual stdout graphics in this cell test. */
    ui.sixel_confirmed=false; media_reset(&ui); preview_prepare(&ui,17); erase(); preview(&ui,55,2,45,20);
    assert(contains_screen("Terminal query failed") && !ui.media_data);
    select_name(&ui,"next"); prepared(&ui); assert(ui.media_text && ui.media_data);
    ui.image_auto=false; media_reset(&ui); prepared(&ui); assert(ui.media_text && ui.media_data);
    for(int i=0;i<4;i++) {
        source(next,PREVIEW_PDF,i==0?"zero":i==1?"white":i==2?"ff":"indent");
        preview_reset(&ui); prepared(&ui); erase(); preview(&ui,55,2,45,20);
        if(i<3) assert(contains_screen("No text could be extracted"));
        else assert(contains_screen("  Indented") && contains_screen("Next line") && !contains_screen("\\x0C"));
    }
    for(int i=0;i<3;i++) {
        source(next,PREVIEW_PDF,i==0?"fail":i==1?"slow":"ok");preview_reset(&ui);
        if(i==2) assert(!setenv("PATH","/nonexistent",1));
        if(i==1) {preview_prepare(&ui,17);clock_offset=9000;}
        prepared(&ui);erase();preview(&ui,55,2,45,20);
        assert(contains_screen(i==2 ? "Preview unavailable" : "Preview failed") && !contains_screen("No text could be extracted"));
        clock_offset=0;assert(!setenv("PATH",tools,1));
    }
    ui_set_mode(&ui,UI_LIST_ONLY); assert(!ui.media_job && !ui.media_data);
    ui_free(&ui); endwin(); delscreen(screen); fclose(out); fclose(in);
    core_media_shutdown(); assert(!unlink(path) && !unlink(next) && !rmdir(dir));
    assert(count("/proc/self/fd",NULL)==fds);
    assert(count("/tmp","tfile-media-")==temps);
    errno=0; assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    puts("PASS: signatures/empty/special names, PNG/JPEG/PDF-page-1, bounded text, missing tools/errors/encryption/timeout/output validation, 50 cancellations, stale identity, FIFO rejection, FD/child/temp cleanup, UI cache/focus/modal/off modes");
}
