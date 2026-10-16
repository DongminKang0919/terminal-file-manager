#include "ui.h"

void ui_path_query_close(UiPathQuery *q) {
    core_read_close(q->job);free(q->base);free(q->input);free(q->directory);
    file_list_free(&q->files);*q=(UiPathQuery){0};
}
void ui_path_query_poll(UiPathQuery *q) {
    (void)core_read_cleanup_pending();
    if(!q->pending)return;
    if(!q->job) {
        if(!core_read_available())return; /* Retire old request before reusing lane. */
        q->result=q->listing?core_read_directory_start(q->base,q->input,true,q->directories_only,&q->job):
            core_read_resolve_start(q->base,q->input,&q->job);
        if(q->result.code==RESULT_CANCELLED)return;
        if(q->result.code!=RESULT_OK) { q->pending=false;q->ready=true;return; }
    }
    bool done=false;q->result=core_read_poll(q->job,&done);
    if(!done)return;
    SearchResult result={0};core_read_take(q->job,&result,&q->directory);
    q->files=result.matches;result.matches=(FileList){0};search_result_free(&result);
    core_read_close(q->job);q->job=NULL;q->pending=false;q->ready=true;
    if(q->listing && q->files.len>1)qsort(q->files.entries,q->files.len,sizeof(FileInfo),file_compare);
}
void ui_path_query_start(UiPathQuery *q,bool asynchronous,const char *base,const char *input,bool listing,bool directories_only) {
    ui_path_query_close(q);
    q->base=base?text_copy(base):NULL;q->input=text_copy(input);q->listing=listing;q->directories_only=directories_only;
    if((base&&!q->base)||!q->input) { q->result=result_make(RESULT_NO_MEMORY,"Out of memory");q->ready=true;return; }
    if(asynchronous) { q->pending=true;ui_path_query_poll(q);return; }
    q->result=core_resolve_directory(base,input,&q->directory);
    if(q->result.code==RESULT_OK && listing)q->result=core_list(q->directory,true,directories_only,&q->files);
    q->ready=true;
}
