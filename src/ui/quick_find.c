#include "ui.h"

/* This input loop owns input until commit/cancel; ordinary letters are data.
   Matching consumes only the loaded list in its existing order. */
void quick_find(UiContext *ui) {
    size_t selected = ui_panel(ui)->selected, top = ui_panel(ui)->top;
    UiFocus focus = ui->focus;
    UiField field; field_init(&field, "");
    char term[UI_INPUT_CAP] = "", previous[UI_INPUT_CAP] = "";
    bool found = ui_panel(ui)->app.files.len != 0, encoded = true;
    ui->focus = UI_FOCUS_FILES;
    for (;;) {
        draw(ui);
        int h,w; getmaxyx(stdscr,h,w);
        if (h < 9 || w < 50) break;
        attrset(COLOR_PAIR(UI_STATUS)); mvhline(h-2,0,' ',w);
        draw_text(h-2,1,12,!encoded ? "Too long:" : found ? "Find:" : "No match:");
        int cursor = field_draw(stdscr,h-2,11,w-12,&field,true);
        attrset(ui_bar()); mvhline(h-1,0,' ',w);
        draw_text(h-1,1,w-2,"Enter: select  Esc: undo  Up/Down: match");
        attrset(A_NORMAL); curs_set(1); move(h-2,11+cursor); refresh();
        wint_t key; int kind=input_wide(stdscr,&key);
        if (kind==ERR) continue;
        if ((kind==OK && key==27) || (kind==KEY_CODE_YES && key==KEY_RESIZE)) break;
        if ((kind==OK && (key=='\n' || key=='\r')) || (kind==KEY_CODE_YES && key==KEY_ENTER)) {
            selected=ui_panel(ui)->selected; top=ui_panel(ui)->top; break;
        }
        bool next=kind==KEY_CODE_YES && key==KEY_DOWN;
        bool prev=kind==KEY_CODE_YES && key==KEY_UP;
        if (!next && !prev && !field_edit(&field,kind,key)) continue;
        encoded=ui_text_encode(field.value,field.len,term,sizeof term);
        if (!encoded) continue;
        bool changed=strcmp(term,previous)!=0;
        if (!changed && !next && !prev) continue;
        snprintf(previous,sizeof previous,"%s",term);
        found=false;
        size_t count=ui_panel(ui)->app.files.len;
        for (size_t n=0;n<count;n++) {
            size_t i=changed ? n : next ? (ui_panel(ui)->selected+1+n)%count : (ui_panel(ui)->selected+count-1-n)%count;
            if (text_contains(ui_panel(ui)->app.files.entries[i].name,term)) { ui_panel(ui)->selected=i; found=true; break; }
        }
    }
    ui_panel(ui)->selected=selected; ui_panel(ui)->top=top; ui->focus=ui->show_preview?focus:UI_FOCUS_FILES;
    curs_set(0); draw(ui);
}
