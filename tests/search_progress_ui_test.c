#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include <assert.h>
#include <unistd.h>
static unsigned polls, progress_frames, input_at;
static bool cancel_search;
uint64_t __wrap_core_monotonic_ms(void) { return 1000; }
uint64_t __wrap_platform_monotonic_ms(void) { return 1000; }
int __real_wrefresh(WINDOW *);
int __wrap_wrefresh(WINDOW *win) {
    if(win!=stdscr) {
        int y,x; getyx(win,y,x); char text[128];
        mvwinnstr(win,2,2,text,sizeof text-1);
        if(strstr(text,"Searching")) progress_frames++;
        wmove(win,y,x);
    }
    return __real_wrefresh(win);
}
int __wrap_input_wide(WINDOW *win,wint_t *key) {
    (void)win; *key=input_at++==0 ? 'f' : '\n'; return OK;
}
int __wrap_input_key(WINDOW *win) {
    if(wgetdelay(win)==0) { polls++; return cancel_search && polls==2 ? 27 : ERR; }
    return 27;
}
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));
    char root[]="/tmp/tfile-search-render-XXXXXX"; assert(mkdtemp(root));
    for(int i=0;i<1024;i++) { char name[32]; snprintf(name,sizeof name,"f%04d",i); assert(core_create(root,name,false).code==RESULT_OK); }
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen); init_theme(); input_init();
    UiContext ui={0}; assert(ui_init(&ui,root).code==RESULT_OK);
    for(int small=0;small<2;small++) for(int cancel=0;cancel<2;cancel++) {
        resizeterm(small?9:24,small?50:100);
        cancel_search=cancel; polls=progress_frames=input_at=0;
        Result retained=result_make(RESULT_IO,"retained operation failure");
        notice_record(&ui,"Copy",retained,"/source",NULL);
        size_t selected=ui_panel(&ui)->selected,top=ui_panel(&ui)->top;
        search_items(&ui);
        assert(progress_frames==1); /* First frame only; every callback still polls. */
        assert(cancel ? polls==2 : polls>=17);
        assert(ui_panel(&ui)->selected==selected && ui_panel(&ui)->top==top && !ui.modal_depth);
        assert(ui.notice.visible && ui.notice.operation.code==RESULT_IO);
        if(cancel) assert(strstr(ui.status,"Search cancelled: 64 found"));
    }
    notice_clear(&ui); preview_reset(&ui); app_free(&ui_panel(&ui)->app);
    for(int i=0;i<1024;i++) { char name[32]; snprintf(name,sizeof name,"f%04d",i); char *p=core_path_join(root,name); assert(p && core_delete(p).code==RESULT_OK); free(p); }
    assert(!rmdir(root)); endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: frozen-clock search suppresses redundant frames but polls every callback, keeps cancellation/results/notice and UI state at 50x9/100x24");
}
