#include "ui.h"

UiLayout ui_layout(int width, int height, bool preview) {
    int list = preview ? (width < 80 ? width * 3 / 5 : width * 11 / 20) : width;
    bool columns = height >= 12;
    return (UiLayout){list, height - 4, columns ? 5 : 4, height - (columns ? 8 : 7), columns};
}
/* Rendering and hit testing share these rectangles, including the narrow fallback. */
UiScreenLayout ui_screen_layout(const UiContext *ui,int width,int height) {
    UiScreenLayout layout={.list=ui_layout(width,height,ui_preview_enabled(ui))};
    if(ui->mode==UI_LIST_LIST && width>=80) {
        layout.width[0]=width/2; layout.width[1]=width-width/2;
        layout.x[1]=width/2; layout.visible[0]=layout.visible[1]=true;
        layout.list.list_width=layout.width[ui->active];
    } else {
        layout.width[ui->active]=layout.list.list_width;
        layout.visible[ui->active]=true;
    }
    return layout;
}
int ui_panel_at(const UiScreenLayout *layout,int x,int y) {
    if(y<1 || y>=layout->list.panel_height+2) return -1;
    for(unsigned i=0;i<2;i++) if(layout->visible[i] && x>layout->x[i] && x<layout->x[i]+layout->width[i]-1) return (int)i;
    return -1;
}
const char *ui_kind(const Item *it) {
    if (!it->valid) return "?";
    return it->kind == FILE_DIRECTORY ? "Dir" : it->kind == FILE_LINK ? "Link" :
           it->kind == FILE_REGULAR ? "File" : "Other";
}
void ui_size(const Item *it, char *out, size_t size) {
    if (!it->valid || it->kind == FILE_DIRECTORY) { snprintf(out, size, "-"); return; }
    double value = (double)it->size;
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    int unit = 0;
    while (value >= 1024 && unit < 6) { value /= 1024; unit++; }
    snprintf(out, size, unit ? "%.1f %s" : "%.0f %s", value, units[unit]);
}

static void draw_box(int x, int y, int w, int h, const char *title, bool focused) {
    if (w < 2 || h < 2) return;
    attrset(COLOR_PAIR(focused ? UI_BASE : UI_BORDER) | (focused ? A_BOLD : A_NORMAL));
    mvhline(y, x, ACS_HLINE, w); mvhline(y + h - 1, x, ACS_HLINE, w);
    mvvline(y, x, ACS_VLINE, h); mvvline(y, x + w - 1, ACS_VLINE, h);
    mvaddch(y, x, ACS_ULCORNER); mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER); mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    if (w > 5) {
        attrset(COLOR_PAIR(UI_MUTED));
        mvaddch(y, x + 2, focused ? '*' : ' '); draw_text(y, x + 3, w - 5, title);
        attrset(COLOR_PAIR(UI_BASE) | A_BOLD);
        char caption[32];
        snprintf(caption,sizeof caption,"%.*s",(int)strcspn(title+1," "),title+1);
        draw_text(y,x+4,w-6,caption);
    }
    attrset(A_NORMAL);
}
void fit_selection(UiContext *ui, int rows) {
    if (ui_panel(ui)->app.files.len && ui_panel(ui)->selected >= ui_panel(ui)->app.files.len) ui_panel(ui)->selected = ui_panel(ui)->app.files.len - 1;
    if (!ui_panel(ui)->app.files.len) ui_panel(ui)->selected = 0;
    if (ui_panel(ui)->selected < ui_panel(ui)->top) ui_panel(ui)->top = ui_panel(ui)->selected;
    if (rows > 0 && ui_panel(ui)->selected >= ui_panel(ui)->top + (size_t)rows) ui_panel(ui)->top = ui_panel(ui)->selected - (size_t)rows + 1;
}
typedef struct { const char *label; const char *compact; int key; } Action;
static const Action actions[] = {
    { "F1 Help", "F1", KEY_F(1) }, { "F2 New", "F2", KEY_F(2) },
    { "F3 Search", "F3", KEY_F(3) }, { "F5 Copy", "F5", KEY_F(5) },
    { "F6 Move", "F6", KEY_F(6) }, { "F7 Options", "F7", KEY_F(7) },
    { "F8 Delete", "F8", KEY_F(8) }, { "F9 Menu", "F9", KEY_F(9) },
    { "F10 Quit", "F10", KEY_F(10) }
};
static const char *action_label(size_t index, int width) {
    int required = 1;
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i)
        required += (int)strlen(actions[i].label) + 3;
    return width >= required ? actions[index].label : actions[index].compact;
}
static bool action_bounds(size_t index, int width, int *at, int *length) {
    *at = 1;
    for (size_t i = 0; i <= index; i++) {
        *length = (int)strlen(action_label(i, width)) + 2;
        if (*at + *length >= width) return false;
        if (i != index) {
            int gap = width >= 120 && (i == 0 || i == 2 || i == 4 || i == 6) ? 3 : 1;
            *at += *length + gap;
        }
    }
    return true;
}
int header_action(int x, int width) {
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        int at, length;
        if (!action_bounds(i, width, &at, &length)) break;
        if (x >= at && x < at + length) return actions[i].key;
    }
    return 0;
}

