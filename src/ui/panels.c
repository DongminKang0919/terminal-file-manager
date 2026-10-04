#include "ui.h"

Result ui_init(UiContext *ui,const char *directory) {
    *ui=(UiContext){.mode=UI_LIST_PREVIEW,.wheel_step=1};
    return app_init(&ui_panel(ui)->app,directory);
}
void ui_free(UiContext *ui) {
    notice_clear(ui); preview_reset(ui);
    for(unsigned i=0;i<2;i++) app_free(&ui->panels[i].app);
    *ui=(UiContext){0};
}
Result ui_refresh_panel(UiContext *ui,unsigned index,const char *highlight,bool require_fresh) {
    UiFilePanel *p=&ui->panels[index];
    size_t previous=p->selected;
    const char *wanted=highlight ? highlight : previous<p->app.files.len ? p->app.files.entries[previous].name : NULL;
    char *name=wanted ? text_copy(wanted) : NULL;
    Result r=wanted&&!name ? result_make(RESULT_NO_MEMORY,"Out of memory") : app_refresh(&p->app);
    if(r.code==RESULT_OK) {
        p->stale=false;
        p->selected=p->app.files.len ? (previous<p->app.files.len ? previous : p->app.files.len-1) : 0;
        if(name) for(size_t i=0;i<p->app.files.len;i++)
            if(!strcmp(p->app.files.entries[i].name,name)) { p->selected=i; break; }
        int rows=stdscr&&LINES>=9 ? ui_screen_layout(ui,COLS,LINES).list.list_rows : 1;
        if(p->selected<p->top) p->top=p->selected;
        if(rows>0&&p->selected>=p->top+(size_t)rows) p->top=p->selected-(size_t)rows+1;
    } else if(require_fresh || p->stale) p->stale=true;
    free(name); return r;
}
Result ui_set_mode(UiContext *ui,UiMode mode) {
    if(mode<UI_LIST_ONLY||mode>UI_LIST_LIST) return result_make(RESULT_UNSUPPORTED,"Unknown screen mode");
    if(mode==ui->mode) return result_make(RESULT_OK,NULL);
    Result r=result_make(RESULT_OK,NULL);
    unsigned other=ui->active^1u;
    if(mode==UI_LIST_LIST) {
        if(!ui->second_initialized) {
            UiFilePanel candidate={0};
            r=app_init(&candidate.app,ui_panel(ui)->app.directory);
            if(r.code!=RESULT_OK) { app_free(&candidate.app); message(ui,r.detail); ui->status_kind=NOTICE_ERROR; ui->status_priority=true; return r; }
            ui->panels[other]=candidate; /* Move ownership only after full initialization. */
            ui->second_initialized=true;
        } else {
            r=ui_refresh_panel(ui,other,NULL,true);
        }
    } else if(ui->mode==UI_LIST_LIST) {
        app_marks_clear(&ui->panels[other].app);
    }
    ui->mode=mode;
    if(mode!=UI_LIST_PREVIEW) { preview_reset(ui); ui->focus=UI_FOCUS_FILES; }
    if(r.code!=RESULT_OK) {
        ui->status_kind=NOTICE_WARNING; ui->status_priority=true;
        snprintf(ui->status,sizeof ui->status,"%s panel stale; refresh with r before file operations: %.220s",other?"Right":"Left",r.detail);
    } else if(ui->status_priority) {
        ui->status_priority=false;
        ui->status[0]='\0';
    }
    return r;
}
bool ui_activate_panel(UiContext *ui,unsigned panel) {
    if(ui->mode!=UI_LIST_LIST || panel>1 || ui->modal_depth) return false;
    ui->active=panel; ui->focus=UI_FOCUS_FILES;
    return true; /* Loaded state only: no I/O, sorting or preview preparation. */
}
bool ui_operation_allowed(UiContext *ui,char *warning,size_t size) {
    if(!ui_panel(ui)->stale) return true;
    const char *text="Stale panel: refresh with r before file operations";
    if(warning&&size) snprintf(warning,size,"%s",text);
    message(ui,text); ui->status_kind=NOTICE_WARNING; ui->status_priority=true; return false;
}
void refresh_peer_after_operation(UiContext *ui) {
    if(ui->mode!=UI_LIST_LIST) return;
    ui->notice.peer_refresh=ui_refresh_panel(ui,ui->active^1u,NULL,true);
    ui->notice.peer_refresh_attempted=true;
}
void refresh_operation_lists(UiContext *ui,const char *highlight) {
    ui->notice.refresh=load_dir(ui,highlight);
    ui->notice.refresh_attempted=true;
    refresh_peer_after_operation(ui);
    /* Both refresh outcomes belong to this operation's retained notice. Show
       its outcome plus refresh failure, rather than a transient load error. */
    ui->status_priority=false;
}
