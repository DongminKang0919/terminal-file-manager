#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static pid_t parent;
static struct { volatile int mode,held,release,pid,writes; } *shared;
enum { NORMAL,HOLD_READ,HOLD_WRITE,HOLD_META,HOLD_DONE,DIE_SECOND,BREAK_TARGET,BAD_FRAME,DROP_TARGET,NO_SPACE,WRITE_ERROR,DENIED,EXDEV_MOVE };
static void tick(void) { struct timespec t={0,1000000};nanosleep(&t,NULL); }
static void hold(int mode) {
    if(getpid()==parent || shared->mode!=mode)return;
    shared->pid=getpid();shared->held=1;while(!shared->release)tick();
}
ssize_t __real_read(int,void *,size_t);
ssize_t __wrap_read(int fd,void *data,size_t n) { if(n==65536)hold(HOLD_READ);return __real_read(fd,data,n); }
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *data,size_t n) {
    struct stat st;
    if(getpid()!=parent && fd!=2 && !fstat(fd,&st) && S_ISREG(st.st_mode)) {
        hold(HOLD_WRITE);
        if(shared->mode==NO_SPACE || shared->mode==WRITE_ERROR) {
            if(shared->writes++) { errno=shared->mode==NO_SPACE?ENOSPC:EIO;return -1; }
            if(n>7)n=7;
        }
    }
    return __real_write(fd,data,n);
}
int __real_fstatat(int,const char *,struct stat *,int);
int __wrap_fstatat(int fd,const char *name,struct stat *st,int flags) {
    hold(HOLD_META);
    if(getpid()!=parent && shared->mode==DENIED) { errno=EACCES;return -1; }
    return __real_fstatat(fd,name,st,flags);
}
int __real_renameat2(int,const char *,int,const char *,unsigned);
int __wrap_renameat2(int a,const char *b,int c,const char *d,unsigned flags) {
    if(getpid()!=parent && shared->mode==EXDEV_MOVE) { errno=EXDEV;return -1; }
    return __real_renameat2(a,b,c,d,flags);
}
bool __real_platform_task_send(PlatformTaskWriter *,unsigned,const void *,size_t);
bool __wrap_platform_task_send(PlatformTaskWriter *writer,unsigned type,const void *data,size_t len) {
    if(getpid()!=parent) {
        if(type==4)hold(HOLD_DONE);
        if(type==1 && len==sizeof(MutationProgress) && ((const MutationProgress *)data)->index==1)hold(DIE_SECOND);
        if(type==2 && shared->mode==BREAK_TARGET)return false;
        if(type==2 && shared->mode==DROP_TARGET)return true;
        if(type==1 && shared->mode==BAD_FRAME)return __real_platform_task_send(writer,999,data,len);
    }
    return __real_platform_task_send(writer,type,data,len);
}
Result __real_platform_task_receive(PlatformTask *,unsigned *,void **,size_t *,bool *);
Result __wrap_platform_task_receive(PlatformTask *task,unsigned *type,void **data,size_t *len,bool *ready) {
    Result r=__real_platform_task_receive(task,type,data,len,ready);
    assert(!strstr(r.detail,"Sanitizer") && !strstr(r.detail,"runtime error:"));
    return r;
}
static void put(const char *path,size_t n) { FILE *f=fopen(path,"w");assert(f);for(size_t i=0;i<n;i++)assert(fputc('x',f)!=EOF);assert(!fclose(f)); }
static BatchJob request(BatchAction action,const char *root,const char *dst,size_t count) {
    BatchJob job={.action=action,.len=count,.directory=dst?text_copy(dst):NULL};job.targets=calloc(count,sizeof *job.targets);assert(job.targets);
    for(size_t i=0;i<count;i++) { char name[32];snprintf(name,sizeof name,"file-%zu",i);job.targets[i].name=text_copy(name);job.targets[i].source=core_path_join(root,name);assert(job.targets[i].source); }
    return job;
}
static void finish(MutationJob *job) {
    bool done=false;uint64_t end=core_monotonic_ms()+4000;
    while(!done) { (void)core_mutation_poll(job,&done);assert(core_monotonic_ms()<end);tick(); }
    core_mutation_close(job);
}
static void entered(MutationJob *job) {
    bool done=false;uint64_t end=core_monotonic_ms()+2000;
    while(!shared->held) { (void)core_mutation_poll(job,&done);assert(!done && core_monotonic_ms()<end);tick(); }
}
static unsigned descriptors(void) { DIR *d=opendir("/proc/self/fd");assert(d);unsigned n=0;while(readdir(d))n++;closedir(d);return n; }
static void reset(int mode) { memset((void *)shared,0,sizeof *shared);shared->mode=mode; }
static BatchConflictDecision collision(const BatchTarget *t,const char *d,void *c) { (void)t;(void)d;(void)c;assert(false);return CONFLICT_STOP; }
int main(void) {
    parent=getpid();shared=mmap(NULL,sizeof *shared,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);
    char root[]="/tmp/tfile-mutation-XXXXXX";assert(mkdtemp(root));char *src=core_path_join(root,"src"),*dst=core_path_join(root,"dst"),*trash=core_path_join(root,"data");assert(!mkdir(src,0700)&&!mkdir(dst,0700));assert(!setenv("XDG_DATA_HOME",trash,1));
    char *path=core_path_join(src,"file-0"),*output=core_path_join(dst,"file-0");unsigned base=descriptors();
    for(int phase=HOLD_READ;phase<=HOLD_DONE;phase++)for(int iteration=0;iteration<8;iteration++) {
        put(path,262144);reset(phase);BatchJob batch=request(BATCH_COPY,src,dst,1);MutationJob *job=NULL;
        assert(core_mutation_start(&batch,&job).code==RESULT_OK);entered(job);
        uint64_t at=core_monotonic_ms();core_mutation_cancel(job);assert(core_monotonic_ms()-at<50);
        bool done=false;for(int i=0;i<10;i++) { (void)core_mutation_poll(job,&done);assert(!done);tick(); }
        shared->release=1;finish(job);
        assert(batch.result.code==(phase==HOLD_DONE?RESULT_OK:RESULT_CANCELLED));
        struct stat st;assert(!stat(output,&st));
        if(phase==HOLD_READ || phase==HOLD_META)assert(st.st_size==0);
        if(phase==HOLD_WRITE)assert(st.st_size==65536 && batch.result.copied_bytes==65536);
        if(phase==HOLD_DONE)assert(st.st_size==262144);
        batch_free(&batch);assert(!unlink(path)&&!unlink(output));assert(descriptors()==base);assert(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD);
    }
    for(int mode=NO_SPACE;mode<=DENIED;mode++) {
        put(path,262144);reset(mode);BatchJob batch=request(BATCH_COPY,src,dst,1);MutationJob *job;
        assert(core_mutation_start(&batch,&job).code==RESULT_OK);finish(job);
        assert(batch.result.code==(mode==DENIED?RESULT_ACCESS:RESULT_IO));
        if(mode!=DENIED) { struct stat st;assert(!stat(output,&st)&&st.st_size==7&&batch.result.copied_bytes==7&&batch.result.partial); }
        batch_free(&batch);assert(!unlink(path));if(mode!=DENIED)assert(!unlink(output));
    }
    for(int mode=DIE_SECOND;mode<=DROP_TARGET;mode++) {
        put(path,65536);char *second=core_path_join(src,"file-1");put(second,65536);reset(mode);BatchJob batch=request(BATCH_COPY,src,dst,2);MutationJob *job;
        assert(core_mutation_start(&batch,&job).code==RESULT_OK);
        if(mode==DIE_SECOND) { entered(job);assert(!kill(shared->pid,SIGKILL)); }
        finish(job);assert(batch.result.code==RESULT_UNCERTAIN);
        assert(batch.targets[1].status==BATCH_UNKNOWN);
        assert(batch.targets[0].status==(mode==DIE_SECOND?BATCH_SUCCESS:BATCH_UNKNOWN));
        assert(batch.succeeded==(mode==DIE_SECOND?1u:0u));batch_free(&batch);reset(NORMAL);
        assert(platform_remove(src).code==RESULT_OK&&platform_remove(dst).code==RESULT_OK);assert(!mkdir(src,0700)&&!mkdir(dst,0700));free(second);
    }
    put(path,32);put(output,12);reset(NORMAL);BatchJob batch=request(BATCH_COPY,src,dst,1);batch.conflict=collision;MutationJob *job;
    assert(core_mutation_start(&batch,&job).code==RESULT_OK);bool done=false;size_t index;uint64_t deadline=core_monotonic_ms()+1000;
    while(!core_mutation_conflict(job,&index)) { core_mutation_poll(job,&done);assert(!done&&core_monotonic_ms()<deadline);tick(); }
    core_mutation_decide(job,CONFLICT_SKIP_ALL);finish(job);assert(batch.result.code==RESULT_OK&&batch.skipped==1&&batch.targets[0].status==BATCH_SKIPPED);batch_free(&batch);assert(!unlink(output));
    reset(EXDEV_MOVE);batch=request(BATCH_MOVE,src,dst,1);assert(core_mutation_start(&batch,&job).code==RESULT_OK);finish(job);assert(batch.result.code==RESULT_CROSS_DEVICE&&access(path,F_OK)==0&&access(output,F_OK)<0);batch_free(&batch);
    reset(NORMAL);batch=request(BATCH_MOVE,src,dst,1);assert(core_mutation_start(&batch,&job).code==RESULT_OK);finish(job);assert(batch.result.code==RESULT_OK&&access(path,F_OK)<0&&access(output,F_OK)==0);batch_free(&batch);assert(!unlink(output));
    put(path,17);batch=request(BATCH_TRASH,src,NULL,1);assert(core_mutation_start(&batch,&job).code==RESULT_OK);finish(job);assert(batch.result.code==RESULT_OK&&batch.targets[0].destination&&access(path,F_OK)<0&&access(batch.targets[0].destination,F_OK)==0);batch_free(&batch);
    /* Unexpected parent death: the orphan requests safe stopping, never SIGKILL. */
    assert(!prctl(PR_SET_CHILD_SUBREAPER,1));put(path,262144);reset(HOLD_READ);
    pid_t supervisor=fork();assert(supervisor>=0);
    if(!supervisor) { BatchJob orphan=request(BATCH_COPY,src,dst,1);MutationJob *running;assert(core_mutation_start(&orphan,&running).code==RESULT_OK);for(;;)pause(); }
    deadline=core_monotonic_ms()+2000;while(!shared->held) { tick();assert(core_monotonic_ms()<deadline); }
    pid_t worker=shared->pid;assert(!kill(supervisor,SIGKILL));assert(waitpid(supervisor,NULL,0)==supervisor);shared->release=1;
    int status;while(waitpid(worker,&status,WNOHANG)==0) { tick();assert(core_monotonic_ms()<deadline); }
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);struct stat st;assert(!stat(output,&st)&&st.st_size==0);
    reset(NORMAL);assert(platform_remove(root).code==RESULT_OK);free(src);free(dst);free(trash);free(path);free(output);munmap(shared,sizeof *shared);
    puts("PASS: 32 held-call cooperative cancellations, completion race, FD/child cleanup, ENOSPC/EIO partial writes, EACCES, confirmed/unknown crash and broken IPC, conflict decisions, EXDEV/no-replace move, Trash and unexpected parent death");
}
