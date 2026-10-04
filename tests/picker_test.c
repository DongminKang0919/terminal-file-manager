#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

static int step;
static bool failed, parent_recovery;
static const char *recovery;
static bool contains(WINDOW *win,const char *text) {
    int h,w;getmaxyx(win,h,w);
    for(int y=0;y<h;y++) {
        char line[256];int n=w<255?w:255;
        for(int x=0;x<n;x++)line[x]=(char)(mvwinch(win,y,x)&A_CHARTEXT);
        line[n]=0;if(strstr(line,text))return true;
    }
    return false;
}
int __wrap_input_key(WINDOW *win) {
    if(failed && step<2) {
        assert(contains(win,"Cannot read directory"));
        assert(contains(win,"Cannot inspect directory contents"));
        assert(!contains(win,"You can use this directory") && !contains(win,"This directory is empty"));
        return step++==0?' ':parent_recovery?KEY_LEFT:'p';
    }
    assert(!contains(win,"Cannot read directory"));
    if(!failed) assert(contains(win,"No subdirectories") || contains(win,"This directory is empty"));
    step++;return failed || !recovery?' ':27;
}
bool __wrap_prompt_value_status(UiContext *ui,const char *title,char *out,size_t cap,const char *initial,bool *resized) {
    (void)ui;(void)title;(void)initial;(void)resized;
    snprintf(out,cap,"%s",recovery);return true;
}
int main(void) {
    assert(geteuid()!=0); /* Permission regression must really run without root. */
    char root[]="/tmp/tfile-picker-XXXXXX";assert(mkdtemp(root));
    char denied[256],deleted[256],empty[256];
    snprintf(denied,sizeof denied,"%s/denied",root);snprintf(deleted,sizeof deleted,"%s/deleted",root);snprintf(empty,sizeof empty,"%s/empty",root);
    assert(!mkdir(denied,0700)&&!mkdir(deleted,0700)&&!mkdir(empty,0700));
    UiContext ui={0};assert(ui_init(&ui,root).code==RESULT_OK);
    FILE *out=tmpfile(),*in=tmpfile();SCREEN *screen=newterm("xterm-256color",out,in);assert(screen);resizeterm(24,100);init_theme();
    char result[UI_INPUT_CAP];bool resized=false;
    failed=false;recovery=NULL;step=0;
    assert(pick_path(&ui,true,empty,result,&resized)&&!strcmp(result,empty));
    recovery=empty;step=0;assert(!pick_path(&ui,false,empty,result,&resized));
    assert(!chmod(denied,0000));assert(!rmdir(deleted));
    failed=true;
    for(int i=0;i<2;i++) {
        step=0;parent_recovery=i==1;recovery=empty;snprintf(result,sizeof result,"unchanged");
        assert(pick_path(&ui,true,i?deleted:denied,result,&resized));
        assert(step==3&&!strcmp(result,i?root:empty));
    }
    ui_free(&ui);endwin();delscreen(screen);fclose(out);fclose(in);
    assert(!chmod(denied,0700)&&!rmdir(denied)&&!rmdir(empty)&&!rmdir(root));
    puts("PASS: non-root permission denial/deleted path are not empty or usable; empty destination/source, blocked use, explicit path and Parent recovery");
}
