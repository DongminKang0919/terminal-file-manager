#include "ui.h"

/* Raw presses make selection immediate; track double presses without curses' delay. */
bool mouse_click(const MEVENT *e) {
    return (e->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED)) != 0;
}
void dialog_frame(WINDOW *win, const char *title) {
    int h, w; getmaxyx(win, h, w); (void)h;
    wbkgdset(win, COLOR_PAIR(UI_BASE));
    wattrset(win, A_NORMAL);
    werase(win);
    wattron(win, COLOR_PAIR(UI_BORDER)); box(win, 0, 0); wattroff(win, COLOR_PAIR(UI_BORDER));
    wattron(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
    mvwhline(win, 0, 1, ' ', w - 2);
    draw_window_text(win, 0, 2, w - 9, title);
    mvwaddstr(win, 0, w - 6, "[ x ]");
    wattroff(win, COLOR_PAIR(UI_HEADER) | A_BOLD);
    /* Footer caption sits in a rule; two rows remain reserved at every size. */
    mvwhline(win, h - 3, 1, ACS_HLINE, w - 2);
}

void dialog_button(WINDOW *win, int y, int x, const char *label, bool focused, bool enabled) {
    attr_t style = !enabled ? A_DIM : focused ? ui_selection() : A_NORMAL;
    wattron(win, style); draw_window_text(win, y, x, getmaxx(win) - x - 2, label);
    wattroff(win, style);
}

/* Use the same safely clipped cell span as drawing; disabled buttons never hit. */
bool dialog_button_hit(WINDOW *win,const MEVENT *event,int y,int x,const char *label,bool enabled) {
    int wy,wx; getbegyx(win,wy,wx);
    int width=getmaxx(win)-x-2;
    UiTextSpan span=ui_text_span(label,0,width);
    int cells=span.more && width>=3 ? ui_text_span(label,0,width-3).cells+3 : span.cells;
    return enabled && mouse_click(event) && event->y==wy+y && event->x>=wx+x && event->x<wx+x+cells;
}
int dialog_focus_next(int focus,int count,bool reverse,unsigned disabled) {
    for(int i=0;i<count;i++) {
        focus=(focus+count+(reverse ? -1 : 1))%count;
        if(!(disabled&(1u<<focus))) break;
    }
    return focus;
}

/* Resolve existing semantic colors onto the popup surfaces without changing text,
   coordinates, cursor, or input policy. Keep ACS and wide-character cells intact. */
void dialog_refresh(WINDOW *win) {
    int h, w, cy, cx; getmaxyx(win, h, w); getyx(win, cy, cx);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        cchar_t cell; wchar_t text[CCHARW_MAX]; attr_t attr; short old;
        mvwin_wch(win, y, x, &cell); getcchar(&cell, text, &attr, &old, NULL);
        attr &= ~A_COLOR;
        int pair;
        bool selected = old == UI_SELECTED || old == UI_DIR_SELECTED || old == UI_FILE_SELECTED ||
                        old == UI_LINK_SELECTED || old == UI_EXEC_SELECTED ||
                        old == UI_HIDDEN_SELECTED || old == UI_SPECIAL_SELECTED;
        bool footer = y >= h - 3;
        if (x == 0 || x == w - 1 || y == h - 1) {
            pair = UI_POP_BORDER; attr |= A_BOLD;
        } else if (y == 0) {
            pair = UI_POP_TITLE; attr |= A_BOLD;
            if (!has_colors()) attr |= A_REVERSE;
        } else if (selected) { pair = UI_SELECTED; attr |= A_BOLD; }
        else if (attr & A_DIM) pair = UI_POP_DISABLED;
        else if (old == UI_ERROR) pair = footer ? UI_POP_FOOT_ERROR : UI_POP_ERROR;
        else if (old == UI_SPECIAL || old == UI_WARNING || old == UI_PREVIEW_WARNING)
            pair = footer ? UI_POP_FOOT_WARNING : UI_POP_WARNING;
        else if (footer) pair = UI_POP_FOOTER;
        else pair = old == UI_MUTED ? UI_POP_MUTED : UI_POP_BODY;
        mvwchgat(win, y, x, 1, attr, has_colors() ? pair : 0, NULL);
    }
    wmove(win, cy, cx); wrefresh(win);
}
WINDOW *dialog_open(UiContext *ui, const char *title, int height, int width) {
    int h, w; getmaxyx(stdscr, h, w);
    if (h < 9 || w < 50) { message(ui, "Resize terminal to at least 50x9"); return NULL; }
    if (height > h - 2) height = h - 2;
    if (width > w - 4) width = w - 4;
    WINDOW *win = newwin(height, width, (h - height) / 2, (w - width) / 2);
    if (win) { graphics_probe_cancel(ui); if(ui->media_job) media_reset(ui); ui->modal_depth++; draw_cached(ui); keypad(win, TRUE); wbkgd(win, COLOR_PAIR(UI_BASE)); dialog_frame(win, title); }
    return win;
}
bool dialog_closed(WINDOW *win, const MEVENT *e) {
    return dialog_button_hit(win,e,0,getmaxx(win)-6,"[ x ]",true);
}

