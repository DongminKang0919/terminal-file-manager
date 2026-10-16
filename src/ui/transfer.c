#include "ui.h"

bool ui_transfer_context(UiContext *ui, UiTransferContext *out) {
    *out=(UiTransferContext){.source_panel=ui->active,.dual=ui->mode==UI_LIST_LIST};
    out->base=text_copy(ui_panel(ui)->app.directory);
    out->destination=text_copy(out->dual ? ui->panels[ui->active^1u].app.directory : ui_panel(ui)->app.directory);
    if(!out->base || !out->destination) {
        ui_transfer_context_free(out); message(ui,"Out of memory; no changes"); return false;
    }
    /* The peer snapshot is a hint. Validation runs when the user requests
       Paths/Run, and the mutation worker revalidates again before changing files. */
    if(out->dual && ui->panels[ui->active^1u].stale)
        out->destination_status=result_make(RESULT_ACCESS,"Opposite list is STALE; destination will be revalidated");
    return true;
}
void ui_transfer_context_free(UiTransferContext *context) {
    free(context->base); free(context->destination); *context=(UiTransferContext){0};
}

bool pick_path(UiContext *ui, bool folders_only, const char *initial, char *result, bool *resized) {
    WINDOW *win = dialog_open(ui, folders_only ? "Choose destination directory" : "Choose source file or directory", LINES - 2, 86);
    if (!win) return false;
    if (strlen(initial) >= UI_INPUT_CAP) { dialog_close(ui, win); message(ui, "Path exceeds terminal input limit"); return false; }
    char folder[UI_INPUT_CAP], warning[256] = "";
    snprintf(folder, sizeof folder, "%s", initial);
    FileInfo *entries = NULL;
    size_t picker_count = 0, choice = 0, offset = 0;
    bool reload = true, accepted = false, readable = false;
    UiPathQuery query={0};
    size_t last_choice = SIZE_MAX;
    uint64_t last_click = 0;
    int focus=0; /* list, Use, Open, Cancel */
    for (;;) {
        if (reload) {
            ui_path_query_start(&query,ui->async_reads,NULL,folder,true,folders_only);
            reload=false;
        }
        ui_path_query_poll(&query);
        if(query.ready) {
            if(query.result.code==RESULT_OK && query.directory && strlen(query.directory)<sizeof folder) {
                FileList old={entries,picker_count};file_list_free(&old);
                entries=query.files.entries;picker_count=query.files.len;query.files=(FileList){0};
                snprintf(folder,sizeof folder,"%s",query.directory);
                choice=offset=0;last_choice=SIZE_MAX;readable=true;warning[0]=0;
            } else snprintf(warning,sizeof warning,"Cannot read directory: %.220s",query.result.code==RESULT_OK?"Path exceeds input limit":query.result.detail);
            query.ready=false;
        }
        int h, w; getmaxyx(win, h, w); int rows = h - 6;
        if (choice < offset) offset = choice;
        if (choice >= offset + (size_t)rows) offset = choice - rows + 1;
        dialog_frame(win, folders_only ? "Choose destination directory" : "Choose source file or directory");
        wattron(win, COLOR_PAIR(UI_PATH));
        mvwhline(win, 1, 1, ' ', w - 2); draw_window_path(win,1,2,w-4,folder);
        wattroff(win, COLOR_PAIR(UI_PATH));
        draw_window_text(win, 2, 2, w - 4, "[Parent]  [Enter path...]");
        for (int i = 0; i < rows && offset + (size_t)i < picker_count; i++) {
            FileInfo *it = &entries[offset + i]; bool active = focus==0 && offset + i == choice;
            wattron(win, (active ? ui_selection() : COLOR_PAIR(it->directory_target ? UI_DIR : UI_FILE)));
            mvwhline(win, i + 3, 1, ' ', w - 2);
            draw_window_text(win, i + 3, 2, 11, it->directory_target ? "Directory  " : "File       ");
            draw_window_text(win, i + 3, 13, w - 15, it->name);
            wattroff(win, (active ? ui_selection() : COLOR_PAIR(it->directory_target ? UI_DIR : UI_FILE)));
        }
        if (query.pending) draw_window_text(win,3,2,w-4,"Loading directory... Esc: back; F10: exit");
        else if (!readable) draw_window_text(win, 3, 2, w - 4, "Cannot inspect directory contents");
        else if (!picker_count) draw_window_text(win, 3, 2, w - 4, folders_only ? "No subdirectories. You can use this directory." : "This directory is empty.");
        draw_window_text(win, h - 3, 2, w - 4, query.pending ? "Loading directory... Esc: back; F10: exit" : *warning ? warning : "Enter: open  Tab: focus  Space: use  p: path");
        const char *use_label = folders_only ? "[ Use directory ]" : "[ Use selected ]";
        int open_x = 2 + (int)strlen(use_label) + 2, cancel_x = open_x + 8 + 2;
        dialog_button(win, h - 2, 2, use_label, focus==1, !query.pending && readable && (folders_only || picker_count));
        dialog_button(win, h - 2, open_x, "[ Open ]", focus==2, !query.pending && picker_count != 0);
        dialog_button(win, h - 2, cancel_x, "[ Cancel ]", focus==3, true);
        dialog_refresh(win);
        wtimeout(win,(query.pending || core_read_cleanup_pending())?40:-1);
        int key = input_key(win);
        if(key==ERR)continue;
        if(key==KEY_F(10)) { core_request_shutdown();break; }
        bool use = false, open = false, up = false, path = false;
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            if (!wenclose(win, e.y, e.x)) continue;
            int y, x; getbegyx(win, y, x); int row = e.y - y, col = e.x - x;
            if (e.bstate & BUTTON4_PRESSED) key = KEY_UP;
            else if (e.bstate & BUTTON5_PRESSED) key = KEY_DOWN;
            else if (mouse_click(&e)) {
                if (row >= 3 && row < 3 + rows && offset + (size_t)(row - 3) < picker_count) {
                    focus=0; choice = offset + row - 3;
                    uint64_t now = core_monotonic_ms();
                    uint64_t ms = now - last_click;
                    open = choice == last_choice && ms < 350;
                    last_choice = open ? SIZE_MAX : choice; last_click = now;
                }
                if (row == 2) { up = col >= 2 && col < 10; path = col >= 12 && col < 27; }
                if (row == h - 2) {
                    use=dialog_button_hit(win,&e,h-2,2,use_label,!query.pending && readable && (folders_only || picker_count));
                    open=dialog_button_hit(win,&e,h-2,open_x,"[ Open ]",!query.pending && picker_count!=0);
                    if(dialog_button_hit(win,&e,h-2,cancel_x,"[ Cancel ]",true)) break;
                }
            }
        }
        if (key == 27 || key == KEY_RESIZE) { if(key==KEY_RESIZE && resized) *resized=true; break; }
        if(key=='\t' || key==KEY_BTAB) {
            unsigned disabled=(query.pending || !readable || (!folders_only && !picker_count) ? 1u<<1 : 0) | (!picker_count ? 1u<<2 : 0);
            focus=dialog_focus_next(focus,4,key==KEY_BTAB,disabled); continue;
        }
        if((key=='\n' || key==KEY_ENTER) && focus) {
            if(focus==3) break;
            use=focus==1; open=focus==2; key=0;
        }
        if(key==KEY_UP || key==KEY_DOWN || key==KEY_PPAGE || key==KEY_NPAGE || key==KEY_HOME || key==KEY_END) focus=0;
        if (key == KEY_UP && choice) choice--;
        if (key == KEY_DOWN && choice + 1 < picker_count) choice++;
        if (key == KEY_HOME) choice = 0;
        if (key == KEY_END && picker_count) choice = picker_count - 1;
        if (key == KEY_PPAGE) choice = choice > (size_t)rows ? choice - rows : 0;
        if (key == KEY_NPAGE && picker_count) { choice += rows; if (choice >= picker_count) choice = picker_count - 1; }
        up |= key == KEY_LEFT || key == KEY_BACKSPACE || key == 127;
        path |= key == 'p'; open |= key == KEY_RIGHT || key == '\n' || key == KEY_ENTER;
        use |= key == ' ';
        if (up) {
            char *parent = core_path_parent(folder);
            if (parent) { ui_path_query_start(&query,ui->async_reads,NULL,parent,true,folders_only);free(parent);warning[0]=0; }
        } else if (path) {
            char input[UI_INPUT_CAP];
            int old_h = LINES, old_w = COLS;
            bool path_resized=false;
            if (prompt_value_status(ui, "Directory path - absolute or relative", input, sizeof input, folder, &path_resized)) {
                ui_path_query_start(&query,ui->async_reads,folder,input,true,folders_only);
            }
            if (path_resized || old_h != LINES || old_w != COLS) { if(resized) *resized=true; break; }
            touchwin(win);
        } else if (use || open) {
            if (query.pending || !readable) continue;
            if (use && folders_only) { snprintf(result, UI_INPUT_CAP, "%s", folder); accepted = true; break; }
            if (!picker_count) continue;
            char full[UI_INPUT_CAP];
            if (join(full, sizeof full, folder, entries[choice].name) < 0) { snprintf(warning, sizeof warning, "Path is too long."); continue; }
            if (open && entries[choice].directory_target) {
                ui_path_query_start(&query,ui->async_reads,NULL,full,true,folders_only);
            } else if (!folders_only) { snprintf(result, UI_INPUT_CAP, "%s", full); accepted = true; break; }
        }
    }
    ui_path_query_close(&query);
    FileList old = {entries, picker_count}; file_list_free(&old); dialog_close(ui, win); return accepted;
}

