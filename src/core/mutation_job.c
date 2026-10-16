#include "core.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MUT_PROGRESS=1,MUT_TARGET,MUT_CONFLICT,MUT_DONE };
typedef struct { size_t index,succeeded,skipped,destination; BatchStatus status; Result result,aggregate; } TargetWire;
typedef struct { Result result; size_t succeeded,skipped; } DoneWire;
typedef struct { PlatformTaskWriter *writer; BatchJob *batch; uint64_t sent; size_t index; bool cancelled,failed; } Work;
struct MutationJob {
    PlatformTask *task; BatchJob *batch; MutationProgress progress;
    bool cancelled,cancel_sent,wire_done,broken,done,conflict;
    unsigned decision;
    size_t conflict_index,confirmed;
};
static bool stopping(Work *work) {
    unsigned command=0;
    if(platform_mutation_control(work->writer,&command,false) && command==1)work->cancelled=true;
    return work->cancelled;
}
static bool progress(const BatchProgress *p,void *context) {
    Work *w=context;
    if(stopping(w))return false;
    uint64_t now=core_monotonic_ms();
    if(!w->sent || p->index!=w->index || now-w->sent>=100) {
        MutationProgress wire={p->index,p->total,p->succeeded,p->skipped,p->progress.completed_items,p->progress.copied_bytes,""};
        snprintf(wire.path,sizeof wire.path,"%s",p->progress.path?p->progress.path:"");
        if(!platform_task_send(w->writer,MUT_PROGRESS,&wire,sizeof wire))w->cancelled=w->failed=true;
        w->sent=now?now:1;w->index=p->index;
    }
    return !w->cancelled && !stopping(w);
}
static void report(size_t index,void *context) {
    Work *w=context;BatchTarget *t=&w->batch->targets[index];
    TargetWire wire={index,w->batch->succeeded,w->batch->skipped,t->destination?strlen(t->destination)+1:0,t->status,t->result,w->batch->result};
    size_t len=sizeof wire+wire.destination;
    if(len>1024u*1024u) { w->cancelled=w->failed=true;return; }
    unsigned char *data=malloc(len);
    if(!data) { w->cancelled=w->failed=true;return; }
    memcpy(data,&wire,sizeof wire);if(wire.destination)memcpy(data+sizeof wire,t->destination,wire.destination);
    if(!platform_task_send(w->writer,MUT_TARGET,data,len))w->cancelled=w->failed=true;
    free(data);
}
static BatchConflictDecision conflict(const BatchTarget *target,const char *directory,void *context) {
    (void)directory;Work *w=context;size_t index=(size_t)(target-w->batch->targets);
    if(stopping(w) || !platform_task_send(w->writer,MUT_CONFLICT,&index,sizeof index))return CONFLICT_CANCEL;
    for(;;) {
        unsigned command=0;
        if(platform_mutation_control(w->writer,&command,true)) {
            if(command==1) { w->cancelled=true;return CONFLICT_CANCEL; }
            if(command>=2 && command<=5)return (BatchConflictDecision)(command-2);
            w->cancelled=true;return CONFLICT_CANCEL;
        }
    }
}
static void mutation_work(PlatformTaskWriter *writer,void *context) {
    BatchJob *batch=context;Work w={.writer=writer,.batch=batch};
    if(batch->conflict) { batch->conflict=conflict;batch->conflict_context=&w; }
    batch->report=report;batch->report_context=&w;
    Result r=batch_execute(batch,progress,&w);
    DoneWire done={r,batch->succeeded,batch->skipped};
    if(!w.failed)(void)platform_task_send(writer,MUT_DONE,&done,sizeof done);
    /* Normal worker-owned allocations are released before the platform _exit.
       The inherited request is an owned fork snapshot, not shared UI memory. */
    batch_free(batch);
}
Result core_mutation_start(BatchJob *batch,MutationJob **out) {
    *out=NULL;
    if(batch->action<BATCH_COPY || batch->action>BATCH_CREATE_DIRECTORY || batch->executed || !batch->len || ((batch->action==BATCH_COPY || batch->action==BATCH_MOVE) && !batch->directory))
        return result_make(RESULT_INVALID_NAME,"Invalid frozen operation request; no changes");
    if(!platform_mutation_available())return result_make(RESULT_CANCELLED,"Previous operation is still finishing; no new work started");
    MutationJob *job=calloc(1,sizeof *job);if(!job)return result_make(RESULT_NO_MEMORY,"Out of memory; no changes");
    Result r=platform_mutation_open(mutation_work,batch,&job->task);
    if(r.code!=RESULT_OK) { free(job);return r; }
    batch->executed=true;batch->result=result_make(RESULT_OK,NULL);job->batch=batch;
    job->progress.total=batch->len;snprintf(job->progress.path,sizeof job->progress.path,"%s",batch->targets[0].source);
    *out=job;return r;
}
void core_mutation_cancel(MutationJob *job) {
    if(job->done)return;
    job->cancelled=true;
    if(job->broken)return;
    if(!job->cancel_sent) { unsigned command=1;job->cancel_sent=platform_task_request(job->task,&command,sizeof command); }
}
void core_mutation_decide(MutationJob *job,BatchConflictDecision decision) { job->conflict=false;job->decision=(unsigned)decision+2; }
bool core_mutation_conflict(const MutationJob *job,size_t *index) { *index=job->conflict_index;return job->conflict; }
const MutationProgress *core_mutation_progress(const MutationJob *job) { return &job->progress; }
static void uncertain(MutationJob *job) {
    if(job->broken)return;
    job->broken=true;job->conflict=false;platform_mutation_stop(job->task);
    Result r=result_make(RESULT_UNCERTAIN,"Worker result uncertain; inspect source and destination paths before retrying");r.partial=true;
    r.completed_items=job->batch->result.completed_items;r.copied_bytes=job->batch->result.copied_bytes;
    snprintf(r.path,sizeof r.path,"%s",job->progress.path);
    job->batch->result=r;
    for(size_t i=0;i<job->batch->len;i++)if(job->batch->targets[i].status==BATCH_UNEXECUTED) {
        job->batch->targets[i].status=BATCH_UNKNOWN;job->batch->targets[i].result=r;
    }
}
static bool target(MutationJob *job,const void *data,size_t len) {
    if(len<sizeof(TargetWire))return false;
    TargetWire w;memcpy(&w,data,sizeof w);
    if(w.index>=job->batch->len || w.index!=job->confirmed || w.status<BATCH_SUCCESS || w.status>BATCH_SKIPPED || w.destination!=len-sizeof w)return false;
    if(w.succeeded!=job->batch->succeeded+(w.status==BATCH_SUCCESS) || w.skipped!=job->batch->skipped+(w.status==BATCH_SKIPPED))return false;
    BatchTarget *t=&job->batch->targets[w.index];if(t->status!=BATCH_UNEXECUTED)return false;
    char *destination=NULL;
    if(w.destination) {
        const char *p=(const char *)data+sizeof w;if(p[w.destination-1])return false;
        destination=text_copy(p);if(!destination)return false;
    }
    t->destination=destination;t->result=w.result;t->status=w.status;job->confirmed++;
    job->batch->succeeded=w.succeeded;job->batch->skipped=w.skipped;job->batch->result=w.aggregate;
    return true;
}
Result core_mutation_poll(MutationJob *job,bool *done) {
    *done=job->done;if(job->done)return job->batch->result;
    if(job->cancelled && !job->cancel_sent)core_mutation_cancel(job);
    if(job->decision && !job->cancelled) {
        if(platform_task_request(job->task,&job->decision,sizeof job->decision))job->decision=0;
    }
    for(unsigned i=0;i<32 && !job->wire_done && !job->broken;i++) {
        unsigned type;void *data;size_t len;bool ready;
        Result r=platform_task_receive(job->task,&type,&data,&len,&ready);
        if(r.code!=RESULT_OK) { uncertain(job);break; }
        if(!ready)break;
        bool valid=true;
        if(type==MUT_PROGRESS && len==sizeof(MutationProgress)) {
            MutationProgress p;memcpy(&p,data,len);
            if(p.index>=job->batch->len || p.path[sizeof p.path-1])valid=false;else job->progress=p;
        } else if(type==MUT_TARGET)valid=target(job,data,len);
        else if(type==MUT_CONFLICT && len==sizeof(size_t)) {
            memcpy(&job->conflict_index,data,len);valid=job->conflict_index<job->batch->len;job->conflict=valid;
        } else if(type==MUT_DONE && len==sizeof(DoneWire)) {
            DoneWire w;memcpy(&w,data,len);
            valid=w.result.code>=RESULT_OK && w.result.code<=RESULT_CANCELLED &&
                w.succeeded==job->batch->succeeded && w.skipped==job->batch->skipped &&
                w.result.completed_items==job->batch->result.completed_items && w.result.copied_bytes==job->batch->result.copied_bytes &&
                (w.result.code!=RESULT_OK || job->confirmed==job->batch->len) && job->confirmed>0;
            if(valid) { job->batch->result=w.result;job->wire_done=true; }
        } else valid=false;
        free(data);if(!valid)uncertain(job);
    }
    if(job->wire_done || job->broken) {
        bool clean=false;
        if(platform_task_finished(job->task,&clean)) {
            if(!clean)uncertain(job);
            job->done=true;platform_task_close(job->task);job->task=NULL;
        }
    }
    *done=job->done;return job->batch->result;
}
void core_mutation_close(MutationJob *job) { if(job) { platform_task_close(job->task);free(job); } }
