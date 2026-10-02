#include "ui.h"

static bool pick_path(UiContext *ui, bool folders_only, const char *initial, char *result) {
    WINDOW *win = dialog_open(ui, folders_only ? "Choose destination directory" : "Choose source file or directory", LINES - 2, 86);
    if (!win) return false;
    if (strlen(initial) >= UI_INPUT_CAP) { dialog_close(ui, win); message(ui, "Path exceeds terminal input limit"); return false; }
    char folder[UI_INPUT_CAP], warning[256] = "";
    snprintf(folder, sizeof folder, "%s", initial);
    FileInfo *entries = NULL;
    size_t picker_count = 0, choice = 0, offset = 0;
    bool reload = true, accepted = false;
    size_t last_choice = SIZE_MAX;
    uint64_t last_click = 0;
    for (;;) {
        if (reload) {
            FileList old = {entries, picker_count}; file_list_free(&old); entries = NULL; picker_count = choice = offset = 0;
            FileList list;
            Result read_result = core_list(folder, true, folders_only, &list);
            entries = list.entries; picker_count = list.len;
            if (read_result.code != RESULT_OK) snprintf(warning, sizeof warning, "Cannot read directory: %.220s", read_result.detail);
            reload = false; last_choice = SIZE_MAX;
        }
        int h, w; getmaxyx(win, h, w); int rows = h - 6;
        if (choice < offset) offset = choice;
        if (choice >= offset + (size_t)rows) offset = choice - rows + 1;
        dialog_frame(win, folders_only ? "Choose destination directory" : "Choose source file or directory");
        wattron(win, COLOR_PAIR(UI_PATH));
        mvwhline(win, 1, 1, ' ', w - 2); draw_window_text(win, 1, 2, w - 4, folder);
        wattroff(win, COLOR_PAIR(UI_PATH));
        draw_window_text(win, 2, 2, w - 4, "[Parent]  [Enter path...]");
        for (int i = 0; i < rows && offset + (size_t)i < picker_count; i++) {
            FileInfo *it = &entries[offset + i]; bool active = offset + i == choice;
            wattron(win, (active ? ui_selection() : COLOR_PAIR(it->directory_target ? UI_DIR : UI_FILE)));
            mvwhline(win, i + 3, 1, ' ', w - 2);
            draw_window_text(win, i + 3, 2, 11, it->directory_target ? "Directory  " : "File       ");
            draw_window_text(win, i + 3, 13, w - 15, it->name);
            wattroff(win, (active ? ui_selection() : COLOR_PAIR(it->directory_target ? UI_DIR : UI_FILE)));
        }
        if (!picker_count) draw_window_text(win, 3, 2, w - 4, folders_only ? "No subdirectories. You can use this directory." : "This directory is empty.");
        draw_window_text(win, h - 3, 2, w - 4, *warning ? warning : "Double-click/Enter: open   Space: use   p: path");
        const char *use_label = folders_only ? "[ Use directory ]" : "[ Use selected ]";
        int open_x = 2 + (int)strlen(use_label) + 2, cancel_x = open_x + 8 + 2;
        dialog_button(win, h - 2, 2, use_label, false, folders_only || picker_count);
        dialog_button(win, h - 2, open_x, "[ Open ]", false, picker_count != 0);
        dialog_button(win, h - 2, cancel_x, "[ Cancel ]", false, true);
        dialog_refresh(win);
        int key = input_key(win);
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
                    choice = offset + row - 3;
                    uint64_t now = core_monotonic_ms();
                    uint64_t ms = now - last_click;
                    open = choice == last_choice && ms < 350;
                    last_choice = open ? SIZE_MAX : choice; last_click = now;
                }
                if (row == 2) { up = col >= 2 && col < 10; path = col >= 12 && col < 27; }
                if (row == h - 2) {
                    use = col >= 2 && col < open_x - 2; open = col >= open_x && col < open_x + 8;
                    if (col >= cancel_x && col < cancel_x + 10) break;
                }
            }
        }
        if (key == 27 || key == KEY_RESIZE) break;
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
            if (parent) { snprintf(folder, sizeof folder, "%s", parent); free(parent); reload = true; warning[0] = 0; }
        } else if (path) {
            char input[UI_INPUT_CAP];
            int old_h = LINES, old_w = COLS;
            if (prompt_value(ui, "Directory path - absolute or relative", input, sizeof input, folder)) {
                char *resolved = NULL;
                Result r = core_resolve_directory(folder, input, &resolved);
                if (r.code == RESULT_OK && strlen(resolved) < sizeof folder) {
                    snprintf(folder, sizeof folder, "%s", resolved); reload = true; warning[0] = 0;
                } else snprintf(warning, sizeof warning, "Cannot open directory: %.220s", r.code == RESULT_OK ? "Path exceeds input limit" : r.detail);
                free(resolved);
            }
            if (old_h != LINES || old_w != COLS) break;
            touchwin(win);
        } else if (use || open) {
            if (use && folders_only) { snprintf(result, UI_INPUT_CAP, "%s", folder); accepted = true; break; }
            if (!picker_count) continue;
            char full[UI_INPUT_CAP];
            if (join(full, sizeof full, folder, entries[choice].name) < 0) { snprintf(warning, sizeof warning, "Path is too long."); continue; }
            if (open && entries[choice].directory_target) {
                char *resolved = NULL;
                Result r = core_resolve_directory(NULL, full, &resolved);
                if (r.code != RESULT_OK || strlen(resolved) >= sizeof folder) {
                    snprintf(warning, sizeof warning, "Cannot open directory: %.220s", r.code == RESULT_OK ? "Path exceeds input limit" : r.detail); free(resolved); continue;
                }
                snprintf(folder, sizeof folder, "%s", resolved); free(resolved); reload = true; warning[0] = 0;
            } else if (!folders_only) { snprintf(result, UI_INPUT_CAP, "%s", full); accepted = true; break; }
        }
    }
    FileList old = {entries, picker_count}; file_list_free(&old); dialog_close(ui, win); return accepted;
}

