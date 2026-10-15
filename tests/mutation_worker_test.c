#define _GNU_SOURCE
/* Run the actual mutation worker body through normal teardown, without _exit,
   separately from the real fork/IPC/death tests. Suitable for standalone LSan. */
#include "../src/core/mutation_job.c"
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
static unsigned targets,finished;
static bool cancel;
bool __wrap_platform_mutation_control(PlatformTaskWriter *writer,unsigned *command,bool wait) {
    (void)writer;(void)wait;*command=cancel?1:0;return cancel;
}
bool __wrap_platform_task_send(PlatformTaskWriter *writer,unsigned type,const void *data,size_t len) {
    (void)writer;(void)data;(void)len;if(type==MUT_TARGET)targets++;if(type==MUT_DONE)finished++;return true;
}
static void run(BatchAction action,const char *source,const char *directory,bool stopped) {
    BatchJob batch={.action=action,.len=1,.directory=directory?text_copy(directory):NULL};
    batch.targets=calloc(1,sizeof *batch.targets);assert(batch.targets);
    batch.targets[0].source=text_copy(source);batch.targets[0].name=core_path_name(source);assert(batch.targets[0].source&&batch.targets[0].name);
    cancel=stopped;targets=finished=0;mutation_work(NULL,&batch);
    assert(!batch.targets && !batch.directory && targets==1 && finished==1);
}
int main(void) {
    char root[]="/tmp/tfile-mutation-body-XXXXXX";assert(mkdtemp(root));char *a=core_path_join(root,"src"),*b=core_path_join(root,"dst");assert(!mkdir(a,0700)&&!mkdir(b,0700));
    char *source=core_path_join(a,"file"),*dest=core_path_join(b,"file");
    for(unsigned i=0;i<20;i++) {
        FILE *f=fopen(source,"w");assert(f);fputs("worker payload\n",f);assert(!fclose(f));
        run(BATCH_COPY,source,b,false);assert(!unlink(dest));
        run(BATCH_COPY,source,b,true);assert(access(dest,F_OK)<0);
        run(BATCH_MOVE,source,b,false);assert(!unlink(dest));
    }
    assert(platform_remove(root).code==RESULT_OK);free(a);free(b);free(source);free(dest);
    puts("PASS: actual mutation worker body normal/cancelled teardown, owned paths, results and progress (in-process harness)");
}
