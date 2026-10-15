#include "../src/platform/platform.h"
#include <limits.h>
static pid_t gate_ui_pid;
Result __real_platform_mutation_open(PlatformTaskWork,void *,PlatformTask **);
Result __wrap_platform_mutation_open(PlatformTaskWork work,void *context,PlatformTask **out) {
    gate_ui_pid=getpid();return __real_platform_mutation_open(work,context,out);
}
static bool gate_fd(unsigned fd) {
    const char *a=getenv("TFILE_GATE_EVENT"),*b=getenv("TFILE_GATE_COMMAND");
    return getpid()!=gate_ui_pid && gate_ui_pid && ((a&&fd==(unsigned)atoi(a))||(b&&fd==(unsigned)atoi(b)));
}
int __real_close(int);
int __wrap_close(int fd) { return gate_fd((unsigned)fd)?0:__real_close(fd); }
int __real_close_range(unsigned,unsigned,int);
int __wrap_close_range(unsigned first,unsigned last,int flags) {
    if(!gate_ui_pid || getpid()==gate_ui_pid)return __real_close_range(first,last,flags);
    unsigned protected[2]={UINT_MAX,UINT_MAX};
    const char *a=getenv("TFILE_GATE_EVENT"),*b=getenv("TFILE_GATE_COMMAND");
    if(a)protected[0]=(unsigned)atoi(a);
    if(b)protected[1]=(unsigned)atoi(b);
    if(protected[0]>protected[1]) { unsigned t=protected[0];protected[0]=protected[1];protected[1]=t; }
    for(unsigned i=0;i<2;i++)if(protected[i]>=first && protected[i]<=last && protected[i]!=UINT_MAX) {
        if(protected[i]>first && __real_close_range(first,protected[i]-1,flags)<0)return -1;
        first=protected[i]+1;
    }
    return first<=last?__real_close_range(first,last,flags):0;
}

/* Hold the UI immediately before forwarding Cancel. This makes a premature
   acknowledgement observable without relying on how the scheduler runs. */
#include <time.h>
void __real_core_mutation_cancel(MutationJob *);
void __wrap_core_mutation_cancel(MutationJob *job) {
    static bool held;
    const char *path=getenv("TFILE_GATE_CANCEL");
    if(path && !held) {
        held=true;
        dprintf(atoi(getenv("TFILE_GATE_EVENT")),"C\n");
        while(access(path,F_OK)==0) {
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        }
    }
    __real_core_mutation_cancel(job);
}
