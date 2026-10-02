#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include <assert.h>

typedef struct { int key, kind; const char *text; int focus, cursor; } Step;
static const Step *steps;
static size_t at, total, created, destroyed;
static WINDOW *last_window;
WINDOW *__real_newwin(int,int,int,int);
WINDOW *__wrap_newwin(int h,int w,int y,int x) { created++; return __real_newwin(h,w,y,x); }
int __real_delwin(WINDOW *w);
int __wrap_delwin(WINDOW *w) { destroyed++; return __real_delwin(w); }
Result __wrap_app_refresh(AppState *app) { (void)app; return result_make(RESULT_ACCESS,"Injected refresh failure"); }
static short pair_at(WINDOW *win,int y,int x) {
    cchar_t cell; wchar_t text[CCHARW_MAX]; attr_t attr; short pair;
    assert(mvwin_wch(win,y,x,&cell)==OK);
    assert(getcchar(&cell,text,&attr,&pair,NULL)==OK); return pair;
}
static Step next(WINDOW *win) {
    assert(at<total);
    Step step=steps[at++];
    if(last_window) assert(last_window==win);
    last_window=win;
    int cy,cx; getyx(win,cy,cx);
    if(step.cursor>=0) assert(cy==3 && cx==step.cursor);
    if(step.text) {
        bool found=false;
        for(int y=1;y<getmaxy(win)-1;y++) {
            char text[256]; mvwinnstr(win,y,2,text,getmaxx(win)-4);
            if(strstr(text,step.text)) {
                found=true;
                if(step.focus==2) assert(pair_at(win,y,2)==UI_SELECTED);
            }
        }
        assert(found);
    }
    if(step.focus==1) assert(pair_at(win,3,2)==UI_SELECTED);
    return step;
}
int __wrap_input_wide(WINDOW *win,wint_t *key) { Step s=next(win); *key=s.key; return s.kind; }
int __wrap_input_key(WINDOW *win) { return next(win).key; }
static void script(const Step *s,size_t len) { steps=s; total=len; at=0; created=destroyed=0; last_window=NULL; }
static void complete(void) { assert(at==total && created==1 && destroyed==1); }
#define KEY(k) {k,KEY_CODE_YES,NULL,0,-1}
#define CHAR(k) {k,OK,NULL,0,-1}
#define SCRIPT(s) script(s,sizeof s/sizeof s[0])
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen); init_theme();
    for(int small=0;small<2;small++) {
        resizeterm(small?9:24,small?50:100);
        UiContext ui={0}; ui.app.directory="/";
        const char *labels[]={"New name (no directory path)","Directory path - absolute or relative"};
        for(int i=0;i<2;i++) {
            char value[32];
            const Step empty[]={CHAR('\n'),{'a',OK,"Enter a value to continue.",1,2},CHAR('b'),KEY(KEY_LEFT),CHAR('\n')};
            SCRIPT(empty); assert(prompt_value(&ui,labels[i],value,sizeof value,""));
            complete(); assert(!strcmp(value,"ab"));
            /* 2 Hangul characters exceed 4 output bytes, despite fitting the field.
               Submit via OK, then backspace at the preserved end cursor. */
            const Step large[]={CHAR('\t'),CHAR('\n'),{KEY_BACKSPACE,KEY_CODE_YES,"Input is too long.",1,6},CHAR('\n')};
            SCRIPT(large); assert(prompt_value(&ui,labels[i],value,5,"한글"));
            complete(); assert(!strcmp(value,"한"));
            const Step raw[]={KEY(KEY_HOME),CHAR('z'),CHAR('\n')};
            SCRIPT(raw); assert(prompt_value(&ui,labels[i],value,sizeof value,"a\xff\n"));
            complete(); assert(!strcmp(value,"za\xff\n"));
        }
        const Step options[]={KEY(KEY_DOWN),KEY(KEY_DOWN),KEY(KEY_DOWN),CHAR('\n'),
            {'\n',OK,"Sort by: Size",2,-1},{KEY_UP,KEY_CODE_YES,"Sort by: Modified",2,-1},
            {27,OK,"Wheel scroll:",2,-1}};
        SCRIPT(options); show_options(&ui); complete(); assert(ui.app.sort.key==SORT_MODIFIED);
        Item item={.name="kept",.path="/kept"};
        ui.app.files=(FileList){&item,1};
        const Step failure[]={CHAR('\n'),{27,OK,"Injected refresh failure",0,-1}};
        SCRIPT(failure); show_options(&ui); complete(); assert(!ui.app.show_hidden);
        assert(strstr(ui.status,"Refresh failed"));
        assert(ui.app.files.entries==&item && ui.app.files.len==1 && !strcmp(item.name,"kept"));
        assert(!ui.modal_depth);
    }
    endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: name/path empty and encoded overflow warnings, preserved edit cursor/raw bytes, one Options window with retained focus/scroll and visible refresh failure at 50x9/100x24");
}
