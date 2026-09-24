#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include <assert.h>
#include <unistd.h>
static unsigned polls, draws;
uint64_t __wrap_core_monotonic_ms(void) { return 1000; }
int __wrap_input_wide(WINDOW *win, wint_t *key) { (void)win; (void)key; polls++; return ERR; }
int __real_wrefresh(WINDOW *win);
int __wrap_wrefresh(WINDOW *win) { draws++; return __real_wrefresh(win); }
int main(void) {
    char dir[]="/tmp/tfile-progress-ui-XXXXXX"; assert(mkdtemp(dir));
    char *source=core_path_join(dir,"source"), *destination=NULL;
    FILE *file=fopen(source,"w"); assert(file);
    for(unsigned i=0;i<262144;i++) assert(fputs("test",file)>=0);
    assert(!fclose(file));
    FILE *out=tmpfile(), *in=tmpfile(); assert(out && in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen);
    UiContext ui={0};
    Result r=run_file_operation(&ui,true,source,dir,"copy",&destination);
    assert(r.code==RESULT_OK && r.completed_items==1 && r.copied_bytes==1048576);
    assert(draws==1 && polls>=34); /* Poll every chunk even while drawing is suppressed. */
    assert(core_delete(source).code==RESULT_OK && core_delete(destination).code==RESULT_OK);
    free(source); free(destination); assert(!rmdir(dir));
    endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: frozen-clock UI draws once while checking input at least 34 times during 1 MiB copy");
}
