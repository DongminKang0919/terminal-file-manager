#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>

void editor_command_free(EditorCommand *c) {
    for(size_t i=0;i<c->len;i++) free(c->argv[i]);
    free(c->argv); *c=(EditorCommand){0};
}
Result editor_command_parse(const char *text,EditorCommand *out) {
    *out=(EditorCommand){0};
    if(!text || strlen(text)>4095) return result_make(RESULT_INVALID_NAME,"Editor command exceeds 4095 bytes");
    char word[4096];size_t n=0;char quote=0;bool started=false;
    Result r=result_make(RESULT_OK,NULL);
    for(size_t at=0;;at++) {
        unsigned char ch=(unsigned char)text[at];
        if(!ch || (!quote && (ch==' '||ch=='\t'))) {
            if(quote) { r=result_make(RESULT_INVALID_NAME,"Unclosed editor quote");break; }
            if(started) {
                if(out->len==32) { r=result_make(RESULT_INVALID_NAME,"At most 32 editor arguments");break; }
                word[n]=0;char *copy=text_copy(word);char **args=copy?realloc(out->argv,(out->len+2)*sizeof *args):NULL;
                if(!args) { free(copy);r=result_make(RESULT_NO_MEMORY,"Out of memory");break; }
                out->argv=args;args[out->len++]=copy;args[out->len]=NULL;n=0;started=false;
            }
            if(!ch) break;
            continue;
        }
        if(ch=='\n'||ch=='\r' || (!quote && strchr("|&;<>$`()",ch))) { r=result_make(RESULT_INVALID_NAME,"Shell operators/substitutions are not supported");break; }
        started=true;
        if(ch=='\\' && quote!='\'') {
            if(!text[at+1]) { r=result_make(RESULT_INVALID_NAME,"Trailing editor escape");break; }
            word[n++]=text[++at];continue;
        }
        if(ch=='\''||ch=='"') {
            if(!quote) {quote=(char)ch;continue;}
            if(quote==(char)ch) {quote=0;continue;}
        }
        word[n++]=(char)ch;
    }
    if(r.code==RESULT_OK && (!out->len || !out->argv[0][0])) r=result_make(RESULT_INVALID_NAME,"Empty editor program");
    if(r.code!=RESULT_OK) editor_command_free(out);
    return r;
}
Result core_editor_run(const char *path) {
    char *setting=NULL;Result r=platform_editor_setting(&setting);
    if(r.code!=RESULT_OK) return r;
    EditorCommand command; r=editor_command_parse(setting,&command);free(setting);
    if(r.code==RESULT_OK) r=platform_editor_run(command.argv,command.len,path);
    editor_command_free(&command);return r;
}
struct ExternalJob { PlatformExternal *process; };
Result core_external_open(const char *path,ExternalJob **out) {
    *out=NULL;ExternalJob *job=calloc(1,sizeof *job);
    if(!job) return result_make(RESULT_NO_MEMORY,"Out of memory");
    Result r=platform_external_open(path,&job->process);
    if(r.code==RESULT_OK) *out=job;else free(job);
    return r;
}
Result core_external_poll(ExternalJob *job,bool *done) { return platform_external_poll(job->process,done); }
void core_external_close(ExternalJob *job) { if(job) { platform_external_close(job->process);free(job); } }
bool core_external_cleanup_pending(void) { return platform_external_cleanup_pending(); }
bool core_terminal_size(unsigned *rows,unsigned *columns) { return platform_terminal_size(rows,columns); }
