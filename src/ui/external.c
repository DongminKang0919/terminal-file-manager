#include "ui.h"
bool external_prepare(UiContext *ui) {
    if(!ui->external_job) { (void)core_external_cleanup_pending();return false; }
    bool done=false;Result r=core_external_poll(ui->external_job,&done);
    if(done) {
        core_external_close(ui->external_job);ui->external_job=NULL;
        message(ui,r.code==RESULT_OK?"External open request returned; display/save unconfirmed":r.detail);
        ui->status_priority=true;ui->status_kind=r.code==RESULT_OK?NOTICE_INFO:NOTICE_ERROR;
        return true;
    }
    return false;
}
void external_entry(UiContext *ui,bool edit) {
    UiFilePanel *panel=ui_panel(ui);
    if(panel->selected>=panel->app.files.len) {message(ui,"No cursor file selected");ui->status_priority=true;return;}
    const FileInfo *file=&panel->app.files.entries[panel->selected];
    if(!file->valid||file->kind!=FILE_REGULAR) {message(ui,"External tools require a cursor regular file");ui->status_priority=true;return;}
    char *path=text_copy(file->path),*name=text_copy(file->name);
    if(!path||!name) {free(path);free(name);message(ui,"Out of memory; not launched");ui->status_priority=true;return;}
    if(edit) {
        bool read_only=false;Result checked=core_vim_check(path,&read_only);
        if(checked.code!=RESULT_OK) {message(ui,checked.detail);ui->status_priority=true;ui->status_kind=NOTICE_ERROR;free(path);free(name);return;}
    }
    Result outcome=result_make(RESULT_OK,NULL);
    if(!edit) {
        if(ui->external_job) {outcome=result_make(RESULT_EXISTS,"External launcher still running");message(ui,outcome.detail);}
        else { Result r=core_external_open(path,&ui->external_job);outcome=r;message(ui,r.code==RESULT_OK?"External open requested; display/save unconfirmed":r.detail); }
    } else {
        UiFocus focus=ui->focus;
        graphics_probe_cancel(ui);graphics_clear(ui);preview_reset(ui);core_media_shutdown();
        if(stdscr) {def_prog_mode();endwin();fflush(stdout);}
        Result r=core_vim_run(path);outcome=r;
        if(stdscr) {
            /* A killed Vim cannot send its normal terminal teardown. Restore
               legacy keyboard/paste/focus and motion modes before curses
               restores its own keypad and SGR mouse reporting. These are
               mode resets, not new input protocols enabled by this app. */
            fputs("\033[>4;0m\033[?2004l\033[?1004l\033[?1002l\033[?1003l",stdout);fflush(stdout);
            reset_prog_mode();unsigned rows=0,cols=0;if(core_terminal_size(&rows,&cols)) resizeterm((int)rows,(int)cols);
            cbreak();noecho();keypad(stdscr,TRUE);curs_set(0);input_init();terminal_input_reset();flushinp();
            /* A SIGWINCH received while the editor owned the terminal may
               still generate KEY_RESIZE after resizeterm/flushinp. Consume
               it before a subsequent menu can see it and close unexpectedly. */
            wtimeout(stdscr,0);for(int i=0;i<64 && input_key(stdscr)!=ERR;i++) {}
            wtimeout(stdscr,-1);flushinp();clearok(stdscr,TRUE);
        }
        core_media_install_signals();ui->focus=focus;
        Result refreshed=ui_refresh_panel(ui,ui->active,name,true);
        if(refreshed.code==RESULT_OK) for(size_t i=0;i<panel->app.files.len;i++) if(!strcmp(panel->app.files.entries[i].name,name)) panel->selected=i;
        Result peer=result_make(RESULT_OK,NULL);
        if(ui->mode==UI_LIST_LIST) peer=ui_refresh_panel(ui,ui->active^1u,NULL,true);
        preview_reset(ui);
        if(refreshed.code!=RESULT_OK) {outcome=refreshed;snprintf(ui->status,sizeof ui->status,"%.240s; refresh failed: %.240s",r.detail,refreshed.detail);}
        else if(peer.code!=RESULT_OK) {outcome=peer;snprintf(ui->status,sizeof ui->status,"%.240s; peer refresh failed: %.240s",r.detail,peer.detail);}
        else message(ui,r.detail);
    }
    ui->status_priority=true;
    ui->status_kind=outcome.code==RESULT_OK?NOTICE_INFO:NOTICE_ERROR;
    free(path);free(name);
}