static void show_transfer_paths(UiContext *ui, const char *source, const char *base, const char *target, bool *resized) {
    WINDOW *win = dialog_open(ui, "Transfer paths", LINES - 2, 88);
    if (!win) return;
    char *name = core_path_name(source);
    const char *text[] = {"Source name:", name ? name : "", "Source path:", source,
                          "Relative destination base:", base, "Final destination (resolved directory):", target};
    size_t top = 0;
    for (;;) {
        int h,w; getmaxyx(win,h,w); size_t rows = (size_t)(h - 4), line = 0;
        dialog_frame(win,"Transfer paths");
        for (size_t i = 0; i < sizeof text / sizeof text[0]; i++) {
            size_t at = 0;
            do {
                if (line >= top && line - top < rows)
                    draw_window_page(win,1+(int)(line-top),2,w-4,text[i],at);
                at = ui_text_span(text[i],at,w-4).end; line++;
            } while (text[i][at]);
        }
        if (top >= line) { top = line > rows ? line - rows : 0; continue; }
        char hint[80]; size_t end = top + rows < line ? top + rows : line;
        snprintf(hint,sizeof hint,"Lines %zu-%zu/%zu",top+1,end,line);
        draw_window_text(win,h-3,2,w-4,hint);
        draw_window_text(win,h-2,2,w-4,"Up/Down PgUp/PgDn  Enter/Esc: back"); dialog_refresh(win);
        int key = mouse_key(win,input_key(win));
        if (key == KEY_RESIZE) { *resized=true; break; }
        if (key == 27 || key == '\n' || key == KEY_ENTER) break;
        if (key == KEY_UP && top) top--;
        if (key == KEY_DOWN && top + rows < line) top++;
        if (key == KEY_PPAGE) top = top > rows ? top - rows : 0;
        if (key == KEY_NPAGE && top + rows < line) top += rows;
        if (key == KEY_HOME) top = 0;
        if (key == KEY_END) top = line > rows ? line - rows : 0;
    }
    free(name); dialog_close(ui,win);
}

