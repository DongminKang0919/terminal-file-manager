#define _GNU_SOURCE
#include "../src/core/core.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool fail_save;
Result __real_platform_favorites_write(const char *,const char *,size_t);
Result __wrap_platform_favorites_write(const char *p,const char *d,size_t n) {
    return fail_save ? result_make(RESULT_IO,"Injected favorites write failure") : __real_platform_favorites_write(p,d,n);
}
static void ok(Result r) { if(r.code) fprintf(stderr,"%s\n",r.detail); assert(r.code==RESULT_OK); }
static void put(const char *p,const char *s) { FILE *f=fopen(p,"w");assert(f);assert(fputs(s,f)>=0);assert(!fclose(f)); }
int main(void) {
    char root[]="/tmp/tfile-favorites-XXXXXX";assert(mkdtemp(root));
    assert(!setenv("XDG_CONFIG_HOME",root,1));
    char *dir=core_path_join(root,"디렉터리\n"),*other=core_path_join(root,"other"),*alias=core_path_join(root,"alias");
    assert(dir&&other&&alias&&!mkdir(dir,0700)&&!mkdir(other,0700)&&!symlink(dir,alias));
    SettingsStore settings;settings_load(&settings);
    StartupSettings defaults=settings_defaults(); defaults.wheel_step=5;ok(settings_save(&settings,&defaults,false));
    Favorites f; favorites_load(&f); assert(!f.len&&f.load_result.code==RESULT_OK);
    ok(favorites_add(&f,dir,"홈\n\xff")); assert(f.len==1);
    assert(favorites_add(&f,alias,"duplicate").code==RESULT_EXISTS&&f.len==1);
    char long_name[130];memset(long_name,'a',129);long_name[129]=0;
    assert(favorites_rename(&f,0,long_name).code==RESULT_INVALID_NAME);
    long_name[128]=0;ok(favorites_rename(&f,0,long_name));
    fail_save=true;assert(favorites_rename(&f,0,"lost").code==RESULT_IO);assert(!strcmp(f.entries[0].name,long_name));
    assert(favorites_add(&f,other,"lost").code==RESULT_IO&&f.len==1);
    assert(favorites_remove(&f,0).code==RESULT_IO&&f.len==1);fail_save=false;
    Favorites reload; favorites_load(&reload);assert(reload.len==1&&!strcmp(reload.entries[0].name,long_name));favorites_free(&reload);
    AppState app;ok(app_init(&app,other));ok(app_navigate(&app,f.entries[0].directory));assert(app.history_len==2);ok(app_history(&app,false));assert(!strcmp(app.directory,other));
    assert(!rmdir(dir));FileInfo *old=app.files.entries;size_t history=app.history_at;
    assert(app_navigate(&app,f.entries[0].directory).code==RESULT_NOT_FOUND&&app.files.entries==old&&app.history_at==history&&!strcmp(app.directory,other));
    assert(!mkdir(dir,0700));ok(favorites_remove(&f,0));assert(!access(dir,F_OK));
    ok(favorites_add(&f,dir,"persist"));char *path=text_copy(f.path);favorites_free(&f);
    put(path,"version=1\n00\t2f\n");favorites_load(&f);assert(f.load_result.code!=RESULT_OK&&!f.len);
    assert(favorites_add(&f,dir,"blocked").code!=RESULT_OK);favorites_free(&f);
    assert(!unlink(path));assert(!symlink(settings.path,path));favorites_load(&f);assert(f.load_result.code!=RESULT_OK);
    assert(favorites_add(&f,dir,"blocked").code!=RESULT_OK);favorites_free(&f);
    SettingsStore check;settings_load(&check);assert(check.load_result.code==RESULT_OK&&check.defaults.wheel_step==5);
    settings_free(&check);settings_free(&settings);app_free(&app);
    free(path);free(dir);free(other);free(alias);ok(platform_remove(root));
    puts("PASS: favorites persistence/raw bytes/limits/canonical duplicates, failed save rollback, corrupt/symlink storage, preserved defaults/history and inaccessible navigation");
}
