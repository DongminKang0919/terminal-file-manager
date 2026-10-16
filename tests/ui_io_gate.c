#define _GNU_SOURCE
#include "../src/platform/platform.h"
#ifndef TFILE_IO_BASELINE
#include "../src/core/core.h"
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool media_call;
static PlatformDirectory *startup_directory;
static char startup_path[4096];
static unsigned gate_calls;
int __real_open(const char *,int,...);
int __real_access(const char *,int);
static void hold(const char *phase,const char *path) {
    const char *wanted=getenv("TFILE_IO_PHASE"),*gate=getenv("TFILE_IO_GATE"),*match=getenv("TFILE_IO_MATCH");
    if(!wanted||strcmp(wanted,phase)||!gate||!match||!strstr(path,match)||__real_access(gate,F_OK))return;
    const char *skip=getenv("TFILE_IO_SKIP");
    if(skip && gate_calls++<(unsigned)strtoul(skip,NULL,10))return;
    char marker[4096],release[4096];snprintf(marker,sizeof marker,"%s.entered",gate);snprintf(release,sizeof release,"%s.release",gate);
    /* Register the FIFO reader before publishing entry. O_RDWR also keeps
       EOF from releasing the gate before the controller writes its byte. */
    int fd=__real_open(release,O_RDWR);if(fd<0)return;
    FILE *f=fopen(marker,"w");if(f) { fprintf(f,"%ld %s\n",(long)getpid(),phase);fclose(f); }
    char c;ssize_t n=read(fd,&c,1);if(n<0)perror("gate read");close(fd);
}
Result __real_core_resolve_directory(const char *,const char *,char **);
Result __wrap_core_resolve_directory(const char *b,const char *p,char **o) { hold("resolve",p);Result r=__real_core_resolve_directory(b,p,o);hold("resolve-after",p);return r; }
Result __real_platform_resolve(const char *,const char *,char **);
Result __wrap_platform_resolve(const char *b,const char *p,char **o) { hold("startup-resolve",p);return __real_platform_resolve(b,p,o); }
Result __real_platform_directory_open(const char *,PlatformDirectory **);
Result __wrap_platform_directory_open(const char *p,PlatformDirectory **o) {
    hold("browse",p);Result r=__real_platform_directory_open(p,o);
    if(r.code==RESULT_OK) { startup_directory=*o;snprintf(startup_path,sizeof startup_path,"%s",p); }
    return r;
}
Result __real_platform_directory_next(PlatformDirectory *,FileInfo *,bool *,Result *);
Result __wrap_platform_directory_next(PlatformDirectory *d,FileInfo *f,bool *end,Result *m) {
    if(d==startup_directory)hold("entries",startup_path);
    return __real_platform_directory_next(d,f,end,m);
}
Result __real_platform_create(const char *,bool);
Result __wrap_platform_create(const char *p,bool d) { hold("create-before",p);Result r=__real_platform_create(p,d);hold("create-after",p);return r; }
Result __real_platform_media_open(const char *,PreviewMediaKind,bool,unsigned,unsigned,PlatformMedia **);
Result __wrap_platform_media_open(const char *p,PreviewMediaKind k,bool t,unsigned w,unsigned h,PlatformMedia **o) {
    media_call=true;Result r=__real_platform_media_open(p,k,t,w,h,o);media_call=false;return r;
}
int __wrap_open(const char *p,int flags,...) {
    mode_t mode=0;if(flags&O_CREAT) { va_list a;va_start(a,flags);mode=(mode_t)va_arg(a,int);va_end(a); }
    if(media_call)hold("media-source",p);
    return __real_open(p,flags,mode);
}
int __wrap_access(const char *p,int mode) { if(mode==X_OK)hold("tools",p);return __real_access(p,mode); }

#ifndef TFILE_IO_BASELINE
Result __real_platform_create_progress(const char *,bool,OperationCallback,void *);
Result __wrap_platform_create_progress(const char *p,bool d,OperationCallback cb,void *ctx) {
    hold("create-before",p);Result r=__real_platform_create_progress(p,d,cb,ctx);hold("create-after",p);return r;
}
ssize_t __real_pwrite(int,const void *,size_t,off_t);
ssize_t __wrap_pwrite(int fd,const void *data,size_t len,off_t offset) {
    const char *match=getenv("TFILE_IO_MATCH");
    if(media_call && len==sizeof(Result) && match)hold("media-result",match);
    return __real_pwrite(fd,data,len,offset);
}

Result __real_core_read_poll(ReadJob *,bool *);
Result __wrap_core_read_poll(ReadJob *job,bool *done) {
    Result r=__real_core_read_poll(job,done);const char *match=getenv("TFILE_IO_MATCH");
    if(*done && match)hold("validation-ready",match);
    return r;
}
#endif /* TFILE_IO_BASELINE */
