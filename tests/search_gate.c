#define _POSIX_C_SOURCE 200809L
#include "../src/core/core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
/* Test-only pipe synchronization after real matches and a real UI frame. */
typedef struct { SearchProgress callback; void *context; bool gated, isolated; } Gate;
static int descriptor(const char *variable, int flags) {
    int fd = atoi(getenv(variable));
    if (fcntl(fd, F_GETFD) >= 0) return fd;
    /* The isolated worker closes inherited test channels. Reopen the parent's
       deliberately passed fixture pipe; production workers never do this. */
    char path[128]; snprintf(path, sizeof path, "/proc/%ld/fd/%d", (long)getppid(), fd);
    int opened = open(path, flags); assert(opened >= 0); return opened;
}
static bool progress(const SearchResult *r, const char *path, void *context) {
    Gate *g=context;
    bool keep=g->callback(r,path,g->context);
    if (keep && !g->gated && r->matches.len>=64) {
        g->gated=true;
        int event=descriptor("TFILE_GATE_EVENT", O_WRONLY), command=descriptor("TFILE_GATE_COMMAND", O_RDONLY);
        assert(write(event,"G\n",2)==2);
        char ch; ssize_t n;
        do { n=read(command,&ch,1); } while(n<0 && errno==EINTR);
        assert(n==1);
        if (g->isolated) {
            /* Keep the producer at exactly 64 results until the parent cancels.
               Releasing the input channel is no longer the UI's wakeup. */
            for (;;) pause();
        }
        /* Give the UI callbacks a chance to consume queued non-command input. */
        for (int i=0;i<16 && keep;i++) keep=g->callback(r,path,g->context);
    }
    return keep;
}
SearchResult __real_core_search_unsorted(const char *,const char *,SearchLimits,SearchProgress,void *);
SearchResult __wrap_core_search_unsorted(const char *root,const char *term,SearchLimits limits,SearchProgress cb,void *context) {
    Gate gate={cb,context,false,true};
    return __real_core_search_unsorted(root,term,limits,progress,&gate);
}
void __real_core_read_cancel(ReadJob *);
void __wrap_core_read_cancel(ReadJob *job) {
    __real_core_read_cancel(job);
    const SearchResult *r = core_read_snapshot(job);
    dprintf(atoi(getenv("TFILE_GATE_EVENT")),"R %d %zu\n",r->stopped,r->matches.len);
}
SearchResult __real_core_search(const char *,const char *,SearchLimits,SearchProgress,void *);
SearchResult __wrap_core_search(const char *root,const char *term,SearchLimits limits,SearchProgress cb,void *context) {
    Gate gate={cb,context,false,false};
    SearchResult r=__real_core_search(root,term,limits,progress,&gate);
    dprintf(atoi(getenv("TFILE_GATE_EVENT")),"R %d %zu\n",r.stopped,r.matches.len);
    return r;
}
