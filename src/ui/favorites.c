#include "ui.h"

void show_favorites(UiContext *ui) {
    int width=COLS-4; if(width>96) width=96;
    WINDOW *win=dialog_open(ui,"Favorites - active panel",LINES-2,width);
    if(!win) return;
    size_t selected=0,top=0; bool moved=false;
    char warning[256]="";
    if(ui->favorites.load_result.code!=RESULT_OK) snprintf(warning,sizeof warning,"%s",ui->favorites.load_result.detail);
    for(;;) {
        int h,w; getmaxyx(win,h,w); size_t rows=(size_t)(h-4);
        if(selected>=ui->favorites.len) selected=ui->favorites.len ? ui->favorites.len-1 : 0;
        if(selected<top) top=selected;
        if(selected>=top+rows) top=selected-rows+1;
        dialog_frame(win,"Favorites - active panel");
        if(!ui->favorites.len) draw_window_text(win,1,2,w-4,"No favorites. a: add current directory");
        for(size_t i=top;i<ui->favorites.len && i<top+rows;i++) {
            wattron(win,i==selected?ui_selection():A_NORMAL);
            draw_window_text(win,1+(int)(i-top),2,w-4,ui->favorites.entries[i].name);
            wattroff(win,ui_selection());
        }
        if(*warning) draw_window_text(win,h-3,2,w-4,warning);
        else if(ui->favorites.len) draw_window_path(win,h-3,2,w-4,ui->favorites.entries[selected].directory);
        draw_window_text(win,h-2,2,w-4,"Enter: go  a: add  r: rename  d: remove  Esc");
        dialog_refresh(win); int key=input_key(win);
        if(key==KEY_MOUSE) {
            MEVENT e; if(getmouse(&e)!=OK) continue;
            if(dialog_closed(win,&e)) break;
            int y,x; getbegyx(win,y,x);
            if(mouse_click(&e)&&e.x>x&&e.x<x+w-1&&e.y>y&&e.y<y+1+(int)rows) {
                size_t hit=top+(size_t)(e.y-y-1); if(hit<ui->favorites.len) selected=hit;
            }
            if(e.bstate&BUTTON4_PRESSED) key=KEY_UP;
            if(e.bstate&BUTTON5_PRESSED) key=KEY_DOWN;
        }
        if(key==27||key==KEY_RESIZE) break;
        if(key==KEY_UP&&selected) selected--;
        if(key==KEY_DOWN&&selected+1<ui->favorites.len) selected++;
        if(key==KEY_HOME) selected=0;
        if(key==KEY_END&&ui->favorites.len) selected=ui->favorites.len-1;
        if(key==KEY_NPAGE&&ui->favorites.len) selected=selected+rows<ui->favorites.len ? selected+rows : ui->favorites.len-1;
        if(key==KEY_PPAGE) selected=selected>rows ? selected-rows : 0;
        if((key=='\n'||key==KEY_ENTER)&&ui->favorites.len) {
            /* navigate commits list/history only after a readable destination. */
            if(navigate(ui,ui->favorites.entries[selected].directory,NULL)) { moved=true; break; }
            snprintf(warning,sizeof warning,"%.255s",ui->status);
        }
        if(key=='a'||(key=='r'&&ui->favorites.len)) {
            char label[FAVORITE_NAME_BYTES+1]; bool resized=false;
            char *initial=key=='a' ? core_path_name(ui_panel(ui)->app.directory) : text_copy(ui->favorites.entries[selected].name);
            if(!initial) { snprintf(warning,sizeof warning,"Out of memory"); continue; }
            bool accepted=prompt_value_status(ui,"Favorite name (1-128 bytes)",label,sizeof label,initial,&resized);
            free(initial); if(resized) break;
            if(accepted) {
                Result r=key=='a' ? favorites_add(&ui->favorites,ui_panel(ui)->app.directory,label) : favorites_rename(&ui->favorites,selected,label);
                snprintf(warning,sizeof warning,"%s",r.code==RESULT_OK?"Favorite saved (startup settings unchanged)":r.detail);
            }
        }
        if(key=='d'&&ui->favorites.len) {
            Result r=favorites_remove(&ui->favorites,selected);
            snprintf(warning,sizeof warning,"%s",r.code==RESULT_OK?"Unregistered; directory was not deleted":r.detail);
        }
    }
    dialog_close(ui,win);
    if(moved) message(ui,"Opened favorite in active panel");
}
