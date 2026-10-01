#define _POSIX_C_SOURCE 200809L
#include "../src/core/core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Test-only pipe synchronization after real matches and a real UI frame. */
typedef struct { SearchProgress callback; void *context; bool gated; } Gate;
static bool progress(const SearchResult *r, const char *path, void *context) {
    Gate *g=context;
    bool keep=g->callback(r,path,g->context);
    if (keep && !g->gated && r->matches.len>=64) {
        g->gated=true;
        int event=atoi(getenv("TFILE_GATE_EVENT")), command=atoi(getenv("TFILE_GATE_COMMAND"));
        assert(write(event,"G\n",2)==2);
        char ch; ssize_t n;
        do { n=read(command,&ch,1); } while(n<0 && errno==EINTR);
        assert(n==1);
        /* Give the UI callbacks a chance to consume queued non-command input. */
        for (int i=0;i<16 && keep;i++) keep=g->callback(r,path,g->context);
    }
    return keep;
}
SearchResult __real_core_search(const char *,const char *,SearchLimits,SearchProgress,void *);
SearchResult __wrap_core_search(const char *root,const char *term,SearchLimits limits,SearchProgress cb,void *context) {
    Gate gate={cb,context,false};
    SearchResult r=__real_core_search(root,term,limits,progress,&gate);
    dprintf(atoi(getenv("TFILE_GATE_EVENT")),"R %d %zu\n",r.stopped,r.matches.len);
    return r;
}
