#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void tick(void) {struct timespec t={0,10000000};nanosleep(&t,NULL);}
static void ok(Result r) {if(r.code)fprintf(stderr,"%s\n",r.detail);assert(r.code==RESULT_OK);}
static size_t fds(void) {DIR *d=opendir("/proc/self/fd");assert(d);size_t n=0;while(readdir(d))n++;closedir(d);return n;}
static Result finish(ExternalJob *job) {bool done=false;Result r;for(unsigned i=0;i<300;i++) {r=core_external_poll(job,&done);if(done)return r;tick();}assert(0);return result_make(RESULT_IO,"timeout");}
int main(int argc,char **argv) {
    assert(argc==4);size_t before=fds();EditorCommand c;
    ok(editor_command_parse("'editor tool' --flag 'quoted spaces' \"$(literal)\" ''",&c));
    assert(c.len==5&&!strcmp(c.argv[0],"editor tool")&&!strcmp(c.argv[2],"quoted spaces")&&!strcmp(c.argv[3],"$(literal)")&&!c.argv[4][0]);editor_command_free(&c);
    const char *invalid[]={"", "vi | cat", "vi >x", "vi $(touch x)", "vi `x`", "vi ; x", "vi '", "vi \\"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++) {assert(editor_command_parse(invalid[i],&c).code!=RESULT_OK&&!c.argv);editor_command_free(&c);}
    assert(!setenv("VISUAL","'editor tool' --flag 'quoted spaces' \"$(literal)\"",1));assert(!setenv("EDITOR","missing",1));
    struct sigaction term_before,hup_before,term_after,hup_after;
    assert(!sigaction(SIGTERM,NULL,&term_before)&&!sigaction(SIGHUP,NULL,&hup_before));
    struct sigaction prior,after;assert(!sigaction(SIGINT,NULL,&prior));ok(core_editor_run(argv[1]));assert(!sigaction(SIGINT,NULL,&after)&&prior.sa_handler==after.sa_handler);
    assert(!setenv("VISUAL","",1));assert(!setenv("EDITOR","'editor tool' --parent-int",1));ok(core_editor_run(argv[1]));
    assert(!unsetenv("VISUAL")&&!unsetenv("EDITOR"));ok(core_editor_run(argv[1])); /* fixture vi, not system vi */
    assert(!setenv("VISUAL","'editor tool' --fail",1));assert(core_editor_run(argv[1]).code==RESULT_IO);
    assert(!setenv("VISUAL","no-shebang",1));assert(core_editor_run(argv[1]).code==RESULT_IO);
    assert(core_editor_run(argv[2]).code==RESULT_UNSUPPORTED); /* directory */
    assert(!sigaction(SIGTERM,NULL,&term_after)&&!sigaction(SIGHUP,NULL,&hup_after));
    assert(term_before.sa_handler==term_after.sa_handler&&hup_before.sa_handler==hup_after.sa_handler);
    ExternalJob *job=NULL;ok(core_external_open(argv[1],&job));ok(finish(job));core_external_close(job);
    assert(!setenv("TFILE_EXTERNAL_MODE","slow",1));ok(core_external_open(argv[1],&job));core_external_close(job);
    ExternalJob *next=NULL;assert(core_external_open(argv[1],&next).code==RESULT_EXISTS&&!next);
    for(unsigned i=0;i<300&&core_external_cleanup_pending();i++) tick();
    assert(!core_external_cleanup_pending());
    assert(!setenv("TFILE_EXTERNAL_MODE","fail",1));ok(core_external_open(argv[1],&job));assert(finish(job).code==RESULT_IO);core_external_close(job);
    assert(!setenv("VISUAL","'editor tool'",1));core_media_install_signals();
    assert(!raise(SIGTERM));assert(core_editor_run(argv[1]).code==RESULT_CANCELLED);
    core_media_shutdown();
    assert(!setenv("PATH",argv[3],1));assert(core_external_open(argv[1],&job).code==RESULT_NOT_FOUND&&!job);
    assert(fds()==before);
    puts("PASS: argv grammar/priority/vi, literal shell-looking filenames, editor signal restoration, no shell fallback/limits, GUI ack/failure/live-detach/reap and FD cleanup");
}