static void draw_file_panel(UiContext *ui,UiFilePanel *p,unsigned index,int x,int mid,int h,UiLayout layout,bool focused) {
    int panel_h=layout.panel_height, rows=layout.list_rows;
    char title[320];
    snprintf(title, sizeof title, " Files (%zu) | %s %s ", p->app.files.len,
             sort_label(p->app.sort.key), p->app.sort.descending ? "descending" : "ascending");
    if ((int)strlen(title) > mid - 5)
        snprintf(title, sizeof title, " Files (%zu) %s%c ", p->app.files.len,
                 p->app.sort.key == SORT_MODIFIED ? "Time" : sort_label(p->app.sort.key),
                 p->app.sort.descending ? '-' : '+');
    if(ui->mode==UI_LIST_LIST) {
        snprintf(title,sizeof title," %s%s %s%c S:%zu M:%zu H:%d ", index ? "Right" : "Left",p->stale?" STALE":"",
                 p->app.sort.key==SORT_MODIFIED?"Time":sort_label(p->app.sort.key),p->app.sort.descending?'-':'+',
                 p->app.files.len,p->app.marks_len,p->app.show_hidden);
    } else if(p->stale) snprintf(title,sizeof title," Files STALE - refresh before writes ");
    draw_box(x, 2, mid, panel_h, title, focused);
    attron(COLOR_PAIR(UI_MUTED));
    draw_text(3, x+2, mid - 4, "[Parent]  [Open]");
    int date_x = mid >= 72 ? mid - 18 : 0;
    int size_x = mid >= 48 ? (date_x ? date_x - 11 : mid - 12) : 0;
    int kind_x = size_x ? size_x - 6 : ui->mode==UI_LIST_LIST && mid<48 ? 0 : mid - 7;
    int name_width = kind_x ? kind_x - 4 : mid - 5;
    if (layout.columns) {
        attrset(COLOR_PAIR(UI_COLUMNS) | (COLORS < 256 ? A_UNDERLINE : A_NORMAL));
        mvhline(4, x+1, ' ', mid - 2);
        draw_text(4, x+3, name_width, "Name");
        if(kind_x) draw_text(4, x+kind_x, 5, "Kind");
        if (size_x) draw_text(4, x+size_x + 5, 4, "Size");
        if (date_x) draw_text(4, x+date_x, 16, "Modified");
    }
    attrset(A_NORMAL);
    if(p->app.files.len && p->selected>=p->app.files.len) p->selected=p->app.files.len-1;
    if(!p->app.files.len) p->selected=0;
    if(p->selected<p->top) p->top=p->selected;
    if(rows>0 && p->selected>=p->top+(size_t)rows) p->top=p->selected-(size_t)rows+1;
    for (int r = 0; r < rows && p->top + (size_t)r < p->app.files.len; ++r) {
        Item *it = &p->app.files.entries[p->top + (size_t)r];
        int y = layout.list_y + r;
        bool cursor = p->top + (size_t)r == p->selected;
        bool active=cursor&&(focused||ui->mode!=UI_LIST_LIST);
        attr_t style = active ? ui_selection() : COLOR_PAIR(UI_BASE);
        attrset(style);
        mvhline(y, x+1, ' ', mid - 2);
        mvaddch(y, x+1, cursor ? (focused||ui->mode!=UI_LIST_LIST ? '>' : ':') : ' ');
        mvaddch(y, x+2, app_marked(&p->app, it->name) ? '*' : ' ');
        if (!active) attrset(COLOR_PAIR(item_color(it, false)));
        draw_text(y, x+3, name_width, it->name);
        attrset(active ? style : COLOR_PAIR(UI_MUTED));
        if(kind_x) draw_text(y, x+kind_x, 5, ui_kind(it));
        if (size_x) {
            char size[32], aligned[40]; ui_size(it, size, sizeof size);
            snprintf(aligned, sizeof aligned, "%9s", size);
            draw_text(y, x+size_x, 9, aligned);
        }
        if (date_x) {
            time_t modified = (time_t)it->modified; struct tm *tm = localtime(&modified);
            char date[32] = "?";
            if (it->valid && tm) strftime(date, sizeof date, "%Y-%m-%d %H:%M", tm);
            draw_text(y, x+date_x, 16, date);
        }
        attrset(A_NORMAL);
    }
    if (p->app.files.len && (p->top || p->app.files.len > (size_t)rows)) {
        size_t end = p->top + (size_t)rows;
        if (end > p->app.files.len) end = p->app.files.len;
        char range[96];
        snprintf(range, sizeof range, " Shown %zu-%zu/%zu ", p->top + 1, end, p->app.files.len);
        if ((int)strlen(range) > mid - 4)
            snprintf(range, sizeof range, " %s %s ", p->top ? "^ more" : "Top", end < p->app.files.len ? "v more" : "End");
        attrset(COLOR_PAIR(UI_MUTED));
        draw_text(h - 3, x+2, mid - 4, range);
        attrset(A_NORMAL);
    }
    if (!p->app.files.len) {
        draw_text(layout.list_y, x+2, mid - 4, "No visible items");
        attron(COLOR_PAIR(UI_MUTED));
        draw_text(layout.list_y + 1, x+2, mid - 4, p->app.show_hidden ? "F2: New" : "F7: Show hidden files");
        attroff(COLOR_PAIR(UI_MUTED));
    }
}

