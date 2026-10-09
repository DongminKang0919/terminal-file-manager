#include "ui.h"
void show_filter(UiContext *ui) {
    const char *labels[]={"Name contains (ASCII case-insensitive)","Full-name glob (case-sensitive: * ? [] backslash)","Clear filter (also clears marks)"};
    int choice=ui_choices(ui,"Filter current list - applying clears marks",labels,3);
    if(choice<0) return;
    char pattern[UI_INPUT_CAP]="";
    if(choice<2 && !prompt_value(ui,choice==0?"Name contains":"Full-name glob",pattern,sizeof pattern,ui_panel(ui)->app.filter?ui_panel(ui)->app.filter:"")) return;
    char *selected=ui_panel(ui)->selected<ui_panel(ui)->app.files.len?text_copy(ui_panel(ui)->app.files.entries[ui_panel(ui)->selected].name):NULL;
    if(ui_panel(ui)->selected<ui_panel(ui)->app.files.len&&!selected) { message(ui,"Out of memory; filter unchanged");return; }
    Result r=app_set_filter(&ui_panel(ui)->app,choice==0?FILTER_CONTAINS:choice==1?FILTER_GLOB:FILTER_NONE,pattern);
    if(r.code==RESULT_OK) {
        ui_panel(ui)->selected=ui_panel(ui)->top=0;
        if(selected) for(size_t i=0;i<ui_panel(ui)->app.files.len;i++) if(!strcmp(ui_panel(ui)->app.files.entries[i].name,selected)) ui_panel(ui)->selected=i;
        ui_panel(ui)->stale=false;preview_reset(ui);ui->focus=UI_FOCUS_FILES;
        message(ui,ui_panel(ui)->app.filter_kind==FILTER_NONE?"Filter cleared; marks cleared":"Filter applied; marks cleared; f: change / clear");
    } else message(ui,r.detail);
    free(selected);
}
