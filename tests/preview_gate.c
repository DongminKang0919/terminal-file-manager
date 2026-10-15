#define _GNU_SOURCE
#include "../src/platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static void hold(const char *phase) {
    const char *wanted=getenv("TFILE_PREVIEW_PHASE"), *gate=getenv("TFILE_PREVIEW_GATE");
    if (!wanted || strcmp(wanted,phase) || !gate || access(gate,F_OK)) return;
    char marker[4096]; snprintf(marker,sizeof marker,"%s.entered",gate);
    FILE *f=fopen(marker,"w"); if(f) { fprintf(f,"%ld",(long)getpid()); fclose(f); }
    struct timespec pause={1,0}; nanosleep(&pause,NULL);
}
Result __real_platform_reader_open(const char *,PlatformReader **);
Result __wrap_platform_reader_open(const char *p,PlatformReader **r) { hold("open");return __real_platform_reader_open(p,r); }
Result __real_platform_reader_peek(PlatformReader *,unsigned char *,size_t,size_t *);
Result __wrap_platform_reader_peek(PlatformReader *r,unsigned char *b,size_t n,size_t *out) { hold("peek");return __real_platform_reader_peek(r,b,n,out); }
Result __real_platform_reader_line(PlatformReader *,char *,size_t,bool *);
Result __wrap_platform_reader_line(PlatformReader *r,char *b,size_t n,bool *end) { hold("line");return __real_platform_reader_line(r,b,n,end); }
Result __real_platform_reader_changed(PlatformReader *,const char *,bool *);
Result __wrap_platform_reader_changed(PlatformReader *r,const char *p,bool *c) { hold("metadata");return __real_platform_reader_changed(r,p,c); }
Result __real_platform_directory_empty(const char *,bool *);
Result __wrap_platform_directory_empty(const char *p,bool *e) { hold("directory");return __real_platform_directory_empty(p,e); }
