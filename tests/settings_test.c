#define _GNU_SOURCE
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_write,fail_close,fail_rename,fail_open,fail_sync,collide, keys[20], key_at;
static char collision[80];
static const char *expected_warning;
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *p,size_t n) {
    if(fail_write) { errno=ENOSPC; return -1; }
    return __real_write(fd,p,n);
}
int __real_close(int);
int __wrap_close(int fd) {
    if(fail_close && fd==fail_close) { fail_close=0; __real_close(fd); errno=EIO; return -1; }
    return __real_close(fd);
}
int __real_openat(int,const char *,int,...);
int __wrap_openat(int dir,const char *p,int flags,...) {
    if(fail_open && !strcmp(p,"settings.conf")) { errno=EACCES; return -1; }
    if(collide && (flags&O_EXCL)) {
        collide=0; snprintf(collision,sizeof collision,"%s",p);
        assert(!symlinkat("settings.conf",dir,p));
    }
    int fd=__real_openat(dir,p,flags,0600);
    if(fail_close==-1 && (flags&O_EXCL) && fd>=0) fail_close=fd;
    return fd;
}
int __real_fsync(int);
int __wrap_fsync(int fd) { if(fail_sync) { errno=EIO; return -1; } return __real_fsync(fd); }
int __real_renameat(int,const char *,int,const char *);
int __wrap_renameat(int a,const char *b,int c,const char *d) {
    if(fail_rename) { errno=EACCES; return -1; }
    return __real_renameat(a,b,c,d);
}
int __wrap_input_key(WINDOW *win) {
    int h=getmaxy(win),w=getmaxx(win);
    assert(h>=7 && w>=46);
    /* The current item, result and help occupy distinct rows at 50x9. */
    assert(h-4<h-3 && h-3<h-2);
    if(key_at==7 && expected_warning) {
        char text[128]; mvwinnstr(win,h-3,2,text,w-4);
        assert(strstr(text,expected_warning));
        mvwinnstr(win,h-4,2,text,w-4);
        assert(strstr(text,"Save current settings"));
        assert(mvwinch(win,h-4,2)&(A_REVERSE|A_BOLD));
    }
    return keys[key_at++];
}
static void put(const char *p,const char *data,size_t n) {
    int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0600); assert(fd>=0);
    assert(__real_write(fd,data,n)==(ssize_t)n); assert(!__real_close(fd));
}
static void no_temp(const char *p) {
    DIR *d=opendir(p); assert(d); struct dirent *e;
    while((e=readdir(d))) assert(strncmp(e->d_name,".settings-",10));
    closedir(d);
}
static void expect_file(const char *p,const char *expected) {
    char b[4097]; size_t n; assert(platform_settings_read(p,b,sizeof b,&n).code==RESULT_OK);
    assert(n==strlen(expected)&&!memcmp(b,expected,n));
}
int main(void) {
    char root[]="/tmp/tfile-settings-XXXXXX"; assert(mkdtemp(root));
    assert(!setenv("HOME",root,1)); assert(!setenv("XDG_CONFIG_HOME","relative",1));
    SettingsStore store; settings_load(&store);
    char path[1024],dir[1024]; snprintf(path,sizeof path,"%s/.config/tfile/settings.conf",root);
    snprintf(dir,sizeof dir,"%s/.config/tfile",root);
    assert(!strcmp(store.path,path)&&store.load_result.code==RESULT_OK);
    assert(store.defaults.mode==1&&store.defaults.show_hidden&&store.defaults.image_auto&&store.defaults.wheel_step==1);
    StartupSettings saved={.mode=2,.wheel_step=5,.sort={SORT_SIZE,true},.show_hidden=false,.image_auto=false};
    assert(settings_save(&store,&saved,false).code==RESULT_OK); settings_free(&store);
    settings_load(&store); assert(store.defaults.mode==2&&store.defaults.sort.key==SORT_SIZE&&store.defaults.sort.descending&&!store.defaults.show_hidden&&!store.defaults.image_auto);
    settings_free(&store);
    UiContext ui; assert(ui_init(&ui,root).code==RESULT_OK);
    assert(ui.mode==UI_LIST_LIST&&ui.second_initialized);
    assert(!strcmp(ui.panels[0].app.directory,root)&&!strcmp(ui.panels[1].app.directory,root));
    assert(ui.panels[0].app.sort.key==SORT_SIZE&&ui.panels[1].app.sort.key==SORT_SIZE);
    assert(!ui.sixel_confirmed && !ui.cell_width && !ui.cell_height);
    ui_activate_panel(&ui,1); ui.panels[1].app.sort=(SortSettings){SORT_KIND,false}; ui.panels[1].app.show_hidden=true;
    ui.panels[0].selected=3; ui.panels[0].top=2;
    assert(app_mark_toggle(&ui.panels[0].app,"marker").code==RESULT_OK);
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in); SCREEN *screen=newterm("xterm",out,in); assert(screen);
    resizeterm(9,50);
    for(int i=0;i<6;i++) keys[i]=KEY_DOWN;
    keys[6]='\n'; keys[7]=27; key_at=0; expected_warning="Saved startup defaults"; show_options(&ui);
    assert(key_at==8 && ui.active==1 && ui.mode==UI_LIST_LIST);
    assert(ui.panels[0].selected==3&&ui.panels[0].top==2&&ui.panels[0].app.sort.key==SORT_SIZE&&!ui.panels[0].app.show_hidden&&app_marked(&ui.panels[0].app,"marker"));
    assert(ui.settings.defaults.sort.key==SORT_KIND&&ui.settings.defaults.show_hidden);
    UiFocus focus=ui.focus; assert(focus==UI_FOCUS_FILES);
    fail_write=1; key_at=0; expected_warning="No space left"; show_options(&ui); fail_write=0; expected_warning=NULL;
    assert(key_at==8 && ui.active==1 && ui.focus==focus); no_temp(dir);
    ui.image_auto=true;
    saved.image_auto=true;
    assert(settings_save(&ui.settings,&saved,false).code==RESULT_OK);
    ui_free(&ui);
    assert(!unsetenv("TFILE_SIXEL")); assert(!unsetenv("TFILE_CELL_PIXELS"));
    assert(ui_init(&ui,root).code==RESULT_OK); assert(ui.image_auto); graphics_init(&ui); assert(!ui.sixel_confirmed); ui_free(&ui);
    const char *original="version=1\nmode=1\n"; put(path,original,strlen(original));
    settings_load(&store);
    fail_write=1; assert(settings_save(&store,&saved,false).code!=RESULT_OK); fail_write=0; expect_file(path,original); no_temp(dir);
    fail_close=-1; assert(settings_save(&store,&saved,false).code!=RESULT_OK); expect_file(path,original); no_temp(dir);
    fail_rename=1; assert(settings_save(&store,&saved,false).code!=RESULT_OK); fail_rename=0; expect_file(path,original); no_temp(dir);
    fail_sync=1; assert(settings_save(&store,&saved,false).code!=RESULT_OK); fail_sync=0; expect_file(path,original); no_temp(dir);
    assert(!chmod(dir,0500)); assert(settings_save(&store,&saved,false).code!=RESULT_OK); expect_file(path,original); assert(!chmod(dir,0700));
    collide=1; assert(settings_save(&store,&saved,false).code==RESULT_OK);
    char collision_path[1200]; snprintf(collision_path,sizeof collision_path,"%s/%s",dir,collision);
    struct stat st; assert(!lstat(collision_path,&st)&&S_ISLNK(st.st_mode)); assert(!unlink(collision_path)); no_temp(dir);
    settings_free(&store);
    const char *damaged="version=2\n"; put(path,damaged,strlen(damaged));
    assert(ui_init(&ui,root).code==RESULT_OK); assert(ui.settings_warning[0]);
    message(&ui,"Routine message"); draw_cached(&ui);
    char status[128]; mvinnstr(LINES-2,0,status,sizeof status-1); assert(strstr(status,"Settings (F7)"));
    key_at=0; show_options(&ui); expect_file(path,damaged); /* First Enter only requests confirmation. */
    notice_dismiss(&ui); assert(!ui.settings_warning[0]&&ui.settings.replace_required);
    keys[7]='\n'; keys[8]=27; key_at=0; show_options(&ui);
    assert(key_at==9&&!ui.settings.replace_required); ui_free(&ui); keys[7]=27;
    const char *bad[]={"version=2\n", "version=1\nmode=9\n", "version=1\nhidden=2\n", "version=1\nwheel=2\n", "version=1\nsort=4\n", "version=1\nimage=Auto\n", "version=1\nmode=0\nmode=1\n", "version=1\nsyntax\n", "mode=0\n", "version=1\ndescending=2\n"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++) {
        put(path,bad[i],strlen(bad[i])); settings_load(&store);
        assert(store.load_result.code!=RESULT_OK&&store.replace_required&&store.defaults.mode==1);
        assert(settings_save(&store,&saved,false).code==RESULT_EXISTS); expect_file(path,bad[i]);
        assert(settings_save(&store,&saved,true).code==RESULT_OK); settings_free(&store);
    }
    StartupSettings parsed=settings_defaults();
    const char *unknown="# comment\nversion=1\nfuture=value\nfuture=other\nmode=0\n";
    assert(settings_parse(unknown,strlen(unknown),&parsed).code==RESULT_OK&&parsed.mode==0);
    char huge[4097]; memset(huge,'x',sizeof huge); put(path,huge,sizeof huge);
    settings_load(&store); assert(store.replace_required); settings_free(&store);
    assert(settings_parse(huge,129,&parsed).code!=RESULT_OK);
    assert(settings_parse("version=1\n\0",11,&parsed).code!=RESULT_OK);
    put(path,original,strlen(original)); fail_open=1; settings_load(&store); assert(store.replace_required&&store.load_result.code==RESULT_ACCESS); fail_open=0;
    assert(settings_save(&store,&saved,false).code==RESULT_EXISTS); expect_file(path,original); settings_free(&store);
    assert(!unlink(path)); assert(!symlink("/dev/null",path)); settings_load(&store); assert(store.replace_required); assert(settings_save(&store,&saved,true).code!=RESULT_OK); settings_free(&store); no_temp(dir); assert(!unlink(path));
    assert(!mkfifo(path,0600)); settings_load(&store); assert(store.replace_required); assert(settings_save(&store,&saved,true).code!=RESULT_OK); settings_free(&store); assert(!unlink(path));
    assert(!unsetenv("HOME")); settings_load(&store); assert(!store.path&&store.load_result.code!=RESULT_OK); assert(settings_save(&store,&saved,true).code!=RESULT_OK); settings_free(&store);
    assert(!setenv("XDG_CONFIG_HOME",root,1)); settings_load(&store); assert(store.path&&store.load_result.code==RESULT_OK); settings_free(&store);
    char moved[1024]; snprintf(moved,sizeof moved,"%s/moved",root);
    assert(!rename(dir,moved)); assert(!symlink(moved,dir));
    assert(!setenv("HOME",root,1)); assert(!unsetenv("XDG_CONFIG_HOME"));
    settings_load(&store); assert(store.replace_required); assert(settings_save(&store,&saved,true).code!=RESULT_OK); settings_free(&store);
    assert(!unlink(dir)); assert(!rmdir(moved)); snprintf(dir,sizeof dir,"%s/.config",root); assert(!rmdir(dir)); assert(!rmdir(root));
    endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: settings parsing, restart, panel isolation, 50x9 options, atomic failure preservation and path safety");
}
