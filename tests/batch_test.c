#define _GNU_SOURCE
#include "../src/core/core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

static size_t calls,fail_at;
static bool cross_device;
Result __real_platform_move(const char *,const char *);
Result __wrap_platform_move(const char *a,const char *b) {
    return cross_device ? result_make(RESULT_CROSS_DEVICE,"Injected EXDEV at native move boundary") : __real_platform_move(a,b);
}
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n) { return ++calls==fail_at?NULL:__real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { return ++calls==fail_at?NULL:__real_calloc(n,s); }
void *__wrap_realloc(void *p,size_t n) { return ++calls==fail_at?NULL:__real_realloc(p,n); }
static void ok(Result r) { if(r.code) fprintf(stderr,"%d %s\n",r.code,r.detail);assert(r.code==RESULT_OK); }
static size_t fds(void) { DIR *d=opendir("/proc/self/fd");assert(d);size_t n=0;while(readdir(d))n++;closedir(d);return n; }
static char *join(const char *p,const char *n) { char *s=core_path_join(p,n);assert(s);return s; }
static void put(const char *p,size_t n) { FILE *f=fopen(p,"w");assert(f);for(size_t i=0;i<n;i++)assert(fputc('x',f)!=EOF);assert(!fclose(f)); }
typedef struct { size_t stop; uint64_t bytes,items,last_bytes,last_items; } Stop;
static bool progress(const BatchProgress *p,void *ctx) {
    Stop *s=ctx;assert(p->index<p->total);assert(p->progress.copied_bytes>=s->last_bytes);assert(p->progress.completed_items>=s->last_items);
    s->last_bytes=p->progress.copied_bytes;s->last_items=p->progress.completed_items;
    return p->index<s->stop&&p->progress.copied_bytes<s->bytes&&p->progress.completed_items<s->items;
}
static Stop unlimited(void) { return (Stop){.stop=SIZE_MAX,.bytes=UINT64_MAX,.items=UINT64_MAX}; }
int main(void) {
    char root[]="/tmp/tfile-batch-XXXXXX";assert(mkdtemp(root));
    char *src=join(root,"src"),*dst=join(root,"dst");assert(!mkdir(src,0700)&&!mkdir(dst,0700));
    const char *names[]={"a한글\n\xff","b\t","c"};char *paths[3];
    for(size_t i=0;i<3;i++) { paths[i]=join(src,names[i]);put(paths[i],8); }
    AppState app;ok(app_init(&app,src));
    BatchJob job;ok(batch_prepare(&app,1,BATCH_COPY,&job));assert(job.len==1&&!strcmp(job.targets[0].source,paths[1]));batch_free(&job);
    ok(app_mark_toggle(&app,names[0]));assert(app.marks_len==1);ok(app_mark_toggle(&app,names[0]));assert(!app.marks_len);
    ok(app_mark_all(&app));size_t selected=0;app_set_sort(&app,(SortSettings){SORT_NAME,true},&selected);assert(app.marks_len==3);
    ok(batch_prepare(&app,0,BATCH_COPY,&job));assert(!strcmp(job.targets[0].name,"c"));
    app_set_sort(&app,(SortSettings){SORT_NAME,false},&selected);assert(!strcmp(job.targets[0].name,"c"));batch_free(&job);
    char *hidden=join(src,".hidden"),*fresh=join(src,"new");put(hidden,1);put(fresh,1);ok(app_refresh(&app));assert(app.marks_len==3&&!app_marked(&app,"new"));
    ok(app_mark_toggle(&app,".hidden"));app.show_hidden=false;ok(app_refresh(&app));assert(app.marks_len==3&&!app_marked(&app,".hidden"));
    assert(!unlink(hidden)&&!unlink(fresh));free(hidden);free(fresh);ok(app_refresh(&app));
    for(size_t i=1;i<=4;i++) {
        calls=0;fail_at=i;Result r=app_mark_all(&app);fail_at=0;
        assert(r.code==RESULT_NO_MEMORY&&app.marks_len==3);
        for(size_t j=0;j<3;j++)assert(app_marked(&app,names[j]));
    }
    /* Every required plan allocation failure leaves source data unchanged. */
    for(size_t i=1;i<=7;i++) { calls=0;fail_at=i;Result r=batch_prepare(&app,0,BATCH_COPY,&job);fail_at=0;assert(r.code==RESULT_NO_MEMORY&&!job.targets);for(size_t j=0;j<3;j++)assert(!access(paths[j],F_OK)); }
    ok(batch_prepare(&app,0,BATCH_COPY,&job));calls=0;fail_at=1;assert(batch_destination(&job,dst).code==RESULT_NO_MEMORY&&!job.directory);fail_at=0;batch_free(&job);
    size_t before=fds();
    for(int action=BATCH_COPY;action<=BATCH_DELETE;action++) {
        for(int mode=0;mode<5;mode++) {
            for(size_t i=0;i<3;i++) if(access(paths[i],F_OK))put(paths[i],8);
            ok(app_refresh(&app));ok(app_mark_all(&app));ok(batch_prepare(&app,0,action,&job));if(action!=BATCH_DELETE)ok(batch_destination(&job,dst));
            Stop stop=unlimited();if(mode==1)stop.stop=0;if(mode==2)stop.stop=1;
            if(mode==3||mode==4) {
                size_t at=mode==3?0:1;
                if(action==BATCH_DELETE)assert(!unlink(paths[at]));else { char *p=join(dst,names[at]);put(p,1);free(p); }
            }
            Result r=batch_execute(&job,progress,&stop);
            if(mode==0) { assert(r.code==RESULT_OK&&job.succeeded==3);for(size_t i=0;i<3;i++)assert(job.targets[i].status==BATCH_SUCCESS); }
            else if(mode==1||mode==2) { assert(r.code==RESULT_CANCELLED&&job.succeeded==(size_t)(mode==2)&&r.partial==(mode==2));assert(job.targets[mode==2].status==BATCH_CANCELLED); }
            else { size_t at=mode==3?0:1;assert(r.code!=RESULT_OK&&r.code!=RESULT_CANCELLED&&job.succeeded==at&&r.partial==(at!=0));assert(job.targets[at].status==BATCH_FAILED); }
            if(mode!=0) { size_t last=(mode==2||mode==4)?1:0;for(size_t i=last+1;i<3;i++)assert(job.targets[i].status==BATCH_UNEXECUTED); }
            for(size_t i=0;i<3;i++) {
                bool done=job.targets[i].status==BATCH_SUCCESS;
                if(action!=BATCH_COPY&&done)assert(access(paths[i],F_OK));
                if(action!=BATCH_DELETE&&done) { char *p=join(dst,names[i]);struct stat st;assert(!stat(p,&st)&&st.st_size==8);free(p); }
            }
            batch_free(&job);assert(fds()==before);
            if(action!=BATCH_DELETE) { ok(core_delete(dst));assert(!mkdir(dst,0700)); }
        }
    }
    /* Recursive copy stops after a write; byte count and residue are exact. */
    for(size_t i=0;i<3;i++) if(access(paths[i],F_OK))put(paths[i],8);
    put(paths[0],262144);ok(app_refresh(&app));ok(app_mark_all(&app));ok(batch_prepare(&app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));Stop stop=unlimited();stop.bytes=65536;
    Result r=batch_execute(&job,progress,&stop);assert(r.code==RESULT_CANCELLED&&r.partial&&r.copied_bytes==65536&&job.targets[0].result.partial);assert(job.targets[1].status==BATCH_UNEXECUTED);batch_free(&job);ok(core_delete(dst));assert(!mkdir(dst,0700));
    /* Native EXDEV is reported without copy/delete fallback. */
    ok(batch_prepare(&app,0,BATCH_MOVE,&job));ok(batch_destination(&job,dst));cross_device=true;
    r=batch_execute(&job,NULL,NULL);cross_device=false;
    assert(r.code==RESULT_CROSS_DEVICE&&!r.partial&&job.targets[0].status==BATCH_FAILED&&job.targets[1].status==BATCH_UNEXECUTED);
    for(size_t i=0;i<3;i++)assert(!access(paths[i],F_OK));
    batch_free(&job);
    ok(batch_prepare(&app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));assert(!unlink(paths[1]));
    r=batch_execute(&job,NULL,NULL);assert(r.code==RESULT_NOT_FOUND&&r.partial&&job.succeeded==1&&job.targets[2].status==BATCH_UNEXECUTED);batch_free(&job);put(paths[1],8);ok(core_delete(dst));assert(!mkdir(dst,0700));
    /* Recursive copy/delete cancellation preserves parent and remaining targets. */
    char *rec=join(root,"recursive"),*tree=join(rec,"a-tree"),*later=join(rec,"b-later"),*child=join(tree,"chunk");
    assert(!mkdir(rec,0700)&&!mkdir(tree,0700));put(child,262144);put(later,8);
    AppState recursive;ok(app_init(&recursive,rec));ok(app_mark_all(&recursive));
    for(int action=BATCH_COPY;action<=BATCH_DELETE;action+=2) {
        ok(batch_prepare(&recursive,0,action,&job));if(action==BATCH_COPY)ok(batch_destination(&job,dst));
        stop=unlimited();if(action==BATCH_COPY)stop.bytes=65536;else stop.items=1;
        r=batch_execute(&job,progress,&stop);assert(r.code==RESULT_CANCELLED&&r.partial&&job.targets[0].result.partial&&job.targets[1].status==BATCH_UNEXECUTED);
        assert(!access(tree,F_OK)&&!access(later,F_OK));
        if(action==BATCH_COPY) { assert(r.copied_bytes==65536);ok(core_delete(dst));assert(!mkdir(dst,0700)); }
        else { assert(r.completed_items==1&&access(child,F_OK)); }
        batch_free(&job);assert(fds()==before);
    }
    app_free(&recursive);ok(core_delete(rec));free(rec);free(tree);free(later);free(child);
    /* Mixed target kinds retain link bytes without traversing the link. */
    char *mixed=join(root,"mixed"),*folder=join(mixed,"dir"),*leaf=join(folder,"leaf"),*link=join(mixed,"link"),*file=join(mixed,"file");
    assert(!mkdir(mixed,0700)&&!mkdir(folder,0700));put(leaf,3);put(file,2);assert(!symlink(paths[0],link));
    AppState mixed_app;ok(app_init(&mixed_app,mixed));ok(app_mark_all(&mixed_app));ok(batch_prepare(&mixed_app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));
    r=batch_execute(&job,NULL,NULL);assert(r.code==RESULT_OK&&job.succeeded==3);
    char *copied_link=join(dst,"link");struct stat st;assert(!lstat(copied_link,&st)&&S_ISLNK(st.st_mode));
    char target[1024];ssize_t n=readlink(copied_link,target,sizeof target-1);assert(n>0);target[n]=0;assert(!strcmp(target,paths[0]));
    app_marks_apply_result(&mixed_app,&job);assert(!mixed_app.marks_len);batch_free(&job);app_free(&mixed_app);free(copied_link);ok(core_delete(dst));assert(!access(paths[0],F_OK));assert(!mkdir(dst,0700));
    ok(core_delete(mixed));free(mixed);free(folder);free(leaf);free(link);free(file);
    for(int cycle=0;cycle<100;cycle++) {
        ok(batch_prepare(&app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));ok(batch_execute(&job,NULL,NULL));batch_free(&job);
        ok(batch_prepare(&app,0,BATCH_COPY,&job));ok(batch_destination(&job,dst));assert(batch_execute(&job,NULL,NULL).code==RESULT_EXISTS);batch_free(&job);
        ok(batch_prepare(&app,0,BATCH_DELETE,&job));stop=unlimited();stop.stop=0;assert(batch_execute(&job,progress,&stop).code==RESULT_CANCELLED);batch_free(&job);
        ok(core_delete(dst));assert(!mkdir(dst,0700));assert(fds()==before);
    }
    /* Cancelling confirmation is distinct: no target attempted, no refresh implied. */
    ok(batch_prepare(&app,0,BATCH_DELETE,&job));batch_cancel(&job);assert(job.result.code==RESULT_CANCELLED&&!job.executed&&!job.result.partial);for(size_t i=0;i<3;i++)assert(job.targets[i].status==BATCH_UNEXECUTED);batch_free(&job);
    ok(app_navigate(&app,dst));assert(!app.marks_len);ok(app_history(&app,false));assert(!app.marks_len);
    app_free(&app);for(size_t i=0;i<3;i++)free(paths[i]);free(src);free(dst);ok(core_delete(root));assert(fds()==before);
    puts("PASS: raw-byte marks/snapshot/order/filter/history, allocation failure, copy/move/delete success/first-middle error/pre-between cancellation, partial writes and FD cleanup");
}
