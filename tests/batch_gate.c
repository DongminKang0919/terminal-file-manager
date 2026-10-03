#define _POSIX_C_SOURCE 200809L
#include "../src/core/core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct { BatchCallback callback;void *context;bool gated; } Gate;
static bool progress(const BatchProgress *p,void *context) {
    Gate *g=context;bool keep=g->callback(p,g->context);
    bool boundary=!strcmp(getenv("TFILE_BATCH_GATE"),"boundary");
    if(keep&&!g->gated&&(boundary ? p->index==1 : p->progress.copied_bytes>=65536||p->progress.completed_items>=1)) {
        g->gated=true;int event=atoi(getenv("TFILE_GATE_EVENT")),command=atoi(getenv("TFILE_GATE_COMMAND"));
        assert(write(event,"G\n",2)==2);char ch;ssize_t n;do{n=read(command,&ch,1);}while(n<0&&errno==EINTR);assert(n==1);
        keep=g->callback(p,g->context);
    }
    return keep;
}
Result __real_batch_execute(BatchJob *,BatchCallback,void *);
Result __wrap_batch_execute(BatchJob *job,BatchCallback cb,void *ctx) {
    Gate gate={cb,ctx,false};Result r=__real_batch_execute(job,progress,&gate);
    dprintf(atoi(getenv("TFILE_GATE_EVENT")),"R %d %d %zu %llu %llu %d %d %d\n",r.code,r.partial,job->succeeded,
            (unsigned long long)r.completed_items,(unsigned long long)r.copied_bytes,
            job->targets[0].status,job->targets[1].status,job->targets[2].status);
    return r;
}
