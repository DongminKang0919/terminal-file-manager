#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { uint64_t items, bytes, last_items, last_bytes; size_t calls; } Stop;
static bool progress(const OperationProgress *p, void *context) {
    Stop *stop = context;
    assert(p->path && *p->path);
    assert(p->completed_items >= stop->last_items && p->copied_bytes >= stop->last_bytes);
    stop->last_items = p->completed_items; stop->last_bytes = p->copied_bytes; stop->calls++;
    return p->completed_items < stop->items && p->copied_bytes < stop->bytes;
}
typedef struct { const char *destination, *saved, *marker; bool fired; } Swap;
static bool swap_then_cancel(const OperationProgress *p, void *context) {
    Swap *swap = context;
    if (!p->copied_bytes) return true;
    assert(!rename(swap->destination, swap->saved));
    assert(!symlink(swap->marker, swap->destination));
    swap->fired = true; return false;
}
static int descriptors(void) {
    DIR *d = opendir("/proc/self/fd"); assert(d); int n=0;
    while (readdir(d)) n++;
    assert(!closedir(d)); return n;
}
static void put(const char *p, size_t n) {
    int fd = open(p, O_CREAT|O_EXCL|O_WRONLY,0600); assert(fd >= 0);
    char buffer[4096]; memset(buffer,'z',sizeof buffer);
    while (n) { size_t part = n < sizeof buffer ? n : sizeof buffer; assert(write(fd,buffer,part)==(ssize_t)part); n-=part; }
    assert(!close(fd));
}
static void check_file(const char *p, size_t n) {
    struct stat st; assert(!lstat(p,&st)); assert((size_t)st.st_size==n);
    int fd = open(p,O_RDONLY); assert(fd >= 0); char buffer[4096]; ssize_t got;
    while ((got=read(fd,buffer,sizeof buffer))>0) for(ssize_t i=0;i<got;i++) assert(buffer[i]=='z');
    assert(!got && !close(fd));
}
static void cancelled(Result r, uint64_t items, uint64_t bytes, bool partial) {
    assert(r.code==RESULT_CANCELLED && r.completed_items==items && r.copied_bytes==bytes && r.partial==partial);
    assert(*r.path && strstr(r.detail,"Cancelled"));
}
int main(void) {
    char root[]="/tmp/tfile-progress-XXXXXX"; assert(mkdtemp(root));
    char *src=core_path_join(root,"source"), *dst=core_path_join(root,"copy"); assert(src && dst);
    put(src,262144); int before=descriptors();
    Stop stop={.items=0,.bytes=UINT64_MAX};
    cancelled(core_delete_progress(src,progress,&stop),0,0,false); check_file(src,262144);
    char *destination=NULL;
    stop=(Stop){.items=0,.bytes=UINT64_MAX};
    cancelled(core_transfer_progress(false,src,root,"copy",&destination,progress,&stop),0,0,false);
    assert(!destination && access(dst,F_OK)<0 && descriptors()==before);
    stop=(Stop){.items=UINT64_MAX,.bytes=65536};
    cancelled(core_transfer_progress(false,src,root,"copy",&destination,progress,&stop),0,65536,true);
    assert(!destination && descriptors()==before); check_file(src,262144); check_file(dst,65536);
    Result r=platform_copy(src,dst); assert(r.code==RESULT_EXISTS && !r.partial && !r.completed_items && !r.copied_bytes);
    assert(platform_remove(dst).code==RESULT_OK);
    stop=(Stop){.items=UINT64_MAX,.bytes=UINT64_MAX};
    r=core_transfer_progress(false,src,root,"copy",&destination,progress,&stop);
    assert(r.code==RESULT_OK && r.completed_items==1 && r.copied_bytes==262144 && stop.calls>4);
    free(destination); check_file(dst,262144); assert(platform_remove(dst).code==RESULT_OK);
    char *saved=core_path_join(root,"saved"), *marker=core_path_join(root,"outside-marker");
    put(marker,23);
    Swap swap={dst,saved,marker,false};
    cancelled(platform_copy_progress(src,dst,swap_then_cancel,&swap),0,65536,true);
    assert(swap.fired && descriptors()==before); check_file(marker,23); check_file(saved,65536);
    assert(platform_remove(dst).code==RESULT_OK); check_file(marker,23);
    assert(platform_remove(saved).code==RESULT_OK && platform_remove(marker).code==RESULT_OK);
    free(saved); free(marker);
    assert(platform_remove(src).code==RESULT_OK); assert(!mkdir(src,0700));
    for(int i=0;i<5;i++) { char name[32]; snprintf(name,sizeof name,"%d",i); char *p=core_path_join(src,name); put(p,17); free(p); }
    assert(!chmod(src,0555));
    stop=(Stop){.items=2,.bytes=UINT64_MAX};
    cancelled(platform_copy_progress(src,dst,progress,&stop),2,34,true);
    struct stat st; assert(!lstat(dst,&st) && (st.st_mode&0777)==0700); assert(descriptors()==before);
    FileList list; assert(core_list(dst,true,false,&list).code==RESULT_OK && list.len==2); file_list_free(&list);
    assert(core_list(src,true,false,&list).code==RESULT_OK && list.len==5); file_list_free(&list);
    assert(platform_remove(dst).code==RESULT_OK);
    stop=(Stop){.items=5,.bytes=UINT64_MAX};
    cancelled(platform_copy_progress(src,dst,progress,&stop),5,85,true);
    assert(!lstat(dst,&st) && (st.st_mode&0777)==0700); /* Final chmod was not reached. */
    assert(platform_remove(dst).code==RESULT_OK);
    mode_t mask=umask(0022);
    r=platform_copy(src,dst); assert(r.code==RESULT_OK && r.completed_items==6 && r.copied_bytes==85);
    assert(!lstat(dst,&st) && (st.st_mode&0777)==0555); umask(mask);
    assert(!chmod(dst,0700));
    stop=(Stop){.items=2,.bytes=UINT64_MAX};
    cancelled(platform_remove_progress(dst,progress,&stop),2,0,true); assert(descriptors()==before);
    assert(core_list(dst,true,false,&list).code==RESULT_OK && list.len==3); file_list_free(&list);
    r=platform_remove(dst); assert(r.code==RESULT_OK && r.completed_items==4);
    for(int i=0;i<30;i++) {
        stop=(Stop){.items=1,.bytes=UINT64_MAX};
        cancelled(platform_copy_progress(src,dst,progress,&stop),1,17,true);
        assert(descriptors()==before); assert(platform_remove(dst).code==RESULT_OK);
    }
    assert(!chmod(src,0700)); assert(platform_remove(src).code==RESULT_OK);
    /* Failure after completed children reports the exact counters and retained data. */
    assert(!mkdir(src,0700)); char *a=core_path_join(src,"first"), *b=core_path_join(src,"blocked");
    put(a,5); assert(!mkfifo(b,0600));
    r=platform_copy(src,dst); assert(r.code==RESULT_UNSUPPORTED && r.partial && strstr(r.path,"blocked"));
    assert(core_list(dst,true,false,&list).code==RESULT_OK);
    assert(r.completed_items==list.len && r.copied_bytes==list.len*5); file_list_free(&list);
    assert(platform_remove(dst).code==RESULT_OK && platform_remove(src).code==RESULT_OK);
    assert(descriptors()==before); free(a); free(b); free(src); free(dst); assert(!rmdir(root));
    puts("PASS: deterministic start/chunk/item/delete cancellation, counters, residual modes/data, no-clobber, FD cleanup and subsequent operations");
}