static void draw_location(UiFilePanel *p,int x,int w,bool dual) {
    attrset(COLOR_PAIR(UI_PATH));
    mvhline(1, x, ' ', w);
    if (!p->app.history_at) attrset(COLOR_PAIR(UI_DISABLED) | A_DIM);
    draw_text(1, x+1, 3, "[<]"); attrset(COLOR_PAIR(UI_PATH));
    if (p->app.history_at + 1 >= p->app.history_len) attrset(COLOR_PAIR(UI_DISABLED) | A_DIM);
    draw_text(1, x+5, 3, "[>]"); attrset(COLOR_PAIR(UI_MUTED));
    if(!dual) draw_text(1, x+10, 10, "Location:");
    attrset(COLOR_PAIR(UI_PATH));
    int start_col=dual?10:20; int available=w-start_col-1;
    if (ui_text_span(p->app.directory, 0, available).more) {
        mvaddstr(1, x+start_col, "... ");
        size_t start = ui_text_tail(p->app.directory, available - 4);
        draw_window_page(stdscr, 1, x+start_col+4, available - 4, p->app.directory, start);
    } else draw_text(1, x+start_col, available, p->app.directory);
    attroff(COLOR_PAIR(UI_PATH));
}

static void draw_screen(UiContext *ui, bool prepare) {
    if (!ui_preview_enabled(ui)) ui->focus = UI_FOCUS_FILES;
    graphics_clear(ui);
    erase(); int h, w; getmaxyx(stdscr, h, w);
    if (prepare) preview_prepare(ui, h >= 9 && w >= 50 ? h - 7 : 0);
    UiLayout preview_layout = ui_layout(w, h, ui_preview_enabled(ui));
    preview_update_actions(ui, w - preview_layout.list_width, h >= 9 && w >= 50 ? preview_layout.panel_height : 0);
    if (h < 9 || w < 50) { mvaddstr(0, 0, "Terminal too small (minimum 50x9)"); refresh(); return; }
    attrset(ui_bar()); mvhline(0, 0, ' ', w);
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        const char *label = action_label(i, w);
        int at, length;
        if (!action_bounds(i, w, &at, &length)) break;
        mvaddch(0, at, '['); draw_text(0, at + 1, length - 2, label);
        mvaddch(0, at + length - 1, ']');
        attron(A_BOLD);
        draw_text(0, at + 1, (int)strlen(actions[i].compact), actions[i].compact);
        attroff(A_BOLD);
    }
    UiScreenLayout screen=ui_screen_layout(ui,w,h);
    for(unsigned i=0;i<2;i++) if(screen.visible[i])
        draw_location(&ui->panels[i],screen.x[i],ui->mode==UI_LIST_LIST?screen.width[i]:w,ui->mode==UI_LIST_LIST);
    UiLayout layout=screen.list;
    int mid=layout.list_width, panel_h=layout.panel_height;
    for(unsigned i=0;i<2;i++) if(screen.visible[i])
        draw_file_panel(ui,&ui->panels[i],i,screen.x[i],screen.width[i],h,layout,ui->active==i&&ui->focus==UI_FOCUS_FILES);
    if(ui_preview_enabled(ui)) {
        draw_box(mid,2,w-mid,panel_h," Preview ",ui->focus==UI_FOCUS_PREVIEW && preview_can_focus(ui));
        preview(ui,mid,2,w-mid,panel_h);
    }
    bool refresh_issue=ui->notice.present &&
        ((ui->notice.refresh_attempted && ui->notice.refresh.code!=RESULT_OK) ||
         (ui->notice.peer_refresh_attempted && ui->notice.peer_refresh.code!=RESULT_OK));
    char summary[96];
    snprintf(summary,sizeof summary,"Shown: %zu | Hidden: %s Sel:%zu",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden ? "on" : "off",ui_panel(ui)->app.marks_len);
    if(w>=90 && ui_panel(ui)->app.marks_len)
        snprintf(summary,sizeof summary,"Shown: %zu | Hidden: %s | Selected: %zu",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden ? "on" : "off",ui_panel(ui)->app.marks_len);
    if(w<90 && (ui_panel(ui)->app.marks_len || (ui->notice.present && ui->notice.visible)))
        snprintf(summary,sizeof summary,"Shown: %zu H:%s Sel:%zu",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden ? "on" : "off",ui_panel(ui)->app.marks_len);
    /* Reserve routine guidance as well as all three counts on narrow screens.
       S=Shown, H=hidden (1/on, 0/off), M=marked. Retained result labels stay separate. */
    if(!(ui->notice.present && ui->notice.visible && !ui->status_priority) && w<90 &&
       strlen(ui->status)+(ui->status_kind==NOTICE_INFO?6u:0u) > (size_t)(w-(int)strlen(summary)-4))
        snprintf(summary,sizeof summary,"S:%zu H:%d M:%zu",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden,ui_panel(ui)->app.marks_len);
    if(ui->mode==UI_LIST_LIST) snprintf(summary,sizeof summary,"%s* / %s  S:%zu H:%d M:%zu",ui->active?"Right":"Left",ui->active?"Left":"Right",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden,ui_panel(ui)->app.marks_len);
    if(w<90 && (ui->status_priority || (refresh_issue && ui->notice.visible))) {
        if(ui->mode==UI_LIST_LIST)
            snprintf(summary,sizeof summary,"%s* S:%zu H:%d M:%zu",ui->active?"Right":"Left",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden,ui_panel(ui)->app.marks_len);
        else snprintf(summary,sizeof summary,"S:%zu H:%d M:%zu",ui_panel(ui)->app.files.len,ui_panel(ui)->app.show_hidden,ui_panel(ui)->app.marks_len);
    }
    attron(COLOR_PAIR(UI_STATUS)); mvhline(h - 2, 0, ' ', w);
    int summary_width = (int)strlen(summary);
    draw_text(h - 2, 1, summary_width, summary);
    int x = summary_width + 3;
    char alert[600];
    if (ui->notice.present && ui->notice.visible && !ui->status_priority) {
        snprintf(alert, sizeof alert, "[%s] ! %s%s%s", notice_label(ui->notice.kind),
                 refresh_issue ? "refresh failed" : "detail", w>=90 ? ": " : "", w>=90 ? ui->notice.action : "");
    } else snprintf(alert, sizeof alert, "%s%s%s", ui->status[0] && ui->status_kind == NOTICE_INFO ? "Info" : "",
                    ui->status[0] && ui->status_kind == NOTICE_INFO ? ": " : "", ui->status);
    if(ui->settings_warning[0]) snprintf(alert,sizeof alert,"%s",ui->settings_warning);
    draw_text(h - 2, x, w - x - 1, alert);
    attroff(COLOR_PAIR(UI_STATUS));
    attrset(ui_bar()); mvhline(h - 1, 0, ' ', w);
    if (!ui->modal_depth) {
        bool reading = ui->focus == UI_FOCUS_PREVIEW;
        const char *keys[] = {reading ? "Esc" : "Enter", reading ? "Up/Down" : "Tab", reading ? "PgUp/PgDn" : "Backspace", reading ? "Home" : "F1", reading ? "Tab" : "F9"};
        const char *labels[] = {reading ? ": Files" : ": Open", reading ? ": Row" : (ui->mode==UI_LIST_LIST ? ": Other" : ui_preview_enabled(ui) ? ": Preview" : ": Files"), reading ? ": Page" : ": Parent", reading ? ": Top" : ": Help", reading ? ": Files" : ": Menu"};
        int x = 1;
        for (size_t i = 0; i < 5; i++) {
            if (!reading && i == 1 && (ui->mode!=UI_LIST_LIST && !preview_can_focus(ui))) continue;
            int keylen = (int)strlen(keys[i]), len = keylen + (int)strlen(labels[i]);
            if (x + len > w - 1) break;
            attron(A_BOLD); draw_text(h - 1, x, keylen, keys[i]); attroff(A_BOLD);
            draw_text(h - 1, x + keylen, len - keylen, labels[i]);
            x += len + 2;
        }
    }
    attrset(A_NORMAL);
    if (ui->modal_depth) {
        /* Change cell attributes only; keep wide glyphs and ACS border characters. */
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            cchar_t cell; wchar_t text[CCHARW_MAX]; attr_t attr; short pair;
            mvwin_wch(stdscr, y, x, &cell); getcchar(&cell, text, &attr, &pair, NULL);
            mvchgat(y, x, 1, (attr & A_ALTCHARSET) | A_DIM, has_colors() ? UI_INACTIVE : 0, NULL);
        }
    }
    refresh(); graphics_present(ui);
}

/* Modal focus changes consume prepared data only: no metadata poll or reader I/O. */
void draw_cached(UiContext *ui) {
    if (stdscr && ui_panel(ui)->app.directory) draw_screen(ui, false);
}
void draw(UiContext *ui) { draw_screen(ui, true); }
