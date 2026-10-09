#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *change_path;
Result __real_platform_reader_peek(PlatformReader *,unsigned char *,size_t,size_t *);
Result __wrap_platform_reader_peek(PlatformReader *r,unsigned char *b,size_t cap,size_t *n) {
    Result result=__real_platform_reader_peek(r,b,cap,n);
    if(change_path) {FILE *f=fopen(change_path,"ab");assert(f);assert(fputs("changed",f)>=0);assert(!fclose(f));}
    return result;
}
static void ok(Result r) {if(r.code)fprintf(stderr,"%s\n",r.detail);assert(!r.code);}
static void put(const char *p,const void *data,size_t n) {FILE *f=fopen(p,"wb");assert(f);assert(fwrite(data,1,n,f)==n);assert(!fclose(f));}
int main(int argc,char **argv) {
    assert(argc==4);const char *file=argv[1],*root=argv[2],*empty=argv[3];bool ro=true;
    char *p=core_path_join(root,"checking"),*link=core_path_join(root,"symlink"),*fifo=core_path_join(root,"fifo");assert(p&&link&&fifo);
    const char *text[]={"", "plain config\n", "// 소스 😃\n", "\xef\xbb\xbfUTF8 BOM\r\n", "<svg>text format</svg>"};
    for(size_t i=0;i<sizeof text/sizeof *text;i++) {put(p,text[i],strlen(text[i]));ok(core_vim_check(p,&ro));assert(!ro);}
    const unsigned char *binary[]={(unsigned char *)"\211PNG\r\n\032\n",(unsigned char *)"\xff\xd8\xff",(unsigned char *)"GIF89a",(unsigned char *)"%PDF-1.7",(unsigned char *)"prefix\n%PDF-1.4",(unsigned char *)"abc\0tail",(unsigned char *)"abc\033control",(unsigned char *)"\xc0\xaf",(unsigned char *)"\xed\xa0\x80",(unsigned char *)"\xf4\x90\x80\x80",(unsigned char *)"\xe3\x81"};
    const size_t sizes[]={8,3,6,8,15,8,11,2,3,4,2};
    for(size_t i=0;i<sizeof sizes/sizeof *sizes;i++) {put(p,binary[i],sizes[i]);assert(core_vim_check(p,&ro).code==RESULT_UNSUPPORTED);assert(core_vim_run(p).code==RESULT_UNSUPPORTED);}
    change_path=p;assert(core_vim_check(p,&ro).code==RESULT_IO);change_path=NULL;
    unsigned char *long_text=malloc(65544);assert(long_text);memset(long_text,'a',65544);memcpy(long_text+65535,"\xf0\x9f\x98\x83",4);put(p,long_text,65544);ok(core_vim_check(p,&ro));
    // The bounded prefix is a documented heuristic, not a full-file binary proof.
    memset(long_text,'a',65544);long_text[65543]=0;put(p,long_text,65544);ok(core_vim_check(p,&ro));free(long_text);
    put(p,"read-only",9);assert(!chmod(p,0444));ok(core_vim_check(p,&ro));assert(ro);
    assert(!symlink(file,link)&&!mkfifo(fifo,0600));assert(core_vim_check(link,&ro).code==RESULT_UNSUPPORTED);assert(core_vim_check(fifo,&ro).code==RESULT_UNSUPPORTED);assert(core_vim_check(root,&ro).code==RESULT_UNSUPPORTED);
    // Always Vim; neither VISUAL nor EDITOR silently changes the selected tool.
    assert(!setenv("VISUAL","missing-editor",1)&&!setenv("EDITOR","vi",1));struct sigaction before,after;assert(!sigaction(SIGINT,NULL,&before));ok(core_vim_run(file));ok(core_vim_run(p));assert(!sigaction(SIGINT,NULL,&after)&&before.sa_handler==after.sa_handler);
    assert(!setenv("TFILE_VIM_MODE","fail",1));assert(core_vim_run(file).code==RESULT_IO);
    assert(!setenv("TFILE_VIM_MODE","signal",1));Result r=core_vim_run(file);assert(r.code==RESULT_IO&&strstr(r.detail,"signal"));assert(!unsetenv("TFILE_VIM_MODE"));
    assert(!setenv("PATH",empty,1));r=core_vim_run(file);assert(r.code==RESULT_NOT_FOUND&&strstr(r.detail,"install vim")&&strstr(r.detail,"No fallback"));
    free(p);free(link);free(fifo);puts("PASS: Vim ignores editor env, UTF-8/empty/extensionless content check, media/binary/link/special refusal, read-only argv, missing tool, exit/signal and signal restoration");
}
