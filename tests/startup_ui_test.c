#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <unistd.h>
#define main tfile_main
#include "../src/ui/main.c"
#undef main
static size_t directories, sorts, initialized_directories, initialized_sorts, inputs;
Result __real_platform_directory_open(const char *,PlatformDirectory **);
Result __wrap_platform_directory_open(const char *p,PlatformDirectory **out) { directories++; return __real_platform_directory_open(p,out); }
void __real_qsort(void *,size_t,size_t,int (*)(const void *,const void *));
void __wrap_qsort(void *p,size_t n,size_t s,int (*compare)(const void *,const void *)) { sorts++; __real_qsort(p,n,s,compare); }
Result __real_app_init(AppState *,const char *);
Result __wrap_app_init(AppState *app,const char *path) {
    Result r=__real_app_init(app,path);
    assert(r.code==RESULT_OK && app->files.len==3 && !strcmp(app->files.entries[0].name,"A"));
    initialized_directories=directories; initialized_sorts=sorts; return r;
}
WINDOW *__wrap_initscr(void) { return stdscr; }
Result __wrap_terminal_session_check(void) { return result_make(RESULT_OK,NULL); }
int __wrap_input_key(WINDOW *win) {
    (void)win; inputs++;
    assert(directories==initialized_directories && sorts==initialized_sorts);
    return 'q';
}
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));
    char root[]="/tmp/tfile-startup-XXXXXX"; assert(mkdtemp(root));
    const char *names[]={"a","A","한글\xff\n"};
    for(size_t i=0;i<3;i++) assert(core_create(root,names[i],false).code==RESULT_OK);
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen);
    for(int small=0;small<2;small++) {
        resizeterm(small?9:24,small?50:100);
        directories=sorts=inputs=0;
        char *args[]={"tfile",root,NULL}; assert(tfile_main(2,args)==0);
        assert(inputs==1 && directories==2 && sorts==1); /* validation + one list read */
    }
    AppState app={0}; directories=sorts=0;
    assert(app_init(&app,root).code==RESULT_OK && directories==2 && sorts==1);
    size_t selected=2; app_set_sort(&app,(SortSettings){SORT_SIZE,true},&selected);
    directories=sorts=0; assert(app_refresh(&app).code==RESULT_OK);
    assert(directories==2 && sorts==1 && app.sort.key==SORT_SIZE && app.sort.descending);
    for(size_t i=1;i<app.files.len;i++) assert(main_file_compare(&app.files.entries[i-1],&app.files.entries[i],app.sort)<=0);
    app_free(&app);
    FileList picker={0}; directories=sorts=0;
    assert(core_list(root,true,false,&picker).code==RESULT_OK && directories==1 && sorts==1);
    for(size_t i=1;i<picker.len;i++) assert(file_compare(&picker.entries[i-1],&picker.entries[i])<=0);
    file_list_free(&picker);
    for(size_t i=0;i<3;i++) { char *p=core_path_join(root,names[i]); assert(p); assert(core_delete(p).code==RESULT_OK); free(p); }
    assert(!rmdir(root)); endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: one initial list read, one main sort per load/refresh, unchanged picker order and raw-name ties at 50x9/100x24");
}
