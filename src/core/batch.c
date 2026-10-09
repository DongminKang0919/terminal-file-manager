#include "core.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void batch_free(BatchJob *job) {
    for(size_t i=0;i<job->len;i++) { free(job->targets[i].source); free(job->targets[i].name); free(job->targets[i].destination); }
    free(job->targets); free(job->directory); *job=(BatchJob){0};
}
Result batch_prepare(const AppState *app,size_t cursor,BatchAction action,BatchJob *out) {
    *out=(BatchJob){.action=action};
    if(action<BATCH_COPY || action>BATCH_TRASH) return result_make(RESULT_UNSUPPORTED,"Invalid batch action");
    size_t count=app->marks_len ? app->marks_len : cursor<app->files.len ? 1 : 0;
    if(!count) return result_make(RESULT_NOT_FOUND,"No targets");
    if(count>SIZE_MAX/sizeof *out->targets) return result_make(RESULT_NO_MEMORY,"Too many targets");
    out->targets=calloc(count,sizeof *out->targets);
    if(!out->targets) return result_make(RESULT_NO_MEMORY,"Out of memory");
    for(size_t i=0;i<app->files.len;i++) {
        const FileInfo *f=&app->files.entries[i];
        if(app->marks_len ? !app_marked(app,f->name) : i!=cursor) continue;
        if(out->len==count) { batch_free(out); return result_make(RESULT_IO,"Selection mismatch"); }
        BatchTarget *target=&out->targets[out->len++]; target->kind=f->kind;
        target->source=text_copy(f->path); target->name=text_copy(f->name);
        if(!target->source || !target->name) { batch_free(out); return result_make(RESULT_NO_MEMORY,"Out of memory"); }
    }
    if(out->len!=count) { batch_free(out); return result_make(RESULT_IO,"Selection mismatch"); }
    return result_make(RESULT_OK,NULL);
}
Result batch_destination(BatchJob *job,const char *directory) {
    if(job->executed) return result_make(RESULT_UNSUPPORTED,"Batch already executed");
    if(job->action==BATCH_TRASH) return result_make(RESULT_UNSUPPORTED,"Trash location is chosen per filesystem");
    char *copy=text_copy(directory);
    if(!copy) return result_make(RESULT_NO_MEMORY,"Out of memory");
    free(job->directory); job->directory=copy;
    return result_make(RESULT_OK,NULL);
}
void batch_cancel(BatchJob *job) {
    if(job->executed) return;
    job->result=result_make(RESULT_CANCELLED,"Cancelled before execution; no changes");
    /* No target was attempted. Keep every target unexecuted. */
}
static uint64_t sum(uint64_t a,uint64_t b) { return UINT64_MAX-a<b ? UINT64_MAX : a+b; }
typedef struct { BatchJob *job; BatchCallback callback; void *context; size_t index; } Run;
static bool progress(const OperationProgress *p,void *context) {
    Run *run=context;
    BatchProgress bp={.index=run->index,.total=run->job->len,.succeeded=run->job->succeeded,.skipped=run->job->skipped,.progress=*p};
    bp.progress.completed_items=sum(run->job->result.completed_items,p->completed_items);
    bp.progress.copied_bytes=sum(run->job->result.copied_bytes,p->copied_bytes);
    return !run->callback || run->callback(&bp,run->context);
}
Result batch_execute(BatchJob *job,BatchCallback callback,void *context) {
    if(job->executed) return result_make(RESULT_UNSUPPORTED,"Batch already executed");
    if(!job->len || ((job->action==BATCH_COPY||job->action==BATCH_MOVE) && !job->directory))
        return result_make(RESULT_NOT_DIRECTORY,"No batch destination");
    job->executed=true; job->result=result_make(RESULT_OK,NULL);
    Run run={job,callback,context,0}; bool changed=false,skip_all=false;
    for(size_t i=0;i<job->len;i++) {
        run.index=i; BatchTarget *target=&job->targets[i];
        OperationProgress before={.path=target->source};
        Result r;
        if(!progress(&before,&run)) r=result_make(RESULT_CANCELLED,i ? "Cancelled before next target; target not started" : "Cancelled before first target; no changes");
        else if(job->action==BATCH_DELETE) r=core_delete_progress(target->source,callback?progress:NULL,&run);
        else if(job->action==BATCH_TRASH) {
            char *destination=NULL;r=core_trash_progress(target->source,&destination,callback?progress:NULL,&run);target->destination=destination;
        }
        else {
            char *destination=NULL;
            r=core_transfer_progress(job->action==BATCH_MOVE,target->source,job->directory,target->name,
                                     &destination,callback?progress:NULL,&run);
            free(destination);
        }
        /* Only a no-change collision can be skipped. A recursive partial copy
           must stop even if its last error was EXISTS. No retry/overwrite. */
        if((job->action==BATCH_COPY||job->action==BATCH_MOVE) && r.code==RESULT_EXISTS && !r.partial) {
            target->result=r;
            BatchConflictDecision decision=skip_all?CONFLICT_SKIP:job->conflict?
                job->conflict(target,job->directory,job->conflict_context):CONFLICT_STOP;
            if(decision==CONFLICT_SKIP || decision==CONFLICT_SKIP_ALL) {
                skip_all|=decision==CONFLICT_SKIP_ALL;target->status=BATCH_SKIPPED;job->skipped++;continue;
            }
            if(decision==CONFLICT_CANCEL) r=result_make(RESULT_CANCELLED,"Cancelled at name collision; this target unchanged");
        }
        target->result=r;
        target->status=r.code==RESULT_OK ? BATCH_SUCCESS : r.code==RESULT_CANCELLED ? BATCH_CANCELLED : BATCH_FAILED;
        uint64_t items=sum(job->result.completed_items,r.completed_items),bytes=sum(job->result.copied_bytes,r.copied_bytes);
        changed|=r.code==RESULT_OK || r.partial;
        if(r.code==RESULT_OK) job->succeeded++;
        else job->result=r;
        job->result.completed_items=items; job->result.copied_bytes=bytes;
        if(r.code!=RESULT_OK) { job->result.partial=changed; break; }
    }
    if(job->result.code==RESULT_OK && job->skipped) snprintf(job->result.detail,sizeof job->result.detail,"Succeeded: %zu; skipped name collisions: %zu",job->succeeded,job->skipped);
    return job->result;
}
