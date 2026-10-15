#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
static bool active, fired;
static double last, gap;
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6; }
static void hold(const char *phase) {
    const char *want=getenv("TFILE_OPERATION_PHASE"),*path=getenv("TFILE_OPERATION_LOG");
    if(!active || fired || !want || strcmp(want,phase) || !path)return;
    fired=true;
    char marker[4096];snprintf(marker,sizeof marker,"%s.entered",path);
    FILE *f=fopen(marker,"w");if(f){fprintf(f,"%s %ld\n",phase,(long)getpid());fclose(f);}
    struct timespec ts={1,0};nanosleep(&ts,NULL);
}
ssize_t __real_read(int,void *,size_t);
ssize_t __wrap_read(int fd,void *b,size_t n){hold("read");return __real_read(fd,b,n);}
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *b,size_t n){hold("write");return __real_write(fd,b,n);}
int __real_fstatat(int,const char *,struct stat *,int);
int __wrap_fstatat(int fd,const char *p,struct stat *s,int flags){hold("metadata");return __real_fstatat(fd,p,s,flags);}
struct dirent *__real_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *d){hold("readdir");return __real_readdir(d);}
int __real_unlinkat(int,const char *,int);
int __wrap_unlinkat(int fd,const char *p,int flags){hold("unlink");return __real_unlinkat(fd,p,flags);}
typedef struct {OperationCallback cb;void *context;} Progress;
static bool progress(const OperationProgress *p,void *ctx) {
    double t=now();if(t-last>gap)gap=t-last;last=t;
    const char *phase=getenv("TFILE_OPERATION_PHASE"),*path=getenv("TFILE_OPERATION_LOG");
    if(!fired && phase && !strcmp(phase,"cancel") && path && (p->copied_bytes>=65536 || p->completed_items>=2)) {
        fired=true;char marker[4096];snprintf(marker,sizeof marker,"%s.entered",path);
        FILE *f=fopen(marker,"w");if(f){fprintf(f,"real-cancel %ld\n",(long)getpid());fclose(f);}
    }
    Progress *s=ctx;return !s->cb || s->cb(p,s->context);
}
static void record(Result r,double start) {
    active=false;const char *path=getenv("TFILE_OPERATION_LOG");if(!path)return;
    FILE *f=fopen(path,"w");if(f){fprintf(f,"%.3f %.3f %d %llu %llu %d\n",now()-start,gap,r.code,(unsigned long long)r.completed_items,(unsigned long long)r.copied_bytes,r.partial);fclose(f);}
}
Result __real_core_transfer_progress(bool,const char *,const char *,const char *,char **,OperationCallback,void *);
Result __wrap_core_transfer_progress(bool move,const char *s,const char *d,const char *n,char **out,OperationCallback cb,void *ctx) {
    Progress p={cb,ctx};double start=last=now();gap=0;fired=false;active=true;
    Result r=__real_core_transfer_progress(move,s,d,n,out,progress,&p);record(r,start);return r;
}
Result __real_core_delete_progress(const char *,OperationCallback,void *);
Result __wrap_core_delete_progress(const char *s,OperationCallback cb,void *ctx) {
    Progress p={cb,ctx};double start=last=now();gap=0;fired=false;active=true;
    Result r=__real_core_delete_progress(s,progress,&p);record(r,start);return r;
}

int __real_input_wide(WINDOW *,wint_t *);
int __wrap_input_wide(WINDOW *win,wint_t *key) {
    int kind=__real_input_wide(win,key);
    const char *path=getenv("TFILE_OPERATION_LOG");
    if(path && ((kind==OK && *key==27) || (kind==KEY_CODE_YES && (*key==KEY_F(10)||*key==KEY_RESIZE)))) {
        char marker[4096];snprintf(marker,sizeof marker,"%s.input",path);
        if(access(marker,F_OK)) { FILE *f=fopen(marker,"w");if(f){fprintf(f,"%.6f\n",now());fclose(f);} }
    }
    return kind;
}
