#define _POSIX_C_SOURCE 200809L
#include "../src/core/core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/* Link-only PTY synchronization. Production code has no test delays or hooks.
   Stop after the real UI callback has drawn; release when input is queued. */
typedef struct { OperationCallback callback; void *context; bool copy, gated; } Gate;
static bool gated_progress(const OperationProgress *p, void *context) {
    Gate *g = context;
    bool keep = !g->callback || g->callback(p,g->context);
    if (keep && !g->gated && (g->copy ? p->copied_bytes >= 65536 : p->completed_items >= 2)) {
        g->gated = true;
        int event = atoi(getenv("TFILE_GATE_EVENT")), command = atoi(getenv("TFILE_GATE_COMMAND"));
        assert(write(event,"G\n",2)==2);
        char ch; ssize_t n; do { n=read(command,&ch,1); } while (n<0 && errno==EINTR);
        assert(n==1);
        keep = !g->callback || g->callback(p,g->context);
    }
    return keep;
}
static void result_event(Result r) {
    int event = atoi(getenv("TFILE_GATE_EVENT"));
    dprintf(event,"R %s %llu %llu %d\n",r.code==RESULT_CANCELLED ? "cancelled" : "other",
            (unsigned long long)r.completed_items,(unsigned long long)r.copied_bytes,r.partial);
}
Result __real_core_transfer_progress(bool,const char *,const char *,const char *,char **,OperationCallback,void *);
Result __wrap_core_transfer_progress(bool move,const char *src,const char *dir,const char *name,char **dst,OperationCallback cb,void *context) {
    Gate gate={.callback=cb,.context=context,.copy=true};
    Result r=__real_core_transfer_progress(move,src,dir,name,dst,gated_progress,&gate); result_event(r); return r;
}
Result __real_core_delete_progress(const char *,OperationCallback,void *);
Result __wrap_core_delete_progress(const char *path,OperationCallback cb,void *context) {
    Gate gate={.callback=cb,.context=context};
    Result r=__real_core_delete_progress(path,gated_progress,&gate); result_event(r); return r;
}