void transfer_entry(UiContext *ui, bool move_it) {
    if(!ui_operation_allowed(ui,NULL,0)) return;
    if (ui_panel(ui)->app.marks_len > 1) { batch_entry(ui, move_it ? BATCH_MOVE : BATCH_COPY); return; }
    const FileInfo *initial=ui_operation_item(ui);
    if(ui_panel(ui)->app.marks_len&&!initial) return;
    bool fixed_source=ui_panel(ui)->app.marks_len!=0 || ui->mode==UI_LIST_LIST;

    if(!initial && ui->mode==UI_LIST_LIST) {
        message(ui,"No transfer targets in active panel"); ui->status_priority=true; return;
    }
    UiTransferContext context;
    if(!ui_transfer_context(ui,&context)) return;
    char title[64];snprintf(title,sizeof title,"%s 1 item",move_it?"Move":"Copy");
    WINDOW *win = dialog_open(ui,title,13,88);
    if (!win) { ui_transfer_context_free(&context); return; }
    if (strlen(ui_panel(ui)->app.directory) >= UI_INPUT_CAP ||
        strlen(context.destination) >= UI_INPUT_CAP ||
        (initial && strlen(initial->path) >= UI_INPUT_CAP)) {
        dialog_close(ui,win); ui_transfer_context_free(&context); message(ui,"Path exceeds terminal input limit"); return;
    }
    /* The process cwd is not the browsed location. Keep this base even when a
       failed operation refreshes/replaces the main list while the form stays open. */
    char base[UI_INPUT_CAP], source[UI_INPUT_CAP] = "", warning[512] = "";
    snprintf(base,sizeof base,"%s",context.base);
    UiField folder_field, name_field; field_init(&folder_field,context.dual ? context.destination : ""); field_init(&name_field,"");
    if(context.destination_status.code!=RESULT_OK) snprintf(warning,sizeof warning,"Destination: %.450s",context.destination_status.detail);
    if (initial) {
        snprintf(source,sizeof source,"%s",initial->path);
        field_init(&name_field,initial->name);
    }
    enum { DESTINATION, NAME, BROWSE, PATHS, SOURCE, RUN, CANCEL, FIELDS };
    int focus = DESTINATION, offset = 0, pending_action=-1;
    UiPathQuery query={0};
    bool checked=false;
    const int button_x[] = {2,14,25};
    const char *buttons[] = {"[ Browse ]","[ Paths ]","[ Source ]"};
    const char *run_label = move_it ? "[ Move 1 item ]" : "[ Copy 1 item ]";
    for (;;) {
        ui_path_query_poll(&query);
        int h,w; getmaxyx(win,h,w); int rows = h - 4;
        bool compact=rows==3;int buttons_row=compact?3:4;
        int focused_row=focus>=BROWSE&&focus<=SOURCE?buttons_row:focus==NAME?2:1;
        if (focused_row < offset) offset = focused_row;
        if (focused_row >= offset + rows) offset = focused_row - rows + 1;
        /* Include the destination and name together in the minimum-height form. */
        if (rows == 3 && (focus==DESTINATION || focus==NAME || focus==RUN || focus==CANCEL))offset=0;
        char folder[UI_INPUT_CAP], name[UI_INPUT_CAP];
        bool valid = ui_text_encode(folder_field.value,folder_field.len,folder,sizeof folder) &&
                     ui_text_encode(name_field.value,name_field.len,name,sizeof name);
        char *directory = valid ? (*folder == '/' ? text_copy(folder) : *folder ? core_path_join(base,folder) : text_copy(base)) : NULL;
        char *target = directory ? core_path_join(directory,name) : NULL;
        dialog_frame(win,title);
        const char *state=query.pending?"Checking destination... Esc: cancel":*warning?warning:checked?"To checked; execution rechecks":"Unchecked; action checks To first";
        int cursor_y = 0, cursor_x = 0;
        for (int row = offset; row < (compact?6:7) && row < offset + rows; row++) {
            int y = row - offset + 1;
            if (row == 0) { draw_window_text(win,y,2,8,"From:"); draw_window_path(win,y,10,w-12,*source?source:"Choose Source first"); }
            if (row == (compact?4:5)) { draw_window_text(win,y,2,8,"Base:"); draw_window_path(win,y,10,w-12,base); }
            if (row == 1 || row == 2) {
                bool active = focus == (row == 1 ? DESTINATION : NAME);
                draw_window_text(win,y,2,8,row == 1 ? (active?">To:":" To:") : (active?">Name:":" Name:"));
                int col = field_draw(win,y,10,w-12,row == 1 ? &folder_field : &name_field,active);
                if (active) { cursor_y = y; cursor_x = 10 + col; }
            }
            if(!compact && row==3) { wattron(win,COLOR_PAIR(*warning?UI_ERROR:UI_MUTED));draw_window_text(win,y,2,w-4,state);wattroff(win,COLOR_PAIR(*warning?UI_ERROR:UI_MUTED)); }
            if (row == buttons_row) for (int i = 0; i < 3; i++) dialog_button(win,y,button_x[i],buttons[i],focus==BROWSE+i,!query.pending && (i!=2 || !fixed_source));
            if (row == (compact?5:6)) { draw_window_text(win,y,2,8,"Target:"); if(target) draw_window_path(win,y,10,w-12,target); else draw_window_text(win,y,10,w-12,"Input too long / no memory"); }
        }
        wattron(win,COLOR_PAIR(*warning ? UI_ERROR : UI_MUTED));
        draw_window_text(win,h-3,2,w-4,query.pending ? "Checking destination... Esc: cancel; F10: exit" : *warning ? warning : checked ? "To checked; execution rechecks. Tab: next" : focus==DESTINATION ? "Enter: Name | Tab: next | To uses Base" : focus==NAME ? (move_it?"Enter: Move button | Tab: next":"Enter: Copy button | Tab: next") : "To relative/blank: Base (Paths); Esc: cancel");
        wattroff(win,COLOR_PAIR(*warning ? UI_ERROR : UI_MUTED));
        int cancel_x = 2 + (int)strlen(run_label) + 2;
        dialog_button(win,h-2,2,run_label,focus==RUN,!query.pending);
        dialog_button(win,h-2,cancel_x,"[ Cancel ]",focus==CANCEL,true);
        if(*warning)draw_window_text(win,h-2,cancel_x+12,w-cancel_x-14,"Edit / retry");
        curs_set(focus==DESTINATION || focus==NAME);
        if (cursor_y) wmove(win,cursor_y,cursor_x);
        dialog_refresh(win);
        free(target); free(directory);
        wint_t key=0;int kind=ERR,action=-1;
        if(query.ready && pending_action>=0) {
            /* Consume already-queued edits/cancel before committing a result.
               A completed request never outranks newer terminal input. */
            if(ui->async_reads) { wtimeout(win,0);kind=input_wide(win,&key); }
            if(kind==ERR) { action=pending_action;pending_action=-1; }
        }
        else { wtimeout(win,(query.pending || core_read_cleanup_pending())?40:-1);kind=input_wide(win,&key); }
        if(kind==KEY_CODE_YES && key==KEY_F(10)) { core_request_shutdown();break; }
        if(kind==OK && key==27 && (query.pending || pending_action>=0)) {
            ui_path_query_close(&query);pending_action=-1;
            checked=false;snprintf(warning,sizeof warning,"Destination check cancelled; inputs kept");continue;
        }
        if (kind==ERR && action<0) continue;
        if ((kind==OK && key==27) || (kind==KEY_CODE_YES && key==KEY_RESIZE)) break;
        if (kind==KEY_CODE_YES && key==KEY_MOUSE) {
            MEVENT e; if (getmouse(&e)!=OK) continue;
            if (dialog_closed(win,&e)) break;
            if (!mouse_click(&e)) continue;
            int wy,wx; getbegyx(win,wy,wx); int y=e.y-wy,x=e.x-wx,row=y-1+offset;
            if (y>=1 && y<=rows && x>=10 && x<w-2 && (row==1 || row==2)) {
                focus=row==1?DESTINATION:NAME; field_click(row==1?&folder_field:&name_field,x-10); continue;
            }
            if (y>=1 && y<=rows && row==buttons_row) for(int i=0;i<3;i++)
                if(dialog_button_hit(win,&e,y,button_x[i],buttons[i],!query.pending && (i!=2 || !fixed_source))) action=focus=BROWSE+i;
            if (y==h-2) {
                if(dialog_button_hit(win,&e,h-2,2,run_label,!query.pending)) action=focus=RUN;
                if(dialog_button_hit(win,&e,h-2,cancel_x,"[ Cancel ]",true)) action=focus=CANCEL;
            }
        }
        if ((kind==OK && key=='\t') || (kind==KEY_CODE_YES && key==KEY_DOWN)) { focus=dialog_focus_next(focus,FIELDS,false,(fixed_source?1u<<SOURCE:0)|(query.pending?((1u<<BROWSE)|(1u<<PATHS)|(1u<<SOURCE)|(1u<<RUN)):0)); continue; }
        if (kind==KEY_CODE_YES && (key==KEY_BTAB || key==KEY_UP)) { focus=dialog_focus_next(focus,FIELDS,true,(fixed_source?1u<<SOURCE:0)|(query.pending?((1u<<BROWSE)|(1u<<PATHS)|(1u<<SOURCE)|(1u<<RUN)):0)); continue; }
        if ((kind==OK && (key=='\n' || key=='\r')) || (kind==KEY_CODE_YES && key==KEY_ENTER)) {
            if (focus==DESTINATION) { focus=NAME; continue; }
            if (focus==NAME) {
                if (!name_field.len) { snprintf(warning,sizeof warning,"Enter a target name."); continue; }
                if(!query.pending)focus=RUN;
                continue;
            }
            action=focus;
        }
        if (action<0) {
            if (focus==DESTINATION || focus==NAME) {
                UiField *field=focus==DESTINATION?&folder_field:&name_field;
                UiField before=*field;field_edit(field,kind,key);
                if(before.len!=field->len || wmemcmp(before.value,field->value,field->len)) {
                    ui_path_query_close(&query);pending_action=-1;warning[0]=0;checked=false;
                }
            }
            continue;
        }
        if (action==CANCEL) break;
        if(query.pending)continue;
        if(query.ready)checked=query.result.code==RESULT_OK;
        if((action==RUN || action==PATHS) && !query.ready) {
            ui_path_query_start(&query,ui->async_reads,base,*folder?folder:".",false,false);
            pending_action=action;continue;
        }
        pending_action=-1;
        if(action==BROWSE || action==SOURCE) {
            /* A newer picker intent supersedes the completed Run as well as
               its result. Picker cancellation must not resurrect that Run. */
            ui_path_query_close(&query);checked=false;
        }
        if (!valid && action!=SOURCE) { snprintf(warning,sizeof warning,"Input is too long."); continue; }
        int old_h=LINES,old_w=COLS; bool resized=false;
        if (action==BROWSE) {
            char *start=*folder=='/'?text_copy(folder):core_path_join(base,*folder?folder:".");
            char picked[UI_INPUT_CAP];
            if (start && pick_path(ui,true,start,picked,&resized)) { field_init(&folder_field,picked); warning[0]=0;checked=false; context.destination_status=result_make(RESULT_OK,NULL); }
            if(!start) snprintf(warning,sizeof warning,"Out of memory; destination unchanged");
            free(start);
        } else if (action==SOURCE && !fixed_source) {
            char picked[UI_INPUT_CAP];
            if (pick_path(ui,false,base,picked,&resized)) {
                char *old_name=core_path_name(source), *new_name=core_path_name(picked);
                if (new_name && (!name_field.len || (valid && old_name && !strcmp(name,old_name)))) field_init(&name_field,new_name);
                snprintf(source,sizeof source,"%s",picked); checked=false;free(old_name); free(new_name);
            }
        } else if (action==PATHS) {
            char *dir=NULL;
            Result path_result=query.result;dir=query.directory;query.directory=NULL;query.ready=false;
            char *full=dir?core_path_join(dir,name):NULL;
            if(full) show_transfer_paths(ui,source,base,full,&resized);
            else snprintf(warning,sizeof warning,"Destination: %.450s",path_result.code==RESULT_OK?"Out of memory":path_result.detail);
            free(dir); free(full);
        } else if (action==RUN) {
            char *resolved=NULL;
            Result r=query.result;resolved=query.directory;query.directory=NULL;query.ready=false;
            if(r.code!=RESULT_OK) { snprintf(warning,sizeof warning,"Destination: %.450s",r.detail); focus=DESTINATION; }
            else if(transfer_path(ui,move_it,source,resolved,name,warning,sizeof warning)) { free(resolved); break; }
            free(resolved);
        }
        if(resized || old_h!=LINES || old_w!=COLS) break;
        touchwin(win);
    }
    ui_path_query_close(&query);
    dialog_close(ui,win); ui_transfer_context_free(&context);
}
