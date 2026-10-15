#define _GNU_SOURCE
/* Execute the actual worker body to its normal return, so leak checking does
   not silently stop at the fork wrapper's _exit. This is a teardown harness,
   separate from the real-process cancellation/IPC tests. */
#include "../src/core/preview_job.c"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
static unsigned states,done_count,commands;
static PreviewReply state;
bool __wrap_platform_task_send(PlatformTaskWriter *writer,unsigned type,const void *data,size_t len) {
    (void)writer;
    if(type==PREVIEW_STATE) { assert(len==sizeof state);memcpy(&state,data,len);states++; }
    else if(type==PREVIEW_DONE) { assert(!len);done_count++; }
    else assert((type==PREVIEW_LINE || type==PREVIEW_LINK) && len && !((const char *)data)[len-1]);
    return true;
}
bool __wrap_platform_task_command(PlatformTaskWriter *writer,void *data,size_t len) {
    (void)writer;assert(len==sizeof(PreviewRequest));
    if(commands++>=3)return false;
    PreviewRequest request={commands+1,commands==1?200:commands==2?190:1800,18};memcpy(data,&request,len);return true;
}
static void run(const char *path,FileKind kind,ResultCode expected) {
    states=done_count=commands=0;Work work={path,kind,{1,0,18}};
    preview_work(NULL,&work);
    assert(states==4 && done_count==4 && state.result.code==expected);
}
int main(void) {
    char root[]="/tmp/tfile-preview-worker-XXXXXX";assert(mkdtemp(root));char path[256];snprintf(path,sizeof path,"%s/file",root);
    FILE *f=fopen(path,"w");assert(f);for(unsigned i=0;i<2000;i++)fprintf(f,"line %u\n",i);assert(!fclose(f));
    for(unsigned i=0;i<20;i++)run(path,FILE_REGULAR,RESULT_OK);
    run(root,FILE_DIRECTORY,RESULT_OK);assert(!state.directory_empty);
    assert(!unlink(path));run(path,FILE_REGULAR,RESULT_NOT_FOUND);run(root,FILE_DIRECTORY,RESULT_OK);assert(state.directory_empty);
    assert(!rmdir(root));puts("PASS: worker body normal teardown, forward/backward/distant cache pages, directory/error cleanup (in-process harness)");
}
