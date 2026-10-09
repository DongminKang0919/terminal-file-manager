#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
static char *virtual_top,*swap_parent,*swap_old,*decoy;
static int mode, sync_failure=-1;
static bool fail_write,fail_remove,unknown_fs;
enum { NORMAL, WRITE_FAIL, READY_CANCEL, LATE_COLLISION, POST_SYNC, PARENT_SWAP, LEAF_SWAP, CRASH, ORPHAN, PRE_SYNC };
int __real_fsync(int);ssize_t __real_write(int,const void *,size_t);int __real_unlinkat(int,const char *,int);int __real_fstatfs(int,struct statfs *);
int __wrap_fsync(int fd) {if(fd==sync_failure){errno=EIO;return -1;}return __real_fsync(fd);}
ssize_t __wrap_write(int fd,const void *p,size_t n) {if(fail_write){errno=ENOSPC;return -1;}return __real_write(fd,p,n);}
int __wrap_unlinkat(int fd,const char *n,int f) {if(fail_remove){errno=EACCES;return -1;}return __real_unlinkat(fd,n,f);}
int __wrap_fstatfs(int fd,struct statfs *s) {int r=__real_fstatfs(fd,s);if(!r&&unknown_fs)s->f_type=0;return r;}
int platform_trash_test_top(int parent,char **path) {(void)parent;if(!virtual_top)return -1;*path=text_copy(virtual_top);assert(*path);int fd=open(virtual_top,O_RDONLY|O_DIRECTORY);assert(fd>=0);return fd;}
void platform_trash_test_hook(const char *stage,int parent,const char *name) {
    if(!strcmp(stage,"trash-write")) {if(mode==WRITE_FAIL||mode==ORPHAN)fail_write=true;if(mode==ORPHAN)fail_remove=true;if(mode==PRE_SYNC)sync_failure=parent;}
    if(!strcmp(stage,"trash-ready")) {
        if(mode==CRASH)_exit(77);
        if(mode==PARENT_SWAP){assert(!rename(swap_parent,swap_old));assert(!symlink(decoy,swap_parent));}
        if(mode==LEAF_SWAP){assert(!renameat(parent,name,parent,"saved-original"));int f=openat(parent,name,O_WRONLY|O_CREAT|O_EXCL,0600);assert(f>=0);close(f);}
    }
    if(!strcmp(stage,"trash-rename")&&mode==LATE_COLLISION){int f=openat(parent,name,O_WRONLY|O_CREAT|O_EXCL,0600);assert(f>=0);close(f);}
    if(!strcmp(stage,"trash-moved")&&mode==POST_SYNC)sync_failure=parent;
}
static void ok(Result r) {if(r.code)fprintf(stderr,"Unexpected: %d %s\n",r.code,r.detail);assert(r.code==RESULT_OK);}
static void put(const char *p) {int f=open(p,O_CREAT|O_EXCL|O_WRONLY,0640);assert(f>=0);assert(write(f,"payload",7)==7);assert(!close(f));}
static int count(const char *path) {DIR *d=opendir(path);if(!d)return 0;int n=0;struct dirent *e;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))n++;closedir(d);return n;}
static char *read_file(const char *p) {FILE *f=fopen(p,"r");assert(f);char b[16384];size_t n=fread(b,1,sizeof b-1,f);assert(!ferror(f));b[n]=0;fclose(f);return text_copy(b);}
static char *info_for(const char *target) {char *leaf=core_path_name(target),*files=core_path_parent(target),*trash=core_path_parent(files),*info=core_path_join(trash,"info");char *name=malloc(strlen(leaf)+11);assert(name);sprintf(name,"%s.trashinfo",leaf);char *out=core_path_join(info,name);free(leaf);free(files);free(trash);free(info);free(name);return out;}
static bool callback(const OperationProgress *p,void *ctx) {(void)p;int *calls=ctx;(*calls)++;return mode!=READY_CANCEL||*calls<2;}
static Result trash(const char *p,char **to) {int calls=0;return core_trash_progress(p,to,callback,&calls);}
static void reset(void) {mode=NORMAL;fail_write=fail_remove=unknown_fs=false;sync_failure=-1;}
static int fds(void) {return count("/proc/self/fd");}
int main(void) {
    int initial=fds();char root[]="/tmp/tfile-trash-XXXXXX";assert(mkdtemp(root));
    char *data=core_path_join(root,"data"),*src=core_path_join(root,"source"),*home=core_path_join(data,"Trash"),*infos=core_path_join(home,"info"),*files=core_path_join(home,"files");assert(!mkdir(src,0700));assert(!setenv("XDG_DATA_HOME",data,1));
    char *p=core_path_join(src,"-한글 %\n.txt"),*to=NULL;put(p);struct stat before,after;assert(!lstat(p,&before));ok(trash(p,&to));assert(access(p,F_OK)<0&&to);assert(!lstat(to,&after)&&before.st_ino==after.st_ino&&(after.st_mode&0777)==0640);char *info=info_for(to),*content=read_file(info);assert(strstr(content,"[Trash Info]\nPath=/")&&strstr(content,"%ED%95%9C%EA%B8%80%20%25%0A.txt\nDeletionDate="));struct tm date={0};char *stamp=strstr(content,"DeletionDate=")+13;assert(strptime(stamp,"%Y-%m-%dT%H:%M:%S",&date));free(content);free(info);free(to);to=NULL;
    put(p);ok(trash(p,&to));assert(count(files)==2&&count(infos)==2);free(to);to=NULL;
    char *tree=core_path_join(src,"tree"),*nested=core_path_join(tree,"nested");assert(!mkdir(tree,0700));put(nested);ok(trash(tree,&to));char *saved=core_path_join(to,"nested");assert(!access(saved,F_OK));free(saved);free(to);to=NULL;
    char *outside=core_path_join(root,"outside"),*link=core_path_join(src,"link");put(outside);assert(!symlink(outside,link));ok(trash(link,&to));assert(!lstat(to,&after)&&S_ISLNK(after.st_mode)&&!access(outside,F_OK));free(to);to=NULL;
    char *special=core_path_join(src,"fifo");assert(!mkfifo(special,0600));assert(trash(special,&to).code==RESULT_UNSUPPORTED&&!to&&!access(special,F_OK));
    char *q=core_path_join(src,"errors");put(q);int baseline=count(infos);
    mode=WRITE_FAIL;Result r=trash(q,&to);assert(r.code==RESULT_IO&&!r.partial&&!to&&!access(q,F_OK)&&count(infos)==baseline);reset();
    mode=PRE_SYNC;r=trash(q,&to);assert(r.code==RESULT_IO&&!r.partial&&!to&&!access(q,F_OK)&&count(infos)==baseline);reset();
    mode=READY_CANCEL;r=trash(q,&to);assert(r.code==RESULT_CANCELLED&&!r.partial&&!to&&count(infos)==baseline&&!access(q,F_OK));reset();
    mode=LATE_COLLISION;r=trash(q,&to);assert(r.code==RESULT_EXISTS&&!r.partial&&!to&&!access(q,F_OK)&&count(infos)==baseline);reset();
    unknown_fs=true;assert(trash(q,&to).code==RESULT_UNSUPPORTED&&!to&&!access(q,F_OK));reset();
    assert(!chmod(home,0755));assert(trash(q,&to).code==RESULT_ACCESS&&!to&&!access(q,F_OK));assert(!chmod(home,0700));
    char *files_old=core_path_join(home,"saved-files");assert(!rename(files,files_old)&&!symlink(src,files));assert(trash(q,&to).code!=RESULT_OK&&!to&&!access(q,F_OK));assert(!unlink(files)&&!rename(files_old,files));free(files_old);
    mode=ORPHAN;r=trash(q,&to);assert(r.code==RESULT_IO&&r.partial&&!r.completed_items&&!to&&!access(q,F_OK)&&count(infos)==baseline+1&&strstr(r.detail,"orphan"));reset();
    mode=POST_SYNC;r=trash(q,&to);assert(r.code==RESULT_IO&&r.partial&&r.completed_items==1&&to&&access(q,F_OK)<0);info=info_for(to);assert(!access(info,F_OK)&&!access(to,F_OK));free(info);free(to);to=NULL;reset();
    put(q);mode=CRASH;pid_t child=fork();assert(child>=0);if(!child){trash(q,&to);_exit(99);}int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==77);assert(!access(q,F_OK)&&count(infos)==baseline+3);reset();
    mode=LEAF_SWAP;r=trash(q,&to);assert(r.code==RESULT_IO&&!to&&!access(q,F_OK));char *old=core_path_join(src,"saved-original");assert(!access(old,F_OK));assert(!unlink(q)&&!rename(old,q));free(old);reset();
    swap_parent=src;swap_old=core_path_join(root,"old-parent");decoy=core_path_join(root,"decoy");assert(!mkdir(decoy,0700));char *other=core_path_join(decoy,"errors");put(other);mode=PARENT_SWAP;ok(trash(q,&to));assert(!access(other,F_OK));old=core_path_join(swap_old,"errors");assert(access(old,F_OK)<0);assert(!unlink(src)&&!rename(swap_old,src));free(old);free(to);to=NULL;reset();
    // Mount routing is simulated into a dedicated temporary top; no real mount-root writes.
    virtual_top=core_path_join(root,"virtual-top");assert(!mkdir(virtual_top,0700));char *vsrc=core_path_join(virtual_top,"source");assert(!mkdir(vsrc,0700));char *vfile=core_path_join(vsrc,"one"),*shared=core_path_join(virtual_top,".Trash");assert(!mkdir(shared,01777));assert(!chmod(shared,01777));put(vfile);ok(trash(vfile,&to));assert(strstr(to,"/.Trash/"));info=info_for(to);content=read_file(info);assert(strstr(content,"Path=source/one\n"));free(content);free(info);free(to);to=NULL;
    assert(!chmod(shared,0777));put(vfile);ok(trash(vfile,&to));assert(strstr(to,"/.Trash-"));free(to);to=NULL;
    char name[48];snprintf(name,sizeof name,".Trash-%lu",(unsigned long)geteuid());char *private=core_path_join(virtual_top,name);assert(!chmod(private,0755));put(vfile);assert(trash(vfile,&to).code==RESULT_ACCESS&&!to&&!access(vfile,F_OK));assert(!chmod(private,0700));
    char *shared_saved=core_path_join(virtual_top,"shared-saved");assert(!rename(shared,shared_saved)&&!symlink(vsrc,shared));ok(trash(vfile,&to));assert(strstr(to,"/.Trash-")&&!access(vsrc,F_OK));free(to);to=NULL;free(virtual_top);virtual_top=NULL;
    // A committed move with a durability error is a failed target, but no stale mark remains.
    put(q);AppState app;ok(app_init(&app,src));ok(app_mark_toggle(&app,"errors"));BatchJob job;ok(batch_prepare(&app,0,BATCH_TRASH,&job));mode=POST_SYNC;r=batch_execute(&job,NULL,NULL);assert(r.code==RESULT_IO&&job.targets[0].status==BATCH_FAILED&&job.targets[0].result.completed_items==1&&job.targets[0].destination);app_marks_apply_result(&app,&job);assert(!app.marks_len);batch_free(&job);app_free(&app);reset();
    free(data);free(home);free(infos);free(files);free(p);free(tree);free(nested);free(outside);free(link);free(special);free(q);free(swap_old);free(decoy);free(other);free(vsrc);free(vfile);free(shared);free(private);free(shared_saved);
    free(src);ok(platform_remove(root));assert(fds()==initial);
    puts("PASS: Trash payload/metadata, no-clobber, failures/cancel/crash, pinned parents, leaf refusal, private mount routing (simulated), marks and FD lifetime");
}
