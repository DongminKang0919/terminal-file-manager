#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool late,denied,partial;
static unsigned decisions;
static BatchConflictDecision decision;
static void put(const char *p,const char *s) { FILE *f=fopen(p,"wx");assert(f);fputs(s,f);fclose(f); }
static void keep(const char *p,const char *s) { char buf[32]={0};FILE *f=fopen(p,"r");assert(f);assert(fread(buf,1,sizeof buf-1,f)==strlen(s));fclose(f);assert(!strcmp(buf,s)); }
Result __real_platform_move(const char *,const char *);
Result __wrap_platform_move(const char *s,const char *d) {
    if(denied) return result_make(RESULT_ACCESS,"Injected access failure");
    if(late) { put(d,"late");late=false; }
    return __real_platform_move(s,d);
}
Result __real_platform_copy_progress(const char *,const char *,OperationCallback,void *);
Result __wrap_platform_copy_progress(const char *s,const char *d,OperationCallback cb,void *ctx) {
    if(partial) { Result r=result_make(RESULT_EXISTS,"Injected nested collision after partial writes");r.partial=true;r.copied_bytes=1;return r; }
    if(denied) return result_make(RESULT_ACCESS,"Injected access failure");
    if(late) { put(d,"late");late=false; }
    return __real_platform_copy_progress(s,d,cb,ctx);
}
static BatchConflictDecision collision(const BatchTarget *t,const char *d,void *ctx) {
    (void)d;(void)ctx;assert(t->result.code==RESULT_EXISTS&&!t->result.partial);decisions++;return decision;
}
static void ok(Result r) { if(r.code)fprintf(stderr,"%s\n",r.detail);assert(r.code==RESULT_OK); }
static void run(BatchAction action,BatchConflictDecision choice) {
    char root[]="/tmp/tfile-conflicts-XXXXXX";assert(mkdtemp(root));
    char *src=core_path_join(root,"source"),*dst=core_path_join(root,"destination");assert(!mkdir(src,0700)&&!mkdir(dst,0700));
    char *paths[3],*targets[3];const char *names[]={"a","b","c"};
    for(int i=0;i<3;i++) { paths[i]=core_path_join(src,names[i]);targets[i]=core_path_join(dst,names[i]);put(paths[i],"original"); }
    put(targets[0],"existing");AppState app;ok(app_init(&app,src));ok(app_mark_all(&app));
    BatchJob job;ok(batch_prepare(&app,0,action,&job));ok(batch_destination(&job,dst));job.conflict=collision;
    decisions=0;decision=choice;late=true;Result r=batch_execute(&job,NULL,NULL);late=false;
    keep(targets[0],"existing");keep(paths[0],"original");
    if(choice==CONFLICT_SKIP||choice==CONFLICT_SKIP_ALL) {
        assert(r.code==RESULT_OK&&job.skipped==2&&job.succeeded==1);
        assert(decisions==(choice==CONFLICT_SKIP_ALL?1:2));keep(targets[1],"late");keep(paths[1],"original");keep(targets[2],"original");
        assert(job.targets[0].status==BATCH_SKIPPED&&job.targets[1].status==BATCH_SKIPPED&&job.targets[2].status==BATCH_SUCCESS);
        app_marks_apply_result(&app,&job);assert(app.marks_len==2&&app_marked(&app,"a")&&app_marked(&app,"b"));
    } else {
        assert(job.skipped==0&&job.succeeded==0&&decisions==1);
        assert(job.targets[0].status==(choice==CONFLICT_CANCEL?BATCH_CANCELLED:BATCH_FAILED)&&job.targets[1].status==BATCH_UNEXECUTED);
        assert(r.code==(choice==CONFLICT_CANCEL?RESULT_CANCELLED:RESULT_EXISTS)&&!r.partial);
    }
    batch_free(&job);assert(!unlink(targets[0]));
    ok(app_refresh(&app));ok(app_mark_all(&app));ok(batch_prepare(&app,0,action,&job));ok(batch_destination(&job,dst));job.conflict=collision;
    decisions=0;denied=true;r=batch_execute(&job,NULL,NULL);denied=false;assert(r.code==RESULT_ACCESS&&!decisions&&job.targets[0].status==BATCH_FAILED);batch_free(&job);
    if(action==BATCH_COPY) {
        ok(batch_prepare(&app,0,action,&job));ok(batch_destination(&job,dst));job.conflict=collision;decisions=0;partial=true;
        r=batch_execute(&job,NULL,NULL);partial=false;assert(r.code==RESULT_EXISTS&&r.partial&&!decisions&&!job.skipped);batch_free(&job);
    }
    app_free(&app);for(int i=0;i<3;i++){free(paths[i]);free(targets[i]);}free(src);free(dst);ok(platform_remove(root));
}
int main(void) {
    for(int action=BATCH_COPY;action<=BATCH_MOVE;action++) for(int d=CONFLICT_STOP;d<=CONFLICT_CANCEL;d++) run(action,d);
    puts("PASS: copy/move preflight/late real no-clobber collisions, skip/skip-all/stop/cancel, skipped marks, error isolation and partial-copy refusal");
}
