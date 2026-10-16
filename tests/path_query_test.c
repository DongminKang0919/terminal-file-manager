#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static void tick(void) { struct timespec pause={0,1000000};nanosleep(&pause,NULL); }
static void ready(UiPathQuery *q) {
    uint64_t end=core_monotonic_ms()+2000;
    while(!q->ready) { assert(core_monotonic_ms()<end);ui_path_query_poll(q);tick(); }
}
static size_t fds(void) {
    DIR *d=opendir("/proc/self/fd");assert(d);size_t n=0;
    while(readdir(d))n++;
    closedir(d);return n;
}
int main(void) {
    char root[]="/tmp/tfile-path-query-XXXXXX";assert(mkdtemp(root));
    char a[256],b[256],file[300];snprintf(a,sizeof a,"%s/a",root);snprintf(b,sizeof b,"%s/b",root);
    assert(!mkdir(a,0700)&&!mkdir(b,0700));snprintf(file,sizeof file,"%s/entry",b);FILE *f=fopen(file,"w");assert(f);fclose(f);
    UiPathQuery q={0};size_t before=fds();
    for(unsigned i=0;i<40;i++) {
        ui_path_query_start(&q,true,root,"a",false,false);
        /* Replacement disposes pending and already-buffered results alike. */
        if(i%2) { ready(&q);assert(!strcmp(q.directory,a)); }
        ui_path_query_start(&q,true,root,"b",true,false);ready(&q);
        assert(q.result.code==RESULT_OK && !strcmp(q.directory,b) && q.files.len==1 && !strcmp(q.files.entries[0].name,"entry"));
        ui_path_query_start(&q,true,root,"missing",false,false);ready(&q);
        assert(q.result.code==RESULT_NOT_FOUND);ui_path_query_close(&q);
    }
    ReadJob *occupied=NULL;assert(core_read_resolve_start(root,"a",&occupied).code==RESULT_OK);
    ui_path_query_start(&q,true,root,"b",false,false);assert(q.pending&&!q.job);
    core_read_close(occupied);ready(&q);assert(q.result.code==RESULT_OK&&!strcmp(q.directory,b));ui_path_query_close(&q);
    core_read_shutdown();assert(fds()==before);errno=0;assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    assert(!unlink(file)&&!rmdir(a)&&!rmdir(b)&&!rmdir(root));
    puts("PASS: path query resolve/list/failure, pending/buffered replacement, occupied-slot retry, 40 cancellations and FD/child cleanup");
}