static void show_transfer_paths(UiContext *ui, const char *source, const char *base, const char *target) {
    WINDOW *win = dialog_open(ui, "Transfer paths", LINES - 2, 88);
    if (!win) return;
    char *name = core_path_name(source);
    const char *text[] = {"Source name:", name ? name : "", "Source path:", source,
                          "Relative destination base:", base, "Final destination (entered path):", target};
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
        if (key == 27 || key == KEY_RESIZE || key == '\n' || key == KEY_ENTER) break;
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
    WINDOW *win = dialog_open(ui, move_it ? "Move" : "Copy", 13, 88);
    if (!win) return;
    if (strlen(ui->app.directory) >= UI_INPUT_CAP ||
        (ui->selected < ui->app.files.len && strlen(ui->app.files.entries[ui->selected].path) >= UI_INPUT_CAP)) {
        dialog_close(ui,win); message(ui,"Path exceeds terminal input limit"); return;
    }
    /* The process cwd is not the browsed location. Keep this base even when a
       failed operation refreshes/replaces the main list while the form stays open. */
    char base[UI_INPUT_CAP], source[UI_INPUT_CAP] = "", warning[512] = "";
    snprintf(base,sizeof base,"%s",ui->app.directory);
    UiField folder_field, name_field; field_init(&folder_field,""); field_init(&name_field,"");
    if (ui->selected < ui->app.files.len) {
        snprintf(source,sizeof source,"%s",ui->app.files.entries[ui->selected].path);
        field_init(&name_field,ui->app.files.entries[ui->selected].name);
    }
    enum { DESTINATION, NAME, BROWSE, PATHS, SOURCE, RUN, CANCEL, FIELDS };
    int focus = DESTINATION, offset = 0;
    const int focus_row[] = {3,4,5,5,5,6,6};
    const int button_x[] = {2,14,25};
    const char *buttons[] = {"[ Browse ]","[ Paths ]","[ Source ]"};
    const char *run_label = move_it ? "[ Move now ]" : "[ Copy now ]";
    for (;;) {
        int h,w; getmaxyx(win,h,w); int rows = h - 4;
        if (focus_row[focus] < offset) offset = focus_row[focus];
        if (focus_row[focus] >= offset + rows) offset = focus_row[focus] - rows + 1;
        /* Include the destination and name together in the minimum-height form. */
        if (focus == DESTINATION && rows == 3) offset = 2;
        char folder[UI_INPUT_CAP], name[UI_INPUT_CAP];
        bool valid = ui_text_encode(folder_field.value,folder_field.len,folder,sizeof folder) &&
                     ui_text_encode(name_field.value,name_field.len,name,sizeof name);
        char *directory = valid ? (*folder == '/' ? text_copy(folder) : *folder ? core_path_join(base,folder) : text_copy(base)) : NULL;
        char *target = directory ? core_path_join(directory,name) : NULL;
        char *source_name = core_path_name(source);
        dialog_frame(win,move_it ? "Move" : "Copy");
        int cursor_y = 0, cursor_x = 0;
        for (int row = offset; row < 7 && row < offset + rows; row++) {
            int y = row - offset + 1;
            if (row == 0) { draw_window_text(win,y,2,8,"Source:"); draw_window_text(win,y,10,w-12,source_name ? source_name : ""); }
            if (row == 1) draw_window_text(win,y,2,w-4,source);
            if (row == 2) { draw_window_text(win,y,2,6,"Base:"); draw_window_text(win,y,8,w-10,base); }
            if (row == 3 || row == 4) {
                bool active = focus == (row == 3 ? DESTINATION : NAME);
                draw_window_text(win,y,2,6,row == 3 ? "To:" : "Name:");
                int col = field_draw(win,y,8,w-10,row == 3 ? &folder_field : &name_field,active);
                if (active) { cursor_y = y; cursor_x = 8 + col; }
            }
            if (row == 5) for (int i = 0; i < 3; i++) dialog_button(win,y,button_x[i],buttons[i],focus==BROWSE+i,true);
            if (row == 6) { draw_window_text(win,y,2,8,"Target:"); draw_window_text(win,y,10,w-12,target ? target : "Input too long / no memory"); }
        }
        wattron(win,COLOR_PAIR(*warning ? UI_SPECIAL : UI_MUTED));
        draw_window_text(win,h-3,2,w-4,*warning ? warning : "To: blank=Base  Tab: focus  Esc: cancel");
        wattroff(win,COLOR_PAIR(*warning ? UI_SPECIAL : UI_MUTED));
        int cancel_x = 2 + (int)strlen(run_label) + 2;
        dialog_button(win,h-2,2,run_label,focus==RUN,true);
        dialog_button(win,h-2,cancel_x,"[ Cancel ]",focus==CANCEL,true);
        curs_set(focus==DESTINATION || focus==NAME);
        if (cursor_y) wmove(win,cursor_y,cursor_x);
        dialog_refresh(win);
        free(source_name); free(target); free(directory);
        wint_t key; int kind=input_wide(win,&key), action=-1;
        if (kind==ERR) continue;
        if ((kind==OK && key==27) || (kind==KEY_CODE_YES && key==KEY_RESIZE)) break;
        if (kind==KEY_CODE_YES && key==KEY_MOUSE) {
            MEVENT e; if (getmouse(&e)!=OK) continue;
            if (dialog_closed(win,&e)) break;
            if (!mouse_click(&e)) continue;
            int wy,wx; getbegyx(win,wy,wx); int y=e.y-wy,x=e.x-wx,row=y-1+offset;
            if (y>=1 && y<=rows && x>=8 && x<w-2 && (row==3 || row==4)) {
                focus=row==3?DESTINATION:NAME; field_click(row==3?&folder_field:&name_field,x-8); continue;
            }
            if (y>=1 && y<=rows && row==5) for(int i=0;i<3;i++)
                if(x>=button_x[i] && x<button_x[i]+(int)strlen(buttons[i])) action=focus=BROWSE+i;
            if (y==h-2) {
                if(x>=2 && x<2+(int)strlen(run_label)) action=focus=RUN;
                if(x>=cancel_x && x<cancel_x+10) action=focus=CANCEL;
            }
        }
        if ((kind==OK && key=='\t') || (kind==KEY_CODE_YES && key==KEY_DOWN)) { focus=(focus+1)%FIELDS; continue; }
        if (kind==KEY_CODE_YES && (key==KEY_BTAB || key==KEY_UP)) { focus=(focus+FIELDS-1)%FIELDS; continue; }
        if ((kind==OK && (key=='\n' || key=='\r')) || (kind==KEY_CODE_YES && key==KEY_ENTER)) {
            if (focus==DESTINATION) { focus=NAME; continue; }
            if (focus==NAME) {
                if (!name_field.len) { snprintf(warning,sizeof warning,"Enter a target name."); continue; }
                focus=RUN; continue;
            }
            action=focus;
        }
        if (action<0) {
            if (focus==DESTINATION || focus==NAME) field_edit(focus==DESTINATION?&folder_field:&name_field,kind,key);
            continue;
        }
        if (action==CANCEL) break;
        if (!valid && action!=SOURCE) { snprintf(warning,sizeof warning,"Input is too long."); continue; }
        int old_h=LINES,old_w=COLS;
        if (action==BROWSE) {
            char *start=NULL; core_resolve_directory(base,*folder?folder:".",&start);
            char picked[UI_INPUT_CAP];
            if (pick_path(ui,true,start?start:base,picked)) field_init(&folder_field,picked);
            free(start);
        } else if (action==SOURCE) {
            char picked[UI_INPUT_CAP];
            if (pick_path(ui,false,base,picked)) {
                char *old_name=core_path_name(source), *new_name=core_path_name(picked);
                if (new_name && (!name_field.len || (valid && old_name && !strcmp(name,old_name)))) field_init(&name_field,new_name);
                snprintf(source,sizeof source,"%s",picked); free(old_name); free(new_name);
            }
        } else if (action==PATHS) {
            char *dir=*folder=='/'?text_copy(folder):*folder?core_path_join(base,folder):text_copy(base);
            char *full=dir?core_path_join(dir,name):NULL;
            if(full) show_transfer_paths(ui,source,base,full);
            else snprintf(warning,sizeof warning,"Out of memory");
            free(dir); free(full);
        } else if (action==RUN) {
            char *resolved=NULL;
            Result r=core_resolve_directory(base,*folder?folder:".",&resolved);
            if(r.code!=RESULT_OK) snprintf(warning,sizeof warning,"Destination: %.450s",r.detail);
            else if(transfer_path(ui,move_it,source,resolved,name,warning,sizeof warning)) { free(resolved); break; }
            free(resolved);
        }
        if(old_h!=LINES || old_w!=COLS) break;
        touchwin(win);
    }
    dialog_close(ui,win);
}