int mouse_key(WINDOW *win, int key) {
    if (key != KEY_MOUSE) return key;
    MEVENT e;
    if (getmouse(&e) != OK) return 0;
    if (dialog_closed(win, &e)) return 27;
    if (!wenclose(win, e.y, e.x)) return 0;
    if (e.bstate & BUTTON4_PRESSED) return KEY_UP;
    if (e.bstate & BUTTON5_PRESSED) return KEY_DOWN;
    return 0;
}
void dialog_close(UiContext *ui, WINDOW *win) {
    curs_set(0); delwin(win);
    if (ui->modal_depth) ui->modal_depth--;
    touchwin(stdscr); draw_cached(ui);
}

/* Wide-character editing keeps cursor movement and deletion on UTF-8 boundaries. */
static bool input_dialog(UiContext *ui, const char *label, char *out, size_t size, bool *directory, const char *initial, WINDOW *host, const char *rename_source, bool *resized) {
    WINDOW *win = host ? host : dialog_open(ui, label, 9, 76);
    if (!win) return false;
    UiField field;
    if (!field_init(&field, initial ? initial : "")) {
        message(ui, "Input exceeds terminal limit");
        if (!host) dialog_close(ui, win);
        return false;
    }
    int focus = 0;
    bool accepted = false;
    char warning[256] = "";
    for (;;) {
        int h, w; getmaxyx(win, h, w);
        dialog_frame(win, label);
        int cancel_x = (host || directory) ? 14 : 10;
        if (directory) {
            mvwaddstr(win, 1, 2, "Type:");
            if (focus == 3) wattron(win, ui_selection());
            mvwprintw(win, 1, 8, "[%c] File   [%c] Directory", *directory ? ' ' : 'x', *directory ? 'x' : ' ');
            wattroff(win, ui_selection());
            mvwaddstr(win, 2, 2, "Name:");
            if (h > 7) draw_window_text(win, h - 3, 2, w - 4, "Tab: next field   Type: Left/Right   Esc: cancel");
        } else {
            draw_window_path(win,1,2,w-4,ui_panel(ui)->app.directory);
            if (host) {
                mvwaddstr(win, 2, 2, "Name contains:");
                if (!*warning) draw_window_text(win, 4, 2, w - 4, focus == 2 ?
                    "Enter: close  Tab: focus  Esc: close" : "Enter: search  Tab: focus  Esc: close");
            }
        }
        if (*warning) {
            wattron(win, ui_notice_style(NOTICE_ERROR));
            draw_window_text(win, 4, 2, w - 4, warning);
            wattroff(win, ui_notice_style(NOTICE_ERROR));
        }
        int cols = field_draw(win, 3, 2, w - 4, &field, focus == 0);
        dialog_button(win, h - 2, 2, directory ? "[ Create ]" : host ? "[ Search ]" : "[ OK ]", focus == 1, true);
        dialog_button(win, h - 2, cancel_x, "[ Cancel ]", focus == 2, true);
        curs_set(focus == 0); wmove(win, 3, 2 + cols); dialog_refresh(win);
        wint_t key; int kind = input_wide(win, &key);
        if (kind == ERR) continue;
        if ((kind == KEY_CODE_YES && key == KEY_RESIZE) || (kind == OK && key == 27)) {
            if(kind==KEY_CODE_YES && key==KEY_RESIZE && resized) *resized=true;
            break;
        }
        if (directory && focus == 3 && ((kind == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) || (kind == OK && key == ' '))) {
            *directory = !*directory; continue;
        }
        if (kind == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (!mouse_click(&e)) continue;
            if (directory && e.y == y + 1) {
                if (e.x >= x + 8 && e.x < x + 16) { *directory = false; focus = 0; }
                if (e.x >= x + 19 && e.x < x + 32) { *directory = true; focus = 0; }
                continue;
            }
            if (dialog_button_hit(win,&e,h-2,cancel_x,"[ Cancel ]",true)) break;
            if (dialog_button_hit(win,&e,h-2,2,directory?"[ Create ]":host?"[ Search ]":"[ OK ]",true)) { key = '\n'; kind = OK; focus = 1; }
            else if (e.y == y + 3 && e.x >= x + 2 && e.x < x + w - 2) {
                focus = 0; field_click(&field, e.x - x - 2);
                continue;
            } else continue;
        }
        if (kind == OK && key == '\t') { focus = (focus + 1) % (directory ? 4 : 3); continue; }
        if (kind == KEY_CODE_YES && key == KEY_BTAB) { focus = (focus + (directory ? 3 : 2)) % (directory ? 4 : 3); continue; }
        if ((kind == OK && (key == '\n' || key == '\r')) || (kind == KEY_CODE_YES && key == KEY_ENTER)) {
            if (focus == 2) break;
            if (focus == 3) { focus = 0; continue; }
            if (!field.len) { snprintf(warning, sizeof warning, "Enter a value to continue."); focus = 0; continue; }
            if (ui_text_encode(field.value, field.len, out, size)) {
                if (rename_source ? rename_named_entry(ui, rename_source, out, warning, sizeof warning) :
                    !directory || create_named_entry(ui, *directory, out, warning, sizeof warning)) { accepted = true; break; }
                focus = 0;
            } else { snprintf(warning, sizeof warning, "Input is too long."); focus = 0; }
            continue;
        }
        if (focus != 0) continue;
        field_edit(&field, kind, key);
    }
    if (host) curs_set(0); else dialog_close(ui, win);
    return accepted;
}
bool prompt(UiContext *ui, const char *label, char *out, size_t size) {
    return input_dialog(ui, label, out, size, NULL, NULL, NULL, NULL, NULL);
}
bool new_entry_dialog(UiContext *ui, bool directory, char *name, size_t size) {
    return input_dialog(ui, "New - File or Directory", name, size, &directory, NULL, NULL, NULL, NULL);
}
bool prompt_value(UiContext *ui, const char *label, char *out, size_t size, const char *initial) {
    return input_dialog(ui, label, out, size, NULL, initial, NULL, NULL, NULL);
}
bool prompt_value_status(UiContext *ui, const char *label, char *out, size_t size, const char *initial, bool *resized) {
    return input_dialog(ui, label, out, size, NULL, initial, NULL, NULL, resized);
}
bool search_prompt(UiContext *ui, WINDOW *win, char *out, size_t size) {
    return input_dialog(ui, "Search", out, size, NULL, out, win, NULL, NULL);
}
void rename_entry(UiContext *ui) {
    if(!ui_operation_allowed(ui,NULL,0)) return;
    if(ui_panel(ui)->app.marks_len>1) { message(ui,"Rename unavailable: multiple marked items");return; }
    if (ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) return;
    char *source = text_copy(ui_panel(ui)->app.files.entries[ui_panel(ui)->selected].path);
    char *name = text_copy(ui_panel(ui)->app.files.entries[ui_panel(ui)->selected].name);
    if (!source || !name) message(ui, "Rename: Out of memory");
    else {
        char out[UI_INPUT_CAP];
        input_dialog(ui, "Rename selected item", out, sizeof out, NULL, name, NULL, source, NULL);
    }
    free(source); free(name);
}
static bool confirm_action(UiContext *ui, const char *name, bool directory, bool trash) {
    const char *title=trash ? "Confirm Trash" : "Confirm deletion";
    const char *button=trash ? "[ Trash ]" : "[ Delete ]";
    WINDOW *win = dialog_open(ui, title, 8, 78);
    if (!win) return false;
    bool yes = false, result = false;
    size_t start = 0;
    for (;;) {
        int h, w; getmaxyx(win, h, w);
        dialog_frame(win, title);
        draw_window_text(win, 1, 2, w - 4, trash ? "Move this item and its contents to Trash?" : directory ? "Delete directory and all its contents?" : "Permanently delete this file?");
        size_t pages = 0, page = 0;
        for (size_t at = 0;;) {
            if (at == start) page = pages;
            pages++;
            size_t end = ui_text_span(name, at, w - 4).end;
            if (!name[end]) break;
            at = end;
        }
        wattron(win, A_BOLD);
        size_t next = draw_window_page(win, 2, 2, w - 4, name, start);
        wattroff(win, A_BOLD);
        if (pages > 1) {
            mvwaddstr(win, h - 3, 2, "[<]"); mvwaddstr(win, h - 3, w - 5, "[>]");
            char hint[80]; snprintf(hint, sizeof hint, "Name %zu/%zu  PgUp/PgDn", page + 1, pages);
            draw_window_text(win, h - 3, 7, w - 14, hint);
        }
        wattron(win,ui_notice_style(NOTICE_WARNING));
        draw_window_text(win,3,2,w-4,trash ? "No permanent-delete fallback; restore using desktop tools." : "[!] Permanent deletion; cannot be undone.");
        wattroff(win,ui_notice_style(NOTICE_WARNING));
        dialog_button(win, h - 2, 2, button, yes, true);
        dialog_button(win, h - 2, 14, "[ Cancel ]", !yes, true); dialog_refresh(win);
        int key = input_key(win);
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (mouse_click(&e) && e.y == y + h - 3) {
                if (e.x >= x + 2 && e.x < x + 5) key = KEY_PPAGE;
                else if (e.x >= x + w - 5 && e.x < x + w - 2) key = KEY_NPAGE;
            }
            if (mouse_click(&e) && e.y == y + h - 2) {
                if (dialog_button_hit(win,&e,h-2,2,button,true)) { result = true; break; }
                if (dialog_button_hit(win,&e,h-2,14,"[ Cancel ]",true)) break;
            }
        }
        if (key == KEY_NPAGE && name[next]) { start = next; yes = false; }
        if (key == KEY_PPAGE && start) {
            size_t previous = 0;
            while (ui_text_span(name, previous, w - 4).end < start)
                previous = ui_text_span(name, previous, w - 4).end;
            start = previous; yes = false;
        }
        if (key == 27 || key == KEY_RESIZE || key == 'n') break;
        if (key == '\t' || key == KEY_BTAB || key == KEY_LEFT || key == KEY_RIGHT) yes = !yes;
        if (key == '\n' || key == KEY_ENTER) { result = yes; break; }
    }
    dialog_close(ui, win); return result;
}

