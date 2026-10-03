#define _GNU_SOURCE
#include "../src/core/core.h"
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Track explicit application allocations without allocating in the tracker. */
static void *owned[32768];
static size_t live, calls, fail_at;
void *__real_malloc(size_t); void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t); void __real_free(void *);
static size_t slot(void *p) {
    for(size_t i=0;i<32768;i++) if(owned[i]==p) return i;
    return 32768;
}
static void keep(void *p) {
    if(!p) return;
    size_t i=slot(NULL); assert(i<32768); owned[i]=p; live++;
}
static bool fail(void) { return ++calls==fail_at; }
void *__wrap_malloc(size_t n) { if(fail()) return NULL; void *p=__real_malloc(n); keep(p); return p; }
void *__wrap_calloc(size_t n,size_t s) { if(fail()) return NULL; void *p=__real_calloc(n,s); keep(p); return p; }
void __wrap_free(void *p) {
    if(p) { size_t i=slot(p); if(i<32768) { owned[i]=NULL; live--; } }
    __real_free(p);
}
void *__wrap_realloc(void *p,size_t n) {
    if(fail()) return NULL;
    size_t i=p?slot(p):32768;
    void *q=__real_realloc(p,n);
    if(q || !n) { if(i<32768) { owned[i]=NULL; live--; } keep(q); }
    return q;
}
static size_t fds(void) {
    DIR *d=opendir("/proc/self/fd"); assert(d); size_t n=0; struct dirent *e;
    while((e=readdir(d))) if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")) n++;
    closedir(d); return n;
}
int main(void) {
    char root[]="/tmp/tfile-ownership-XXXXXX"; assert(mkdtemp(root));
    char file[256]; snprintf(file,sizeof file,"%s/text",root);
    FILE *f=fopen(file,"w"); assert(f);
    for(int i=0;i<5000;i++) fprintf(f,"line %d\n",i);
    assert(!fclose(f)); size_t descriptors=fds();
    for(size_t fault=1;fault<=100;fault++) {
        calls=0; fail_at=fault;
        AppState app={0}; Result r=app_init(&app,root);
        fail_at=0; app_free(&app); assert(!live); assert(fds()==descriptors);
        (void)r;
        calls=0; fail_at=fault;
        SearchResult found=core_search(root,"text",search_default_limits(),NULL,NULL);
        fail_at=0; search_result_free(&found); assert(!live); assert(fds()==descriptors);
        calls=0; fail_at=fault;
        PreviewSession *session=NULL; r=preview_session_open(file,&session);
        if(r.code==RESULT_OK) {
            PreviewText page={0}; r=preview_session_page(session,1000,PREVIEW_PAGE_MAX,&page);
            preview_text_free(&page);
        }
        fail_at=0; preview_session_close(session); assert(!live); assert(fds()==descriptors);
        /* Failed refresh must retain its owned list, then release it once. */
        assert(app_init(&app,root).code==RESULT_OK);
        FileInfo *old=app.files.entries; size_t count=app.files.len;
        calls=0; fail_at=fault; r=app_refresh(&app); fail_at=0;
        if(r.code!=RESULT_OK) { assert(app.files.entries==old); assert(app.files.len==count); }
        app_free(&app); assert(!live); assert(fds()==descriptors);
        assert(app_init(&app,root).code==RESULT_OK);
        calls=0;fail_at=fault;r=app_mark_all(&app);fail_at=0;
        if(r.code==RESULT_OK) {
            BatchJob job;calls=0;fail_at=fault;r=batch_prepare(&app,0,BATCH_COPY,&job);fail_at=0;
            if(r.code==RESULT_OK) { calls=0;fail_at=fault;(void)batch_destination(&job,root);fail_at=0; }
            batch_free(&job);
        }
        app_free(&app);assert(!live);assert(fds()==descriptors);
    }
    assert(!unlink(file)); assert(!rmdir(root));
    puts("PASS: 100 allocation-failure positions in listing/refresh/search/preview release explicit ownership and FDs; failed refresh retains list");
}
