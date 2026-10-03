#include "core.h"
#include <stdlib.h>
#include <string.h>

/* A sorted owned name set, independent of list ordering and FileInfo lifetime. */
static size_t lower(const AppState *app, const char *name) {
    size_t lo=0, hi=app->marks_len;
    while(lo<hi) { size_t mid=lo+(hi-lo)/2;
        if(strcmp(app->marks[mid].name,name)<0) lo=mid+1; else hi=mid;
    }
    return lo;
}
bool app_marked(const AppState *app, const char *name) {
    size_t at=lower(app,name);
    return at<app->marks_len && !strcmp(app->marks[at].name,name);
}
void app_marks_clear(AppState *app) {
    for(size_t i=0;i<app->marks_len;i++) free(app->marks[i].name);
    free(app->marks); app->marks=NULL; app->marks_len=app->marks_cap=0;
}
void app_unmark(AppState *app, const char *name) {
    size_t at=lower(app,name);
    if(at==app->marks_len || strcmp(app->marks[at].name,name)) return;
    free(app->marks[at].name);
    memmove(app->marks+at,app->marks+at+1,(app->marks_len-at-1)*sizeof *app->marks);
    app->marks_len--;
}
Result app_mark_toggle(AppState *app, const char *name) {
    size_t at=lower(app,name);
    if(at<app->marks_len && !strcmp(app->marks[at].name,name)) {
        app_unmark(app,name); return result_make(RESULT_OK,NULL);
    }
    char *copy=text_copy(name);
    if(!copy) return result_make(RESULT_NO_MEMORY,"Out of memory");
    if(app->marks_len==app->marks_cap) {
        size_t next=app->marks_cap ? app->marks_cap*2 : 16;
        if(next<app->marks_cap || next>SIZE_MAX/sizeof *app->marks) { free(copy); return result_make(RESULT_NO_MEMORY,"Selection too large"); }
        SelectionName *marks=realloc(app->marks,next*sizeof *marks);
        if(!marks) { free(copy); return result_make(RESULT_NO_MEMORY,"Out of memory"); }
        app->marks=marks; app->marks_cap=next;
    }
    memmove(app->marks+at+1,app->marks+at,(app->marks_len-at)*sizeof *app->marks);
    app->marks[at]=(SelectionName){.name=copy}; app->marks_len++;
    return result_make(RESULT_OK,NULL);
}
static int compare(const void *a,const void *b) {
    return strcmp(((const SelectionName *)a)->name,((const SelectionName *)b)->name);
}
Result app_mark_all(AppState *app) {
    if(!app->files.len) { app_marks_clear(app); return result_make(RESULT_OK,NULL); }
    if(app->files.len>SIZE_MAX/sizeof *app->marks) return result_make(RESULT_NO_MEMORY,"Selection too large");
    SelectionName *marks=calloc(app->files.len,sizeof *marks);
    if(!marks) return result_make(RESULT_NO_MEMORY,"Out of memory");
    for(size_t i=0;i<app->files.len;i++) {
        marks[i].name=text_copy(app->files.entries[i].name);
        if(!marks[i].name) {
            for(size_t j=0;j<i;j++) free(marks[j].name);
            free(marks); return result_make(RESULT_NO_MEMORY,"Out of memory");
        }
    }
    qsort(marks,app->files.len,sizeof *marks,compare);
    app_marks_clear(app); app->marks=marks; app->marks_len=app->marks_cap=app->files.len;
    return result_make(RESULT_OK,NULL);
}
/* No allocation or I/O. A single list pass plus binary lookups, then compaction. */
void app_marks_reconcile(AppState *app) {
    for(size_t i=0;i<app->marks_len;i++) app->marks[i].seen=false;
    for(size_t i=0;i<app->files.len;i++) {
        const char *name=app->files.entries[i].name; size_t at=lower(app,name);
        if(at<app->marks_len && !strcmp(app->marks[at].name,name)) app->marks[at].seen=true;
    }
    size_t kept=0;
    for(size_t i=0;i<app->marks_len;i++) {
        if(app->marks[i].seen) app->marks[kept++]=app->marks[i]; else free(app->marks[i].name);
    }
    app->marks_len=kept;
}

void app_marks_apply_result(AppState *app,const BatchJob *job) {
    for(size_t i=0;i<app->marks_len;i++) app->marks[i].seen=true;
    for(size_t i=0;i<job->len;i++) if(job->targets[i].status==BATCH_SUCCESS) {
        const char *name=job->targets[i].name; size_t at=lower(app,name);
        if(at<app->marks_len && !strcmp(app->marks[at].name,name)) app->marks[at].seen=false;
    }
    size_t kept=0;
    for(size_t i=0;i<app->marks_len;i++) {
        if(app->marks[i].seen) app->marks[kept++]=app->marks[i];else free(app->marks[i].name);
    }
    app->marks_len=kept;
}
