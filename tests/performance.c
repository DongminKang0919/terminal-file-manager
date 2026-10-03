#define _GNU_SOURCE
#include "ui.h"
#include "platform.h"
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/resource.h>

/* Measurement-only wrappers. No filesystem timing assertions belong here. */
static size_t dirs, sorts, opens, reads, frames, polls, allocations;
static enum { IDLE, STARTUP, FIND, SEARCH, CLOSE } mode;
static size_t input_at;
WINDOW *__wrap_initscr(void) { return stdscr; }
int tfile_main(int, char **);
void __real_qsort(void *,size_t,size_t,int (*)(const void *,const void *));
void __wrap_qsort(void *p,size_t n,size_t s,int (*c)(const void *,const void *)) { sorts++; __real_qsort(p,n,s,c); }
void *__real_malloc(size_t); void *__real_calloc(size_t,size_t); void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n) { allocations++; return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { allocations++; return __real_calloc(n,s); }
void *__wrap_realloc(void *p,size_t n) { allocations++; return __real_realloc(p,n); }
Result __real_platform_directory_open(const char *,PlatformDirectory **);
Result __wrap_platform_directory_open(const char *p,PlatformDirectory **d) { dirs++; return __real_platform_directory_open(p,d); }
Result __real_platform_reader_open(const char *,PlatformReader **);
Result __wrap_platform_reader_open(const char *p,PlatformReader **r) { opens++; return __real_platform_reader_open(p,r); }
Result __real_platform_reader_line(PlatformReader *,char *,size_t,bool *);
Result __wrap_platform_reader_line(PlatformReader *r,char *p,size_t n,bool *e) { reads++; return __real_platform_reader_line(r,p,n,e); }
int __real_wrefresh(WINDOW *);
int __wrap_wrefresh(WINDOW *w) { frames++; return __real_wrefresh(w); }
int __real_input_key(WINDOW *);
int __wrap_input_key(WINDOW *w) {
    if(mode==STARTUP) return 'q';
    if(mode==CLOSE) return 27;
    if(mode==SEARCH) { if(wgetdelay(w)==0) { polls++; return ERR; } return 27; }
    return __real_input_key(w);
}
int __real_input_wide(WINDOW *,wint_t *);
int __wrap_input_wide(WINDOW *w,wint_t *key) {
    if(mode==FIND || mode==SEARCH) {
        const char *term=mode==FIND ? "zzzz-no-match" : "zzzz-no-match";
        *key=input_at<strlen(term) ? (unsigned char)term[input_at++] : '\n';
        return OK;
    }
    return __real_input_wide(w,key);
}
static double now(void) { struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC,&t)); return t.tv_sec*1000.0+t.tv_nsec/1e6; }
static double started;
static void begin(void) { dirs=sorts=opens=reads=frames=polls=allocations=0; started=now(); }
static size_t descriptors(void) {
    DIR *d=opendir("/proc/self/fd"); assert(d); size_t n=0;
    struct dirent *e; while((e=readdir(d))) if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")) n++;
    closedir(d); return n;
}
static size_t rss(void) {
    FILE *f=fopen("/proc/self/status","r"); assert(f); char line[256]; size_t n=0;
    while(fgets(line,sizeof line,f)) if(sscanf(line,"VmRSS: %zu kB",&n)==1) break;
    fclose(f); return n;
}
static void finish(const char *name,int run) {
    double elapsed=now()-started;
    printf("%s,%d,%.6f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",name,run,elapsed,dirs,sorts,opens,reads,frames,polls,allocations,rss(),descriptors());
}
static void ok(Result r) { if(r.code) fprintf(stderr,"%s\n",r.detail); assert(r.code==RESULT_OK); }
static char *path(const char *root,const char *name) { char *p=core_path_join(root,name); assert(p); return p; }
static bool cancel(const OperationProgress *p,void *context) { (void)context; return p->copied_bytes<65536; }
static void resources(const char *root) {
    char *small=path(root,"small"),*many=path(root,"list1000"),*tree=path(root,"tree"),*text=path(root,"text.txt");
    char *large=path(root,"large.bin"),*out=path(root,"out");
    UiContext ui={0}; ok(ui_init(&ui,small)); mode=CLOSE;
    begin(); finish("resources",0);
    for(int cycle=1;cycle<=100;cycle++) {
        ok(app_remember_selection(&ui_panel(&ui)->app,ui_panel(&ui)->selected,ui_panel(&ui)->top));
        ok(app_navigate(&ui_panel(&ui)->app,many)); ok(app_history(&ui_panel(&ui)->app,false));
        ok(app_history(&ui_panel(&ui)->app,true)); ok(app_navigate(&ui_panel(&ui)->app,small));
        app_set_sort(&ui_panel(&ui)->app,(SortSettings){SORT_SIZE,cycle%2},&ui_panel(&ui)->selected);
        /* Match the real UI's session reset on navigation. */
        preview_reset(&ui); ui_panel(&ui)->selected=(size_t)cycle%ui_panel(&ui)->app.files.len;
        draw(&ui); ui.focus=UI_FOCUS_PREVIEW;
        WINDOW *w=dialog_open(&ui,"Resource check",7,50); assert(w); dialog_close(&ui,w);
        notice_record(&ui,"Copy",result_make(RESULT_CANCELLED,"diagnostic\nline"),large,out);
        show_result(&ui);
        SearchResult found=core_search(tree,"f",search_default_limits(),NULL,NULL); ok(found.result); search_result_free(&found);
        char *destination=NULL;
        Result stopped=core_transfer_progress(false,large,out,"cancelled",&destination,cancel,NULL);
        assert(stopped.code==RESULT_CANCELLED && stopped.partial); free(destination);
        char *partial=path(out,"cancelled"); ok(core_delete(partial)); free(partial);
        if(cycle%10==0) { begin(); finish("resources",cycle); }
    }
    notice_clear(&ui); preview_reset(&ui); app_free(&ui_panel(&ui)->app);
    free(small); free(many); free(tree); free(text); free(large); free(out);
    begin(); finish("resources_released",100); mode=IDLE;
}
int main(int argc,char **argv) {
    assert(argc==3); assert(setlocale(LC_ALL,"C.UTF-8")); const char *root=argv[1]; int reps=atoi(argv[2]);
    FILE *out=fopen("/dev/null","w"),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen); resizeterm(24,100); init_theme();
    fprintf(stderr,"%s\n",curses_version());
    puts("name,run,ms,directory_opens,sorts,reader_opens,reader_reads,frames,polls,allocations,rss_kb,fds");
    const char *sizes[]={"small","list1000","list10000"};
    for(size_t size=0;size<3;size++) {
        char *dir=path(root,sizes[size]); char name[80];
        for(int run=0;run<reps;run++) {
            char *args[]={"tfile",dir,NULL}; mode=STARTUP;
            begin(); assert(tfile_main(2,args)==0); snprintf(name,sizeof name,"startup_%s",sizes[size]); finish(name,run); mode=IDLE;
            AppState app={0}; begin(); ok(app_init(&app,dir)); snprintf(name,sizeof name,"open_%s",sizes[size]); finish(name,run);
            begin(); ok(app_refresh(&app)); snprintf(name,sizeof name,"refresh_%s",sizes[size]); finish(name,run);
            for(int key=SORT_NAME;key<=SORT_MODIFIED;key++) {
                size_t selected=app.files.len/2;
                begin(); app_set_sort(&app,(SortSettings){key,false},&selected);
                snprintf(name,sizeof name,"sort_%d_%s",key,sizes[size]); finish(name,run);
            }
            app_free(&app);
            if(size==2) {
                UiContext ui={0}; ok(ui_init(&ui,dir)); draw(&ui); mode=FIND; input_at=0;
                begin(); quick_find(&ui); finish("quick_find_10000",run); mode=IDLE;
                preview_reset(&ui); app_free(&ui_panel(&ui)->app);
            }
        }
        free(dir);
    }
    char *tree=path(root,"tree"),*text=path(root,"text.txt"),*many=path(root,"small-files"),*large=path(root,"large.bin"),*target=path(root,"out");
    for(int run=0;run<reps;run++) {
        begin(); SearchResult result=core_search(tree,"zzzz-no-match",search_default_limits(),NULL,NULL); ok(result.result); finish("search_core",run); search_result_free(&result);
        UiContext ui={0}; ok(ui_init(&ui,tree)); draw(&ui); mode=SEARCH; input_at=0;
        begin(); search_items(&ui); finish("search_ui",run); mode=IDLE; preview_reset(&ui); app_free(&ui_panel(&ui)->app);
        PreviewSession *s=NULL; PreviewText page={0};
        begin(); ok(preview_session_open(text,&s));
        for(size_t at=0;at<20000;at+=20) { ok(preview_session_page(s,at,21,&page)); preview_text_free(&page); }
        finish("preview_forward_20000",run);
        begin(); for(int i=0;i<100;i++) { ok(preview_session_page(s,19980,21,&page)); preview_text_free(&page); } finish("preview_same_page_100",run);
        begin(); for(size_t at=19960;at>19500;at-=20) { ok(preview_session_page(s,at,21,&page)); preview_text_free(&page); } finish("preview_back_cached",run);
        begin(); ok(preview_session_page(s,100,21,&page)); preview_text_free(&page); finish("preview_back_uncached",run); preview_session_close(s);
        for(int which=0;which<2;which++) {
            char name[32]; snprintf(name,sizeof name,"copy-%d-%d",which,run); char *dest=NULL;
            begin(); ok(core_transfer(false,which?large:many,target,name,&dest)); finish(which?"copy_16MiB":"copy_500_small",run);
            ok(core_delete(dest)); free(dest);
        }
        for(int small=0;small<2;small++) {
            resizeterm(small?9:24,small?50:100);
            WINDOW *w=newwin(LINES-2,COLS-8,1,4); assert(w);
            begin(); for(int i=0;i<100;i++) { dialog_frame(w,"Popup"); draw_window_text(w,1,2,COLS-12,"한글 popup surface"); dialog_refresh(w); }
            finish(small?"popup_100_50x9":"popup_100_100x24",run); delwin(w);
        }
        resizeterm(24,100);
    }
    resources(root);
    free(tree); free(text); free(many); free(large); free(target);
    endwin(); delscreen(screen); fclose(in); fclose(out);
}
