#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void ok(Result r) { if(r.code) fprintf(stderr,"%s\n",r.detail);assert(r.code==RESULT_OK); }
static void put(const char *p) { FILE *f=fopen(p,"w");assert(f);fputs("payload",f);fclose(f); }
int main(void) {
    char root[]="/tmp/tfile-filter-XXXXXX";assert(mkdtemp(root));
    const char *names[]={"a한글.txt","A.TXT","b[1].jpg",".hidden.txt"};
    for(size_t i=0;i<4;i++) { char *p=core_path_join(root,names[i]);assert(p);put(p);free(p); }
    char *sub=core_path_join(root,"sub");assert(sub&&!mkdir(sub,0700));
    char *nested=core_path_join(sub,"nested.txt");assert(nested);put(nested);
    AppState app,other;ok(app_init(&app,root));ok(app_init(&other,root));ok(app_mark_all(&app));
    ok(app_set_filter(&app,FILTER_CONTAINS,"txt"));assert(app.files.len==3&&app.unfiltered_count==5&&!app.marks_len&&other.files.len==5);
    ok(app_mark_all(&app));BatchJob job;ok(batch_prepare(&app,0,BATCH_COPY,&job));assert(job.len==3);batch_free(&job);
    ok(app_set_filter(&app,FILTER_GLOB,"*.txt"));assert(app.files.len==2&&!app.marks_len);
    ok(app_set_filter(&app,FILTER_GLOB,"b\\[1\\].jpg"));assert(app.files.len==1&&!strcmp(app.files.entries[0].name,names[2]));
    ok(app_set_filter(&app,FILTER_CONTAINS,"한글"));assert(app.files.len==1);
    ok(app_set_filter(&app,FILTER_GLOB,"*.txt"));app.show_hidden=false;ok(app_refresh(&app));assert(app.files.len==1&&app.unfiltered_count==4);
    size_t selected=0;app_set_sort(&app,(SortSettings){SORT_SIZE,true},&selected);assert(app.files.len==1&&app.filter_kind==FILTER_GLOB);
    ok(app_navigate(&app,sub));assert(app.files.len==1&&app.filter_kind==FILTER_GLOB);ok(app_history(&app,false));assert(app.files.len==1);
    ok(app_set_filter(&app,FILTER_CONTAINS,"absent"));assert(!app.files.len&&app.unfiltered_count==4);
    assert(geteuid()!=0);assert(!chmod(root,0000));FileInfo *old=app.files.entries;char *filter=app.filter;
    assert(app_set_filter(&app,FILTER_NONE,"").code==RESULT_ACCESS&&app.files.entries==old&&app.filter==filter);
    assert(!chmod(root,0700));
    char *target=core_path_join(root,"a한글.txt");bool revealed=false;
    ok(app_open_search_result(&app,target,&selected,&revealed));assert(revealed&&app.filter_kind==FILTER_NONE&&app.files.len==4&&!strcmp(app.files.entries[selected].name,names[0]));
    ok(app_navigate(&app,sub));ok(core_delete(nested));ok(app_set_filter(&app,FILTER_CONTAINS,"empty"));assert(!app.files.len&&!app.unfiltered_count);
    app_free(&app);app_free(&other);free(target);free(nested);free(sub);ok(platform_remove(root));
    puts("PASS: contains/glob/raw Unicode, independent panels, hidden/sort/history/refresh rules, transactional failure, visible-only batches, mark clearing and empty/no-match distinction");
}
