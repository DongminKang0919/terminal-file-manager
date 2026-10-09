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
/* Conservative UTF-8 text heuristic, independent of filename extensions.
   Inspect at most 64 KiB, plus up to three bytes to finish a UTF-8 character. */
static bool vim_text(const unsigned char *s,size_t n) {
    if((n>=8&&!memcmp(s,"\211PNG\r\n\032\n",8)) ||
       (n>=3&&s[0]==255&&s[1]==216&&s[2]==255) ||
       (n>=6&&(!memcmp(s,"GIF87a",6)||!memcmp(s,"GIF89a",6))) ||
       (n>=3&&!memcmp(s,"ID3",3)) || (n>=4&&(!memcmp(s,"OggS",4)||!memcmp(s,"fLaC",4))) ||
       (n>=12&&!memcmp(s,"RIFF",4)&&(!memcmp(s+8,"WEBP",4)||!memcmp(s+8,"WAVE",4)||!memcmp(s+8,"AVI ",4)))) return false;
    for(size_t i=0;i+5<=n && i<1024;i++) if(!memcmp(s+i,"%PDF-",5)) return false;
    for(size_t i=0;i<n&&i<65536;) {
        unsigned c=s[i++];
        if(c<128) { if((c<32&&c!='\t'&&c!='\n'&&c!='\r'&&c!='\f')||c==127) return false;continue; }
        unsigned more,code,min;
        if(c>=0xc2&&c<=0xdf) {more=1;code=c&31;min=0x80;}
        else if(c>=0xe0&&c<=0xef) {more=2;code=c&15;min=0x800;}
        else if(c>=0xf0&&c<=0xf4) {more=3;code=c&7;min=0x10000;}
        else return false;
        if(more>n-i) return false;
        while(more--) {unsigned b=s[i++];if((b&0xc0)!=0x80)return false;code=(code<<6)|(b&63);}
        if(code<min || code>0x10ffff || (code>=0xd800&&code<=0xdfff)) return false;
    }
    return true;
}
Result core_vim_check(const char *path,bool *read_only) {
    unsigned char sample[65539];size_t n=0;
    Result r=platform_editor_inspect(path,sample,sizeof sample,&n,read_only);
    if(r.code==RESULT_OK && !vim_text(sample,n)) r=result_make(RESULT_UNSUPPORTED,"Vim: binary/media or unsupported text encoding; not opened (64 KiB UTF-8 sample)");
    return r;
}
Result core_vim_run(const char *path) {
    bool read_only=false;Result r=core_vim_check(path,&read_only);
    if(r.code!=RESULT_OK) return r;
    r=platform_vim_available();if(r.code!=RESULT_OK)return r;
    char *normal[]={"vim","--",NULL};char *readonly[]={"vim","-R","--",NULL};
    r=platform_editor_run(read_only?readonly:normal,read_only?3:2,path);
    if(r.code==RESULT_OK) return result_make(RESULT_OK,read_only?"Vim returned (read-only); save completion unconfirmed":"Vim returned; save completion unconfirmed");
    return r;
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