bool confirm(UiContext *ui,const char *name,bool directory) { return confirm_action(ui,name,directory,false); }
bool confirm_trash(UiContext *ui,const char *name,bool directory) { return confirm_action(ui,name,directory,true); }

static int choice_dialog(WINDOW *win, const char *title, const char **labels, int total, int *selection, int *scroll, const char *warning, NoticeKind warning_kind, unsigned disabled) {
    int selected_row = *selection, offset = *scroll, result = -1;
    if(disabled&(1u<<selected_row)) selected_row=dialog_focus_next(selected_row,total,false,disabled);
    for (;;) {
        int h, w; getmaxyx(win, h, w); int rows = h - 4;
        if (selected_row < offset) offset = selected_row;
        if (selected_row >= offset + rows) offset = selected_row - rows + 1;
        dialog_frame(win, title);
        for (int i = 0; i < rows && offset + i < total; i++) {
            attr_t style=(disabled&(1u<<(offset+i))) ? A_DIM : offset+i==selected_row ? ui_selection() : A_NORMAL;
            wattron(win,style);
            draw_window_text(win,i+1,2,w-4,labels[offset+i]); wattroff(win,style);
        }
        if (warning && *warning) {
            wattron(win,ui_notice_style(warning_kind));
            draw_window_text(win,h-3,2,w-4,warning);
            wattroff(win,ui_notice_style(warning_kind));
        }
        draw_window_text(win, h - 2, 2, w - 4, "Tab/Arrows  Enter: choose  Esc: close"); dialog_refresh(win);
        int key = input_key(win);
        if (key == KEY_MOUSE) {
            MEVENT e; if (getmouse(&e) != OK) continue;
            if (dialog_closed(win, &e)) break;
            int y, x; getbegyx(win, y, x);
            if (!wenclose(win, e.y, e.x)) continue;
            if (e.bstate & BUTTON4_PRESSED) key = KEY_UP;
            else if (e.bstate & BUTTON5_PRESSED) key = KEY_DOWN;
            else if (mouse_click(&e) && e.x > x && e.x < x + w - 1 && e.y > y && e.y < y + 1 + rows && offset + e.y - y - 1 < total) { int hit=offset+e.y-y-1; if(!(disabled&(1u<<hit))) { result=hit; break; } }
        }
        if (key == 27 || key == KEY_RESIZE) break;
        if (key=='\t' || key==KEY_BTAB) selected_row=dialog_focus_next(selected_row,total,key==KEY_BTAB,disabled);
        if (key == KEY_UP && selected_row) {
            do { selected_row--; } while(selected_row && (disabled&(1u<<selected_row)));
        }
        if (key == KEY_DOWN && selected_row + 1 < total) {
            do { selected_row++; } while(selected_row+1<total && (disabled&(1u<<selected_row)));
        }
        if (key == '\n' || key == KEY_ENTER) { result = selected_row; break; }
    }
    *selection = result >= 0 ? result : selected_row; *scroll = offset;
    return result;
}
int ui_choices(UiContext *ui,const char *title,const char **labels,int count) {
    WINDOW *win=dialog_open(ui,title,10,70); if(!win) return -1;
    int selected=0,scroll=0;
    int result=choice_dialog(win,title,labels,count,&selected,&scroll,NULL,NOTICE_INFO,0);
    dialog_close(ui,win);return result;
}
int show_menu(UiContext *ui) {
    const char *labels[] = {"F1   Help", "F2   New...", "F3   Search", "F5   Copy", "F6   Move / Rename", "F7   Options", "F8   Permanent delete", "F10  Quit", "Backspace   Parent directory", "r    Refresh", "Ctrl+F  Find in current list", "Rename selected item", "!    Recent operation result", "z    Dismiss notification", "Select all visible items", "Clear selection", "View: Files + Preview", "View: Files only", "View: Left + Right files", "b    Favorite directories", "f    Filter / Clear current list", "e    Edit with Vim (cursor only)", "Open externally (cursor file)", "t    Move to Trash"};
    if(ui_panel(ui)->app.marks_len > 1) labels[11]="[disabled] Rename: multiple marked items";
    const int keys[] = {KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(5), KEY_F(6), KEY_F(7), KEY_F(8), KEY_F(10), KEY_BACKSPACE, 'r', UI_QUICK_FIND, UI_RENAME, UI_RESULT, 'z', UI_SELECT_ALL, UI_CLEAR_SELECTION, UI_MODE_PREVIEW, UI_MODE_FILES, UI_MODE_DUAL, UI_FAVORITES, UI_FILTER, UI_EDIT, UI_EXTERNAL, UI_TRASH};
    WINDOW *win = dialog_open(ui, "Menu", 14, 52);
    if (!win) return 0;
    int selected = 0, offset = 0;
    int i = choice_dialog(win,"Menu",labels,24,&selected,&offset,NULL,NOTICE_INFO,ui_panel(ui)->app.marks_len>1 ? 1u<<11 : 0);
    dialog_close(ui, win);
    if (i == 11 && ui_panel(ui)->app.marks_len > 1) { message(ui, "Rename unavailable: multiple marked items"); return 0; }
    return i < 0 ? 0 : keys[i];
}
void show_options(UiContext *ui) {
    WINDOW *win = dialog_open(ui, "Options", 10, 52);
    if (!win) return;
    int selected = 0, offset = 0;
    char warning[sizeof ui->status] = "Active panel sort/hidden saved as defaults";
    if(ui->settings.load_result.code!=RESULT_OK) snprintf(warning,sizeof warning,"%s",ui->settings.load_result.detail);
    NoticeKind warning_kind=ui->settings.load_result.code==RESULT_OK ? NOTICE_INFO : NOTICE_WARNING;
    bool replace=false;
    for (;;) {
        char hidden[64], preview_text[64], wheel[64], sort[64], direction[64], image[64], setup[96];
        snprintf(hidden, sizeof hidden, "[%c] Show hidden files", ui_panel(ui)->app.show_hidden ? 'x' : ' ');
        snprintf(preview_text, sizeof preview_text, "[%c] Show preview panel", ui_preview_enabled(ui) ? 'x' : ' ');
        snprintf(wheel, sizeof wheel, "Wheel scroll: %d rows (click to change)", ui->wheel_step);
        snprintf(sort, sizeof sort, "Sort by: %s (click to change)", sort_label(ui_panel(ui)->app.sort.key));
        snprintf(direction, sizeof direction, "Sort order: %s (click to change)", ui_panel(ui)->app.sort.descending ? "Descending" : "Ascending");
        snprintf(image,sizeof image,"Image preview: %s (click to change)",ui->image_auto ? "Auto" : "Off");
        snprintf(setup,sizeof setup,"Image display setup: %s",image_status_label(ui));
        const char *labels[] = {hidden, preview_text, wheel, sort, direction, image, replace ? "Confirm: replace existing settings file" : "Save current settings as startup defaults", setup, "Done"};
        int i = choice_dialog(win,"Options",labels,9,&selected,&offset,warning,warning_kind,0);
        if (i < 0 || i == 8) break;
        if(i!=6) replace=false;
        if(i==6) {
            if(ui->settings.replace_required && !replace) {
                replace=true; warning_kind=NOTICE_WARNING;
                snprintf(warning,sizeof warning,"Replace file using active panel sort/hidden?");
                continue;
            }
            StartupSettings s={.mode=ui->mode,.wheel_step=ui->wheel_step,
                .sort=ui_panel(ui)->app.sort,.show_hidden=ui_panel(ui)->app.show_hidden,.image_auto=ui->image_auto};
            Result r=settings_save(&ui->settings,&s,replace);
            replace=false; warning_kind=r.code==RESULT_OK ? NOTICE_SUCCESS : NOTICE_ERROR;
            snprintf(warning,sizeof warning,"%s",r.code==RESULT_OK ? "Saved startup defaults (active panel sort/hidden)" : r.detail);
            if(r.code==RESULT_OK) ui->settings_warning[0]=0;
        }
        if(i==7 && image_setup(ui,warning,sizeof warning)) {
            message(ui,"F7: retry image check after resize"); break;
        }
        if (i == 0) {
            ui_panel(ui)->app.show_hidden = !ui_panel(ui)->app.show_hidden;
            if (load_dir(ui, NULL).code != RESULT_OK) {
                ui_panel(ui)->app.show_hidden = !ui_panel(ui)->app.show_hidden;
                warning_kind=NOTICE_ERROR;
                snprintf(warning, sizeof warning, "%s", ui->status);
            }
        }
        if (i == 1) ui_set_mode(ui,ui_preview_enabled(ui) ? UI_LIST_ONLY : UI_LIST_PREVIEW);
        if (i == 2) ui->wheel_step = ui->wheel_step == 1 ? 3 : ui->wheel_step == 3 ? 5 : 1;
        if (i == 3) change_sort(ui, (SortSettings){(ui_panel(ui)->app.sort.key + 1) % 4, ui_panel(ui)->app.sort.descending});
        if (i == 4) change_sort(ui, (SortSettings){ui_panel(ui)->app.sort.key, !ui_panel(ui)->app.sort.descending});
        if (i == 5) {
            ui->image_auto = !ui->image_auto;
            graphics_probe_cancel(ui);
            if(ui->image_auto) {
                if(!ui->sixel_confirmed) ui->image_probe=IMAGE_PROBE_IDLE;
                else if(!ui->cell_width || !ui->cell_height) ui->image_cells_stale=true;
            }
            media_reset(ui);
        }
        draw(ui);
    }
    dialog_close(ui, win);
}
