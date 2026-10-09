#define _GNU_SOURCE
#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

struct PlatformExternal { pid_t pid; };
static pid_t active_launcher,detached_launcher;
static Result external_error(const char *what) {
    char detail[256];snprintf(detail,sizeof detail,"%s: %s",what,strerror(errno));
    return result_make(errno==ENOENT?RESULT_NOT_FOUND:errno==EACCES?RESULT_ACCESS:RESULT_IO,detail);
}
static char *program(const char *name) {
    if(strchr(name,'/')) return access(name,X_OK)==0?text_copy(name):NULL;
    const char *path=getenv("PATH");if(!path) return NULL;
    char *copy=text_copy(path);if(!copy) return NULL;
    char *save=NULL,*found=NULL;
    for(char *dir=strtok_r(copy,":",&save);dir;dir=strtok_r(NULL,":",&save)) {
        /* Never pick a selected directory's program from relative PATH entries. */
        if(*dir!='/') continue;
        char *candidate=platform_path_join(dir,name);struct stat st;
        if(candidate && access(candidate,X_OK)==0 && stat(candidate,&st)==0 && S_ISREG(st.st_mode)) {found=candidate;break;}
        free(candidate);
    }
    free(copy);return found;
}
Result platform_editor_setting(char **out) {
    *out=NULL;const char *s=getenv("VISUAL");if(!s||!*s) s=getenv("EDITOR");
    if(!s||!*s) {
        char *vi=program("vi");if(!vi) return result_make(RESULT_NOT_FOUND,"No VISUAL/EDITOR or available vi");
        free(vi);s="vi";
    }
    *out=text_copy(s);return *out?result_make(RESULT_OK,NULL):result_make(RESULT_NO_MEMORY,"Out of memory");
}
static Result target(const char *path,char **out) {
    *out=NULL;struct stat st;
    if(lstat(path,&st)<0) return external_error("Cannot inspect external target");
    if(!S_ISREG(st.st_mode)) return result_make(RESULT_UNSUPPORTED,"External tools accept cursor regular files only; no links/directories/special files");
    return platform_resolve(NULL,path,out);
}
Result platform_vim_available(void) {
    char *vim=program("vim");
    if(!vim) return result_make(RESULT_NOT_FOUND,"Vim unavailable; install vim with your package manager (Ubuntu/WSL: sudo apt install vim). No fallback.");
    free(vim);return result_make(RESULT_OK,NULL);
}
Result platform_editor_inspect(const char *path,unsigned char *sample,size_t capacity,size_t *length,bool *read_only) {
    *length=0;*read_only=false;
    char *resolved=NULL;Result r=target(path,&resolved);
    if(r.code!=RESULT_OK) return r;
    PlatformReader *reader=NULL;r=platform_reader_open(path,&reader);
    if(r.code==RESULT_OK) r=platform_reader_peek(reader,sample,capacity,length);
    bool changed=false;
    if(r.code==RESULT_OK) r=platform_reader_changed(reader,path,&changed);
    struct stat st;
    if(r.code==RESULT_OK && (changed || lstat(resolved,&st)<0 || !S_ISREG(st.st_mode)))
        r=result_make(RESULT_IO,"File changed while checking editor target; not opened");
    if(r.code==RESULT_OK) *read_only=!(st.st_mode&0222) || faccessat(AT_FDCWD,resolved,W_OK,AT_EACCESS)<0;
    platform_reader_close(reader);free(resolved);return r;
}
static void child_error(int fd,int error) {
    size_t at=0;
    while(at<sizeof error) {ssize_t n=write(fd,(char *)&error+at,sizeof error-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(size_t)n;}
    _exit(127);
}
static void close_extra(int error_fd) {
    if(error_fd!=3 && dup2(error_fd,3)<0) child_error(error_fd,errno);
    if(fcntl(3,F_SETFD,FD_CLOEXEC)<0) child_error(3,errno);
    DIR *dir=opendir("/proc/self/fd");if(!dir) child_error(3,errno);
    struct dirent *e;int scan=dirfd(dir);
    while((e=readdir(dir))) {
        char *end=NULL;long fd=strtol(e->d_name,&end,10);
        if(end&&!*end&&fd>3&&fd!=scan) close((int)fd);
    }
    closedir(dir);
}
static Result spawn(const char *tool,char **argv,bool terminal,pid_t *pid) {
    int pipefd[2];if(pipe2(pipefd,O_CLOEXEC)<0) return external_error("Cannot create exec acknowledgement");
    *pid=fork();
    if(*pid<0) {Result r=external_error("Cannot launch external tool");close(pipefd[0]);close(pipefd[1]);return r;}
    if(!*pid) {
        close(pipefd[0]);
        struct sigaction normal={0};normal.sa_handler=SIG_DFL;sigemptyset(&normal.sa_mask);
        sigaction(SIGINT,&normal,NULL);sigaction(SIGQUIT,&normal,NULL);sigaction(SIGTERM,&normal,NULL);sigaction(SIGHUP,&normal,NULL);sigaction(SIGPIPE,&normal,NULL);
        if(!terminal) {
            int null=open("/dev/null",O_RDWR|O_CLOEXEC);if(null<0) child_error(pipefd[1],errno);
            for(int i=0;i<3;i++) if(dup2(null,i)<0) child_error(pipefd[1],errno);
        }
        close_extra(pipefd[1]);
        execv(tool,argv); /* No execvp ENOEXEC shell fallback. */
        child_error(3,errno);
    }
    close(pipefd[1]);int error=0;size_t at=0;
    while(at<sizeof error) {
        ssize_t n=read(pipefd[0],(char *)&error+at,sizeof error-at);
        if(n<0&&errno==EINTR) continue;
        if(n<0) {error=errno;at=sizeof error;break;}
        if(!n) break;
        at+=(size_t)n;
    }
    close(pipefd[0]);
    if(at) {while(waitpid(*pid,NULL,0)<0&&errno==EINTR) {} errno=error?error:EIO;return external_error("External exec failed");}
    return result_make(RESULT_OK,NULL);
}
static Result exit_result(int status,const char *tool) {
    char detail[256];
    if(WIFEXITED(status)&&WEXITSTATUS(status)==0) return result_make(RESULT_OK,"External tool returned; display/save completion unconfirmed");
    if(WIFSIGNALED(status)) snprintf(detail,sizeof detail,"%s ended by signal %d",tool,WTERMSIG(status));
    else snprintf(detail,sizeof detail,"%s exited with status %d",tool,WIFEXITED(status)?WEXITSTATUS(status):-1);
    return result_make(RESULT_IO,detail);
}
static volatile sig_atomic_t editor_shutdown;
static void editor_request_shutdown(int number) { editor_shutdown|=number==SIGTERM?1:2; }
bool platform_editor_shutdown_requested(void) { return editor_shutdown!=0; }
Result platform_editor_run(char **configured,size_t count,const char *path) {
    char *file=NULL;Result r=target(path,&file);if(r.code!=RESULT_OK) return r;
    char *tool=program(configured[0]);if(!tool) {free(file);return result_make(RESULT_NOT_FOUND,"Editor program unavailable (VISUAL/EDITOR/vi)");}
    char **argv=calloc(count+2,sizeof *argv);if(!argv) {free(file);free(tool);return result_make(RESULT_NO_MEMORY,"Out of memory");}
    for(size_t i=0;i<count;i++) argv[i]=configured[i];
    argv[count]=file;
    const int signals[]={SIGINT,SIGQUIT,SIGTERM,SIGHUP};
    struct sigaction previous[4],action={0};size_t installed=0;
    editor_shutdown=0;sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask,SIGTERM);sigaddset(&action.sa_mask,SIGHUP);
    for(size_t i=0;i<4;i++) {
        action.sa_handler=i<2?SIG_IGN:editor_request_shutdown;
        if(sigaction(signals[i],&action,&previous[i])<0) {
            r=external_error("Cannot suspend editor signals");goto restore;
        }
        installed++;
    }
    /* A request caught by the app handler during terminal handoff precedes
       these handlers. Do not start a new editor for an already closing app. */
    if(platform_media_shutdown_requested()) {
        r=result_make(RESULT_CANCELLED,"Shutdown requested before editor launch");goto restore;
    }
    pid_t pid=0;r=spawn(tool,argv,true,&pid);
    if(r.code==RESULT_OK) {
        int status=0,forwarded=0;pid_t waited;
        /* A request may arrive during exec. Use polling waitpid to
           avoid the check/wait lost-wakeup race. No timeout or forced kill. */
        for(;;) {
            sig_atomic_t requested=editor_shutdown;
            if((requested&1) && !(forwarded&1)) {kill(pid,SIGTERM);forwarded|=1;}
            if((requested&2) && !(forwarded&2)) {kill(pid,SIGHUP);forwarded|=2;}
            waited=waitpid(pid,&status,WNOHANG);
            if(waited==pid || (waited<0&&errno!=EINTR)) break;
            struct timespec delay={0,20000000};nanosleep(&delay,NULL);
        }
        r=waited<0?external_error("Cannot wait for editor"):exit_result(status,!strcmp(configured[0],"vim")?"Vim":"Editor");
    }
restore:
    while(installed) {installed--;sigaction(signals[installed],&previous[installed],NULL);}
    free(argv);free(tool);free(file);return r;
}
bool platform_external_cleanup_pending(void) {
    if(detached_launcher) {
        pid_t n=waitpid(detached_launcher,NULL,WNOHANG);
        if(n>0 || (n<0&&errno==ECHILD)) detached_launcher=0;
    }
    return detached_launcher!=0;
}
Result platform_external_open(const char *path,PlatformExternal **out) {
    *out=NULL;
    if(active_launcher || platform_external_cleanup_pending()) return result_make(RESULT_EXISTS,"Previous external launcher still running");
    char *file=NULL;Result r=target(path,&file);if(r.code!=RESULT_OK) return r;
    char *tool=program("xdg-open");if(!tool) {free(file);return result_make(RESULT_NOT_FOUND,"Missing xdg-open; install xdg-utils");}
    PlatformExternal *job=calloc(1,sizeof *job);if(!job) {free(file);free(tool);return result_make(RESULT_NO_MEMORY,"Out of memory");}
    char *argv[]={tool,file,NULL};r=spawn(tool,argv,false,&job->pid);free(tool);free(file);
    if(r.code==RESULT_OK) {*out=job;active_launcher=job->pid;}else free(job);
    return r;
}
Result platform_external_poll(PlatformExternal *job,bool *done) {
    *done=false;int status=0;pid_t n=waitpid(job->pid,&status,WNOHANG);
    if(!n) return result_make(RESULT_OK,NULL);
    if(n<0&&errno==EINTR) return result_make(RESULT_OK,NULL);
    *done=true;active_launcher=0;job->pid=0;
    return n<0?external_error("Cannot collect external launcher"):exit_result(status,"xdg-open");
}
void platform_external_close(PlatformExternal *job) {
    if(!job) return;
    if(job->pid) {
        /* Never apply converter SIGKILL/time/resource limits to user tools.
           Collect later while this process lives; on exit the OS reparents it. */
        pid_t n;do {n=waitpid(job->pid,NULL,WNOHANG);} while(n<0&&errno==EINTR);
        if(n==0) detached_launcher=job->pid;
        active_launcher=0;
    }
    free(job);
}
bool platform_terminal_size(unsigned *rows,unsigned *columns) {
    struct winsize size;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&size)<0||!size.ws_row||!size.ws_col) return false;
    *rows=size.ws_row;*columns=size.ws_col;return true;
}
