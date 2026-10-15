#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>

enum { PREVIEW_STATE=1, PREVIEW_LINE, PREVIEW_LINK, PREVIEW_DONE };
typedef struct { const char *path; FileKind kind; PreviewRequest request; } Work;
struct PreviewJob { PlatformTask *task; PreviewReply reply; size_t lines; bool state, busy; };
void core_preview_reply_free(PreviewReply *reply) {
    preview_text_free(&reply->page); free(reply->link); *reply=(PreviewReply){0};
}
static void preview_work(PlatformTaskWriter *writer, void *context) {
    Work *w=context;
    PreviewSession *session=NULL;
    PreviewRequest request=w->request;
    Result opened=result_make(RESULT_OK,NULL);
    if(w->kind==FILE_REGULAR) opened=preview_session_open(w->path,&session);
    do {
        PreviewReply reply={.request=request,.result=opened};
        if(reply.result.code==RESULT_OK) {
            if(w->kind==FILE_DIRECTORY) reply.result=core_preview_directory_empty(w->path,&reply.directory_empty);
            else if(w->kind==FILE_LINK) reply.result=core_link_target(w->path,&reply.link);
            else if(session) {
                reply.result=preview_session_check(session,&reply.changed);
                if(reply.changed) reply.request.start=0;
                reply.media=preview_session_media(session);
                if(reply.result.code==RESULT_OK && reply.media==PREVIEW_NOT_MEDIA) {
                    reply.result=preview_session_page(session,reply.request.start,request.limit,&reply.page);
                    if(reply.result.code==RESULT_OK && !reply.page.more && !reply.page.binary) {
                        size_t total=reply.page.skipped+reply.page.len+(reply.page.empty?1:0);
                        size_t rows=request.limit>1?request.limit-1:1;
                        size_t max=total>rows?total-rows:0;
                        if(reply.request.start>max) {
                            reply.request.start=max; preview_text_free(&reply.page);
                            reply.result=preview_session_page(session,max,request.limit,&reply.page);
                        }
                    }
                }
            }
        }
        PreviewReply wire=reply; wire.page.lines=NULL; wire.link=NULL;
        bool ok=platform_task_send(writer,PREVIEW_STATE,&wire,sizeof wire);
        for(size_t i=0;ok && i<reply.page.len;i++) ok=platform_task_send(writer,PREVIEW_LINE,reply.page.lines[i],strlen(reply.page.lines[i])+1);
        if(ok && reply.link) ok=platform_task_send(writer,PREVIEW_LINK,reply.link,strlen(reply.link)+1);
        core_preview_reply_free(&reply);
        if(!ok || !platform_task_send(writer,PREVIEW_DONE,NULL,0)) break;
    } while(platform_task_command(writer,&request,sizeof request));
    preview_session_close(session);
}
bool core_preview_available(void) { return platform_preview_available(); }
Result core_preview_start(const char *path,FileKind kind,PreviewRequest request,PreviewJob **out) {
    *out=NULL;
    if(request.limit>PREVIEW_PAGE_MAX) return result_make(RESULT_INVALID_NAME,"Preview page too large");
    PreviewJob *job=calloc(1,sizeof *job);
    if(!job) return result_make(RESULT_NO_MEMORY,"Out of memory");
    Work work={path,kind,request};
    Result r=platform_preview_open(preview_work,&work,&job->task);
    if(r.code!=RESULT_OK) { free(job); return r; }
    job->busy=true; *out=job; return r;
}
Result core_preview_request(PreviewJob *job,PreviewRequest request) {
    if(job->busy || request.limit>PREVIEW_PAGE_MAX || !platform_task_request(job->task,&request,sizeof request))
        return result_make(RESULT_IO,"Cannot request preview page");
    job->busy=true; return result_make(RESULT_OK,NULL);
}
Result core_preview_poll(PreviewJob *job,bool *done,PreviewReply *reply) {
    *done=false;
    Result r=result_make(RESULT_OK,NULL);
    for(unsigned i=0;i<32;i++) {
        unsigned type; void *data; size_t len; bool ready;
        r=platform_task_receive(job->task,&type,&data,&len,&ready);
        if(r.code!=RESULT_OK || !ready) return r;
        if(type==PREVIEW_STATE && !job->state && len==sizeof(PreviewReply)) {
            memcpy(&job->reply,data,len); job->reply.page.lines=NULL; job->reply.link=NULL;
            if(job->reply.page.len>PREVIEW_PAGE_MAX) { job->reply.page.len=0; r=result_make(RESULT_IO,"Invalid preview page"); }
            else {
                job->reply.page.lines=calloc(job->reply.page.len?job->reply.page.len:1,sizeof(char *));
                if(!job->reply.page.lines) { job->reply.page.len=0; r=result_make(RESULT_NO_MEMORY,"Out of memory"); }
                job->state=true;
            }
        } else if(type==PREVIEW_LINE && job->state && job->lines<job->reply.page.len && len && len<=4176 && !((char *)data)[len-1]) {
            job->reply.page.lines[job->lines++]=data; data=NULL;
        } else if(type==PREVIEW_LINK && job->state && !job->reply.link && len && !((char *)data)[len-1]) {
            job->reply.link=data; data=NULL;
        } else if(type==PREVIEW_DONE && !len && job->state && job->lines==job->reply.page.len) {
            *reply=job->reply; job->reply=(PreviewReply){0}; job->lines=0; job->state=job->busy=false; *done=true;
        } else r=result_make(RESULT_IO,"Invalid preview worker record");
        free(data);
        if(r.code!=RESULT_OK || *done) return r;
    }
    return r;
}
void core_preview_close(PreviewJob *job) {
    if(!job) return;
    platform_task_close(job->task); core_preview_reply_free(&job->reply); free(job);
}
