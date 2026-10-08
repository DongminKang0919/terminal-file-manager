#define _GNU_SOURCE
#include "platform.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* GCC advertises ASan directly; Clang uses a feature query. */
#ifndef __has_feature
#define __has_feature(feature) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#define TFILE_ADDRESS_SANITIZER 1
#else
#define TFILE_ADDRESS_SANITIZER 0
#endif

/* A single converter at a time, including children being cancelled. */
struct PlatformMedia {
    pid_t pid;
    int source, output, diagnostic;
    struct stat identity;
    char *path, *tool;
    char directory[64], raster[96], prefix[96];
    PreviewMediaKind kind;
    bool text, pdf_stage, done;
    unsigned width, height;
    uint64_t started;
    Result result;
};
static PlatformMedia *retiring, *active;
static volatile sig_atomic_t shutdown_requested;
static struct sigaction previous_signals[3];
static bool handlers_installed;
static const int shutdown_signals[]={SIGINT,SIGTERM,SIGHUP};
static void request_shutdown(int signal_number) { (void)signal_number; shutdown_requested=1; }
void platform_media_install_signals(void) {
    struct sigaction action={0}; action.sa_handler=request_shutdown; sigemptyset(&action.sa_mask);
    for(size_t i=0;i<3;i++) sigaction(shutdown_signals[i],&action,&previous_signals[i]);
    handlers_installed=true; shutdown_requested=0;
}
bool platform_media_shutdown_requested(void) { return shutdown_requested!=0; }
static Result media_error(const char *what) {
    char detail[256]; snprintf(detail,sizeof detail,"%s: %s",what,strerror(errno));
    return result_make(errno==EACCES||errno==EPERM ? RESULT_ACCESS : RESULT_IO,detail);
}
static bool same_file(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static void dispose(PlatformMedia *m) {
    if(m->source>=0) close(m->source);
    if(m->output>=0) close(m->output);
    if(m->diagnostic>=0) close(m->diagnostic);
    if(m->directory[0]) {
        unlink(m->raster);
        DIR *d=opendir(m->directory);
        if(d) {
            struct dirent *entry;
            while((entry=readdir(d))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,".."))
                unlinkat(dirfd(d),entry->d_name,0);
            closedir(d);
        }
        /* All other artifacts are unlinked immediately after creation. */
        rmdir(m->directory);
    }
    free(m->path); free(m->tool); free(m);
}
void platform_media_reap(void) {
    if(!retiring) return;
    int status; pid_t p=waitpid(retiring->pid,&status,WNOHANG);
    if(p==retiring->pid || (p<0 && errno==ECHILD)) { dispose(retiring); retiring=NULL; }
}
void platform_media_close(PlatformMedia *m) {
    if(!m) return;
    if(active==m) active=NULL;
    if(m->pid>0) {
        kill(-m->pid,SIGKILL); kill(m->pid,SIGKILL);
        /* Start refuses to launch while a retired child remains; bounded to one. */
        retiring=m; platform_media_reap();
    } else dispose(m);
}
bool platform_media_cleanup_pending(void) { platform_media_reap(); return retiring!=NULL; }
void platform_media_shutdown(void) {
    if(handlers_installed) {
        for(size_t i=0;i<3;i++) sigaction(shutdown_signals[i],&previous_signals[i],NULL);
        handlers_installed=false;
    }
    if(!retiring) return;
    /* Exit only: SIGKILL is already sent. Never wait in input/selection handling. */
    while(waitpid(retiring->pid,NULL,0)<0 && errno==EINTR) {}
    dispose(retiring); retiring=NULL;
}
bool platform_terminal_pixels(unsigned *width,unsigned *height) {
    struct winsize s={0}; *width=*height=0;
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)) return false;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&s)==0 && s.ws_col && s.ws_row && s.ws_xpixel && s.ws_ypixel) {
        *width=s.ws_xpixel/s.ws_col; *height=s.ws_ypixel/s.ws_row;
    }
    return true;
}
static char *find_tool(const char *name) {
    const char *env=getenv("PATH"); if(!env) return NULL;
    char *paths=text_copy(env); if(!paths) return NULL;
    char *save=NULL, *found=NULL;
    for(char *p=strtok_r(paths,":",&save);p;p=strtok_r(NULL,":",&save)) {
        /* Ignore relative PATH entries; do not execute a selected directory's tools. */
        if(*p!='/') continue;
        char *candidate=platform_path_join(p,name);
        struct stat st;
        if(candidate && access(candidate,X_OK)==0 && stat(candidate,&st)==0 && S_ISREG(st.st_mode)) { found=candidate; break; }
        free(candidate);
    }
    free(paths); return found;
}
static int artifact(PlatformMedia *m,const char *name) {
    char path[128]; snprintf(path,sizeof path,"%s/%s",m->directory,name);
    int fd=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    if(fd>=0) unlink(path);
    return fd;
}
static void child_limit(int resource,rlim_t value) {
    struct rlimit r={value,value}; if(setrlimit(resource,&r)<0) _exit(126);
}
static Result launch(PlatformMedia *m) {
    char source[96], geometry[48], scale[24];
    snprintf(source,sizeof source,"/proc/self/fd/%d",m->source);
    snprintf(geometry,sizeof geometry,"%ux%u>",m->width,m->height);
    snprintf(scale,sizeof scale,"%u",m->width>m->height?m->width:m->height);
    char input[128];
    snprintf(input,sizeof input,"%s:%s",m->kind==PREVIEW_JPEG?"JPEG":"PNG",m->pdf_stage?m->raster:source);
    char *image_args[]={m->tool,"-limit","memory","128MiB","-limit","map","0","-limit","disk","0",
        "-limit","thread","1",input,"-auto-orient","-thumbnail",geometry,"-background","black",
        "-alpha","remove","-colors","64","sixel:-",NULL};
    char *pdf_args[]={m->tool,"-f","1","-l","1","-singlefile","-scale-to",scale,"-png",source,m->prefix,NULL};
    char *text_args[]={m->tool,"-f","1","-l","1","-layout",source,"-",NULL};
    bool rendering_pdf=m->kind==PREVIEW_PDF && !m->text && !m->pdf_stage;
    char **args=m->text?text_args:rendering_pdf?pdf_args:image_args;
    if(ftruncate(m->output,0)<0 || lseek(m->output,0,SEEK_SET)<0 ||
       ftruncate(m->diagnostic,0)<0 || lseek(m->diagnostic,0,SEEK_SET)<0) return media_error("Preview capture failed");
    pid_t parent=getpid();
    pid_t pid=fork(); if(pid<0) return media_error("Cannot start preview converter");
    if(pid==0) {
        if(setpgid(0,0)<0 || prctl(PR_SET_PDEATHSIG,SIGKILL)<0 || getppid()!=parent) _exit(126);
        /* ASan reserves terabytes of virtual shadow space before fork. Limiting
           that inherited address space prevents its exec interceptor from
           running. The normal binary always enforces the converter limit. */
#if !TFILE_ADDRESS_SANITIZER
        child_limit(RLIMIT_AS,512u*1024u*1024u);
#endif
        child_limit(RLIMIT_CPU,5);
        child_limit(RLIMIT_FSIZE,rendering_pdf?8u*1024u*1024u:m->text?PREVIEW_PDF_TEXT_BYTES:PREVIEW_SIXEL_BYTES);
        child_limit(RLIMIT_CORE,0);
        uint64_t elapsed=platform_monotonic_ms()-m->started;
        unsigned remaining=elapsed<PREVIEW_CONVERSION_MS?(unsigned)(PREVIEW_CONVERSION_MS-elapsed):1;
        alarm((remaining+999)/1000);
        int nullfd=open("/dev/null",O_RDONLY);
        if(nullfd<0 || dup2(nullfd,0)<0 || dup2(m->output,1)<0 || dup2(m->diagnostic,2)<0) _exit(126);
        close(nullfd);
        if(fcntl(m->source,F_SETFD,0)<0) _exit(126);
        /* Only the source descriptor survives exec besides standard capture FDs. */
        if(m->source>3) close_range(3,(unsigned)m->source-1,0);
        if(close_range((unsigned)m->source+1,~0u,0)<0) {
            long max=sysconf(_SC_OPEN_MAX);
            for(int fd=3;fd<max;fd++) if(fd!=m->source) close(fd);
        }
        setenv("LC_ALL","C",1); setenv("MAGICK_THREAD_LIMIT","1",1);
        setenv("MAGICK_TEMPORARY_PATH",m->directory,1);
        execv(m->tool,args); _exit(127);
    }
    setpgid(pid,pid); m->pid=pid;
    return result_make(RESULT_OK,NULL);
}
Result platform_media_open(const char *path,PreviewMediaKind kind,bool text,unsigned width,unsigned height,PlatformMedia **out) {
    *out=NULL; platform_media_reap();
    if(retiring || active) return result_make(RESULT_CANCELLED,"Previous preview is stopping");
    if(kind<PREVIEW_PNG || kind>PREVIEW_PDF || (text && kind!=PREVIEW_PDF) ||
        (!text && (!width||!height||width>PREVIEW_PIXEL_WIDTH||height>PREVIEW_PIXEL_HEIGHT||width>PREVIEW_PIXEL_COUNT/height)))
        return result_make(RESULT_UNSUPPORTED,"Unsupported preview request");
    PlatformMedia *m=calloc(1,sizeof *m);
    if(!m) return result_make(RESULT_NO_MEMORY,"Out of memory");
    m->source=m->output=m->diagnostic=-1;
    m->path=text_copy(path); m->kind=kind; m->text=text; m->width=width; m->height=height;
    Result r=result_make(RESULT_OK,NULL);
    m->source=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(m->source>=0 && m->source<3) {
        int fd=fcntl(m->source,F_DUPFD_CLOEXEC,3); close(m->source); m->source=fd;
    }
    if(m->source<0) { r=media_error("Cannot read preview source"); goto fail; }
    if(fstat(m->source,&m->identity)<0) { r=media_error("Cannot read preview metadata"); goto fail; }
    if(!S_ISREG(m->identity.st_mode)) { r=result_make(RESULT_UNSUPPORTED,"Preview source is not a regular file"); goto fail; }
    if(m->identity.st_size>64u*1024u*1024u) { r=result_make(RESULT_UNSUPPORTED,"Preview source exceeds 64 MiB limit"); goto fail; }
    unsigned char magic[8]; ssize_t n=pread(m->source,magic,sizeof magic,0);
    bool match=(kind==PREVIEW_PNG && n==8 && !memcmp(magic,"\211PNG\r\n\032\n",8)) ||
        (kind==PREVIEW_JPEG && n>=3 && magic[0]==255 && magic[1]==216 && magic[2]==255) ||
        (kind==PREVIEW_PDF && n>=5 && !memcmp(magic,"%PDF-",5));
    if(!match) { r=result_make(RESULT_UNSUPPORTED,"File signature changed or is unsupported"); goto fail; }
    m->tool=find_tool(text?"pdftotext":kind==PREVIEW_PDF?"pdftoppm":"magick");
    if(!m->tool && !text && kind!=PREVIEW_PDF) m->tool=find_tool("convert");
    if(!m->tool) { r=result_make(RESULT_UNSUPPORTED,text?"Missing pdftotext (Poppler)":kind==PREVIEW_PDF?"Missing pdftoppm (Poppler)":"Missing ImageMagick (magick or convert)"); goto fail; }
    if(!m->path) { r=result_make(RESULT_NO_MEMORY,"Out of memory"); goto fail; }
    snprintf(m->directory,sizeof m->directory,"/tmp/tfile-media-XXXXXX");
    if(!mkdtemp(m->directory)) { m->directory[0]=0; r=media_error("Cannot create preview temporary directory"); goto fail; }
    snprintf(m->prefix,sizeof m->prefix,"%s/page",m->directory);
    snprintf(m->raster,sizeof m->raster,"%s/page.png",m->directory);
    m->output=artifact(m,"output"); m->diagnostic=artifact(m,"diagnostic");
    if(m->output<0 || m->diagnostic<0) { r=media_error("Cannot create preview capture"); goto fail; }
    m->started=platform_monotonic_ms();
    r=launch(m); if(r.code!=RESULT_OK) goto fail;
    active=m; *out=m; return r;
fail:
    dispose(m); return r;
}
static Result unchanged(PlatformMedia *m) {
    struct stat current, opened;
    if(lstat(m->path,&current)<0 || fstat(m->source,&opened)<0) return media_error("Cannot verify preview source");
    if(!same_file(&m->identity,&current) || !same_file(&m->identity,&opened))
        return result_make(RESULT_IO,"File changed during conversion; refresh to retry");
    return result_make(RESULT_OK,NULL);
}
Result platform_media_poll(PlatformMedia *m,bool *done,char **data,size_t *len) {
    *data=NULL; *len=0; *done=m->done;
    if(m->done) return m->result;
    int status=0; pid_t p=waitpid(m->pid,&status,WNOHANG);
    if(p==0) {
        if(platform_monotonic_ms()-m->started < PREVIEW_CONVERSION_MS) return result_make(RESULT_OK,NULL);
        kill(-m->pid,SIGKILL); kill(m->pid,SIGKILL);
        m->result=result_make(RESULT_IO,"Preview conversion timed out (8 seconds)");
        /* Caller closes and retires the still-live child. */
        m->done=*done=true; return m->result;
    }
    if(p<0) {
        if(errno==EINTR) return result_make(RESULT_OK,NULL);
        m->result=media_error("Cannot collect converter"); if(errno==ECHILD) m->pid=0; goto finish;
    }
    m->pid=0;
    if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
        char error[256]={0}; ssize_t n=pread(m->diagnostic,error,sizeof error-1,0);
        if(n<0) error[0]=0;
        const char *reason;
        if(WIFSIGNALED(status) && WTERMSIG(status)==SIGALRM) reason="Preview conversion timed out (8 seconds)";
        else if(strstr(error,"Incorrect password") || strstr(error,"encrypted")) reason="Encrypted PDF requires a password";
        else if(WIFSIGNALED(status) && WTERMSIG(status)==SIGXFSZ) reason="Preview output exceeds size limit";
        else if(WIFSIGNALED(status)) reason="Converter exceeded resource limits or crashed";
        else if(WEXITSTATUS(status)==127 || WEXITSTATUS(status)==126) reason="Cannot execute preview converter";
        else if(strstr(error,"security policy") || strstr(error,"not authorized")) reason="ImageMagick policy disallows preview";
        else if(strstr(error,"no encode delegate") || strstr(error,"no decode delegate")) reason="ImageMagick is missing a required PNG/JPEG/SIXEL coder";
        else if(strstr(error,"improper image header") || strstr(error,"insufficient image data") ||
                strstr(error,"Not a JPEG") || strstr(error,"corrupt") || strstr(error,"CRC error")) reason="Damaged image";
        else if(m->kind==PREVIEW_PDF && !m->pdf_stage &&
                (strstr(error,"Syntax Error") || strstr(error,"xref") || strstr(error,"May not be a PDF"))) reason="Damaged PDF";
        else reason=m->kind==PREVIEW_PDF&&!m->pdf_stage?"PDF converter failed":"Image converter failed";
        char message[256]; snprintf(message,sizeof message,"%.120s: %.120s",reason,error);
        m->result=result_make(RESULT_IO,message); goto finish;
    }
    m->result=unchanged(m); if(m->result.code!=RESULT_OK) goto finish;
    if(platform_monotonic_ms()-m->started>=PREVIEW_CONVERSION_MS) {
        m->result=result_make(RESULT_IO,"Preview conversion timed out (8 seconds)"); goto finish;
    }
    if(m->kind==PREVIEW_PDF && !m->text && !m->pdf_stage) {
        free(m->tool); m->tool=find_tool("magick"); if(!m->tool) m->tool=find_tool("convert");
        if(!m->tool) { m->result=result_make(RESULT_UNSUPPORTED,"Missing ImageMagick (magick or convert)"); goto finish; }
        m->pdf_stage=true; m->result=launch(m);
        if(m->result.code==RESULT_OK) return m->result;
        goto finish;
    }
    struct stat st;
    if(fstat(m->output,&st)<0) { m->result=media_error("Cannot read converted preview"); goto finish; }
    size_t cap=m->text?PREVIEW_PDF_TEXT_BYTES:PREVIEW_SIXEL_BYTES;
    if(st.st_size<0 || (uint64_t)st.st_size>cap) { m->result=result_make(RESULT_IO,"Preview output exceeds size limit"); goto finish; }
    *len=(size_t)st.st_size; *data=calloc(*len+1,1);
    if(!*data) { m->result=result_make(RESULT_NO_MEMORY,"Out of memory"); goto finish; }
    if(pread(m->output,*data,*len,0)!=(ssize_t)*len) { free(*data); *data=NULL; *len=0; m->result=media_error("Cannot read converted preview"); }
finish:
    m->done=*done=true; return m->result;
}

static Result query_terminal(const char *query) {
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)) return result_make(RESULT_UNSUPPORTED,"Interactive terminal required");
    size_t at=0, length=strlen(query);
    while(at<length) {
        ssize_t n=write(STDOUT_FILENO,query+at,length-at);
        if(n<0&&errno==EINTR) continue;
        if(n<=0) return result_make(RESULT_IO,"Cannot write terminal query");
        at+=(size_t)n;
    }
    return result_make(RESULT_OK,NULL);
}
Result platform_terminal_query(void) { return query_terminal("\033[c\033[16t"); }
Result platform_terminal_query_cells(void) { return query_terminal("\033[16t"); }
TerminalTools platform_terminal_tools(void) {
    TerminalTools tools={0};
    char *p=find_tool("magick"); if(p) tools.image=1;
    else { p=find_tool("convert"); if(p) tools.image=2; }
    free(p); p=find_tool("pdftoppm"); tools.pdf_image=p!=NULL; free(p);
    p=find_tool("pdftotext"); tools.pdf_text=p!=NULL; free(p);
    return tools;
}
