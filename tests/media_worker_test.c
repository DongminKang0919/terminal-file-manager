#define _GNU_SOURCE
/* Normal-return media preparation body, separate from fork/cancel UI tests. */
#include "../src/platform/posix_media.c"
#include <assert.h>
static void write_file(const char *path,const char *body) {
    FILE *f=fopen(path,"w");assert(f);assert(fputs(body,f)>=0);assert(!fclose(f));
}
int main(void) {
    char root[]="/tmp/tfile-media-body-XXXXXX";assert(mkdtemp(root));
    char path[256],tool[256];snprintf(path,sizeof path,"%s/source",root);snprintf(tool,sizeof tool,"%s/convert",root);
    write_file(tool,"#!/bin/sh\nprintf '\\033Pq\"1;1;6;6#0;2;100;0;0!6~\\033\\\\'\n");assert(!chmod(tool,0700));
    assert(!setenv("PATH",root,1));
    for(unsigned i=0;i<20;i++) {
        write_file(path,"\211PNG\r\n\032\nbody");
        PlatformMedia *m=NULL;assert(media_allocate(path,PREVIEW_PNG,false,120,90,false,&m).code==RESULT_OK);
        assert(media_work(m).code==RESULT_OK);assert(m->source==-1 && !m->tool && !m->pid);dispose(m);
        assert(media_allocate(path,PREVIEW_PNG,false,120,90,true,&m).code==RESULT_OK);
        assert(media_work(m).code==RESULT_OK);TerminalTools tools={0};assert(pread(m->output,&tools,sizeof tools,0)==sizeof tools && tools.image==2);dispose(m);
        write_file(path,"unsupported");assert(media_allocate(path,PREVIEW_PNG,false,120,90,false,&m).code==RESULT_OK);
        assert(media_work(m).code==RESULT_UNSUPPORTED && m->source==-1 && !m->tool);dispose(m);
    }
    char pdf_tool[256],text_tool[256];
    snprintf(pdf_tool,sizeof pdf_tool,"%s/pdftoppm",root);snprintf(text_tool,sizeof text_tool,"%s/pdftotext",root);
    write_file(pdf_tool,"#!/bin/sh\nfor last; do :; done\nprintf '\\211PNG\\r\\n\\032\\nbody' > \"$last.png\"\n");
    write_file(text_tool,"#!/bin/sh\nprintf 'PDF text\\n'\n");assert(!chmod(pdf_tool,0700)&&!chmod(text_tool,0700));
    write_file(path,"%PDF-1.4\nbody");
    for(unsigned i=0;i<2;i++) {
        PlatformMedia *m=NULL;assert(media_allocate(path,PREVIEW_PDF,i!=0,120,90,false,&m).code==RESULT_OK);
        assert(media_work(m).code==RESULT_OK && m->source==-1 && !m->tool && !m->pid);dispose(m);
    }
    assert(!unlink(pdf_tool)&&!unlink(text_tool));
    assert(!unlink(tool));write_file(path,"\211PNG\r\n\032\nbody");
    PlatformMedia *m=NULL;assert(media_allocate(path,PREVIEW_PNG,false,120,90,false,&m).code==RESULT_OK);
    Result r=media_work(m);assert(r.code==RESULT_UNSUPPORTED && strstr(r.detail,"Missing ImageMagick"));dispose(m);
    assert(!unlink(path));assert(media_allocate(path,PREVIEW_PNG,false,120,90,false,&m).code==RESULT_OK);
    assert(media_work(m).code!=RESULT_OK && m->source==-1 && !m->tool);dispose(m);
    assert(!rmdir(root));errno=0;assert(waitpid(-1,NULL,WNOHANG)==-1 && errno==ECHILD);
    puts("PASS: media source/tool preparation, converter normal return, unsupported/missing/access cleanup (in-process body; no fork cancellation guarantee)");
}
