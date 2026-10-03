#include "ui.h"

/* Stream wrapped source bytes into the viewport; no truncated name buffer. */
static void review_text(WINDOW *w,const char *text,size_t top,size_t page,size_t *line,int width) {
    size_t at=0;
    do {
        if(*line>=top && *line-top<page) draw_window_page(w,1+(int)(*line-top),2,width,text,at);
        size_t next=ui_text_span(text,at,width).end;
        (*line)++; if(next==at && text[at]) break; at=next;
    } while(text[at]);
}
static bool review(UiContext *ui,const BatchJob *job,bool *resized) {
    const char *title=job->action==BATCH_DELETE ? "Batch delete confirmation" : job->action==BATCH_MOVE ? "Batch move confirmation" : "Batch copy confirmation";
    WINDOW *w=dialog_open(ui,title,LINES-2,88); if(!w) return false;
    bool run=false,accepted=false; size_t top=0;
    size_t kinds[4]={0}; for(size_t i=0;i<job->len;i++) kinds[job->targets[i].kind]++;
    for(;;) {
        int h,width; getmaxyx(w,h,width); size_t page=(size_t)(h-4),line=0;
        dialog_frame(w,title); char summary[160];
        snprintf(summary,sizeof summary,"Targets: %zu | Files: %zu Dirs: %zu Links: %zu Other: %zu",job->len,kinds[FILE_REGULAR],kinds[FILE_DIRECTORY],kinds[FILE_LINK],kinds[FILE_OTHER]);
        review_text(w,summary,top,page,&line,width-4);
        review_text(w,job->action==BATCH_DELETE ? "Directories include all contents. Deleted items cannot be restored." : "Original names; no overwrite or merge. Stop at first error or cancellation.",top,page,&line,width-4);
        if(job->directory) { review_text(w,"Destination directory:",top,page,&line,width-4); review_text(w,job->directory,top,page,&line,width-4); }
        for(size_t i=0;i<job->len;i++) {
            snprintf(summary,sizeof summary,"Target %zu/%zu:",i+1,job->len); review_text(w,summary,top,page,&line,width-4);
            review_text(w,job->targets[i].source,top,page,&line,width-4);
        }
        size_t max=line>page?line-page:0; if(top>max) { top=max; continue; }
        snprintf(summary,sizeof summary,"Lines %zu-%zu/%zu  PgUp/PgDn",top+1,top+page<line?top+page:line,line);
        draw_window_text(w,h-3,2,width-4,summary);
        const char *label=job->action==BATCH_DELETE ? "[ Delete ]" : "[ Execute ]";
        dialog_button(w,h-2,2,"[ Cancel ]",!run,true); dialog_button(w,h-2,14,label,run,true);
        draw_window_text(w,h-2,28,width-30,"Tab: focus"); dialog_refresh(w);
        int key=input_key(w);
        if(key==KEY_MOUSE) {
            MEVENT e;if(getmouse(&e)!=OK) continue;
            if(dialog_closed(w,&e)) break;
            int y,x;getbegyx(w,y,x);
            if(!wenclose(w,e.y,e.x)) continue;
            if(e.bstate&BUTTON4_PRESSED) key=KEY_UP;
            else if(e.bstate&BUTTON5_PRESSED) key=KEY_DOWN;
            else if(mouse_click(&e)&&e.y==y+h-2) {
                if(e.x>=x+2&&e.x<x+12) break;
                if(e.x>=x+14&&e.x<x+14+(int)strlen(label)) { accepted=true;break; }
            }
        }
        if(key==27||key=='x'||key==KEY_RESIZE) { if(key==KEY_RESIZE && resized) *resized=true; break; }
        if(key=='\t'||key==KEY_BTAB||key==KEY_LEFT||key==KEY_RIGHT) run=!run;
        if(key=='\n'||key==KEY_ENTER) { accepted=run; break; }
        if(key==KEY_UP&&top) top--;
        if(key==KEY_DOWN&&top<max) top++;
        if(key==KEY_PPAGE) top=top>page?top-page:0;
        if(key==KEY_NPAGE) top=max-top<page?max:top+page;
        if(key==KEY_HOME) top=0;
        if(key==KEY_END) top=max;
    }
    dialog_close(ui,w);return accepted;
}
bool batch_review(UiContext *ui,const BatchJob *job) { return review(ui,job,NULL); }
void batch_finish(UiContext *ui,BatchJob *job) {
    /* Remove successes before refresh: even a failed refresh cannot retain them. */
    app_marks_apply_result(&ui_panel(ui)->app,job);
    notice_record(ui,job->action==BATCH_DELETE?"Batch delete":job->action==BATCH_MOVE?"Batch move":"Batch copy",job->result,NULL,NULL);
    ui->notice.batch=*job; *job=(BatchJob){0}; /* transfer ownership, no result allocation */
    if(ui->notice.batch.executed) {
        ui->notice.refresh=load_dir(ui,NULL); ui->notice.refresh_attempted=true;
    }
}
/* Own the frozen job throughout editing; only execution transfers it to notice. */
static void batch_transfer_form(UiContext *ui,BatchJob *job) {
    const char *title="Batch destination directory";
    char *base=text_copy(ui_panel(ui)->app.directory);
    UiField field;
    if(!base || !field_init(&field,base)) {
        free(base); message(ui,"Path exceeds input limit / out of memory"); return;
    }
    WINDOW *win=dialog_open(ui,title,9,88);
    if(!win) { free(base); return; }
    enum { DESTINATION, BROWSE, NEXT, CANCEL, FIELDS };
    const char *buttons[]={"[ Browse ]","[ Next ]","[ Cancel ]"};
    const int xs[]={2,15,26};
    int focus=DESTINATION;
    char warning[512]="";
    for(;;) {
        int h,w; getmaxyx(win,h,w);
        dialog_frame(win,title);
        draw_window_text(win,1,2,6,"Base:");
        draw_window_text(win,1,8,w-10,base);
        draw_window_text(win,2,2,w-4,"Relative paths use Base; blank uses Base.");
        draw_window_text(win,3,2,6,"To:");
        int col=field_draw(win,3,8,w-10,&field,focus==DESTINATION);
        wattron(win,COLOR_PAIR(*warning?UI_SPECIAL:UI_MUTED));
        draw_window_text(win,h-3,2,w-4,*warning?warning:"Enter: review targets  Tab: focus  Esc: cancel");
        wattroff(win,COLOR_PAIR(*warning?UI_SPECIAL:UI_MUTED));
        for(int i=0;i<3;i++) dialog_button(win,h-2,xs[i],buttons[i],focus==i+1,true);
        curs_set(focus==DESTINATION); if(focus==DESTINATION) wmove(win,3,8+col);
        dialog_refresh(win);
        wint_t key; int kind=input_wide(win,&key),action=-1;
        if(kind==ERR) continue;
        if((kind==OK&&key==27)||(kind==KEY_CODE_YES&&key==KEY_RESIZE)) break;
        if(kind==KEY_CODE_YES&&key==KEY_MOUSE) {
            MEVENT e; if(getmouse(&e)!=OK) continue;
            if(dialog_closed(win,&e)) break;
            if(!mouse_click(&e)) continue;
            int y,x; getbegyx(win,y,x); y=e.y-y; x=e.x-x;
            if(y==3&&x>=8&&x<w-2) { focus=DESTINATION; field_click(&field,x-8); continue; }
            if(y==h-2) for(int i=0;i<3;i++)
                if(x>=xs[i]&&x<xs[i]+(int)strlen(buttons[i])) action=focus=i+1;
        }
        if(kind==OK&&key=='\t') { focus=(focus+1)%FIELDS; continue; }
        if(kind==KEY_CODE_YES&&key==KEY_BTAB) { focus=(focus+FIELDS-1)%FIELDS; continue; }
        if((kind==OK&&(key=='\n'||key=='\r'))||(kind==KEY_CODE_YES&&key==KEY_ENTER)) action=focus==DESTINATION?NEXT:focus;
        if(action<0) { if(focus==DESTINATION) field_edit(&field,kind,key); continue; }
        if(action==CANCEL) break;
        char input[UI_INPUT_CAP];
        if(!ui_text_encode(field.value,field.len,input,sizeof input)) {
            snprintf(warning,sizeof warning,"Destination: input is too long"); continue;
        }
        int old_h=LINES,old_w=COLS; bool resized=false;
        char *resolved=NULL;
        Result r=core_resolve_directory(base,*input?input:".",&resolved);
        if(action==BROWSE) {
            char picked[UI_INPUT_CAP];
            if(pick_path(ui,true,r.code==RESULT_OK?resolved:base,picked,&resized)) {
                field_init(&field,picked); warning[0]=0;
            }
        } else {
            if(r.code==RESULT_OK) r=batch_destination(job,resolved);
            if(r.code!=RESULT_OK) snprintf(warning,sizeof warning,"Destination: %.450s",r.detail);
            else if(review(ui,job,&resized)) {
                free(resolved); dialog_close(ui,win); free(base);
                run_batch_operation(ui,job); batch_finish(ui,job); return;
            }
        }
        free(resolved);
        if(resized||old_h!=LINES||old_w!=COLS) break;
        touchwin(win);
    }
    dialog_close(ui,win); free(base);
}
void batch_entry(UiContext *ui,BatchAction action) {
    BatchJob job; Result r=batch_prepare(&ui_panel(ui)->app,ui_panel(ui)->selected,action,&job);
    if(r.code!=RESULT_OK) {
        notice_record(ui,action==BATCH_DELETE?"Batch delete":action==BATCH_MOVE?"Batch move":"Batch copy",r,NULL,NULL);return;
    }
    if(action!=BATCH_DELETE) {
        batch_transfer_form(ui,&job);
        batch_free(&job);
        return;
    }
    if(batch_review(ui,&job)) run_batch_operation(ui,&job); else batch_cancel(&job);
    batch_finish(ui,&job);
}
