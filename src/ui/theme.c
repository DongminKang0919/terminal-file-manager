#include "ui.h"

FileType item_type(const Item *it) {
    if (it->hidden) return TYPE_HIDDEN;
    if (!it->valid) return TYPE_SPECIAL;
    if ((it->kind == FILE_LINK)) return TYPE_LINK;
    if ((it->kind == FILE_DIRECTORY)) return TYPE_DIR;
    if ((it->kind == FILE_REGULAR)) return it->executable ? TYPE_EXEC : TYPE_FILE;
    return TYPE_SPECIAL;
}

char type_letter(FileType type) {
    static const char letters[] = { 'D', 'F', 'X', 'H', 'L', 'S' };
    return letters[type];
}

int item_color(const Item *it, bool active) {
    if (active) return UI_SELECTED;
    switch (item_type(it)) {
        case TYPE_DIR: return active ? UI_DIR_SELECTED : UI_DIR;
        case TYPE_EXEC: return active ? UI_EXEC_SELECTED : UI_EXEC;
        case TYPE_HIDDEN: return active ? UI_HIDDEN_SELECTED : UI_HIDDEN;
        case TYPE_LINK: return active ? UI_LINK_SELECTED : UI_LINK;
        case TYPE_SPECIAL: return active ? UI_SPECIAL_SELECTED : UI_SPECIAL;
        default: return active ? UI_FILE_SELECTED : UI_FILE;
    }
}

attr_t ui_selection(void) {
    return A_BOLD | (has_colors() ? COLOR_PAIR(UI_SELECTED) : A_REVERSE);
}

attr_t ui_bar(void) {
    return has_colors() ? COLOR_PAIR(UI_HEADER) : A_NORMAL;
}

attr_t ui_notice_style(NoticeKind kind) {
    int pair=kind==NOTICE_SUCCESS ? UI_SUCCESS : kind==NOTICE_WARNING ? UI_WARNING :
        kind==NOTICE_ERROR ? UI_ERROR : UI_MUTED;
    return COLOR_PAIR(pair) | ((kind==NOTICE_WARNING || kind==NOTICE_ERROR) ? A_BOLD : A_NORMAL);
}
void init_theme(void) {
    if (!has_colors()) return;
    start_color();
    if (COLORS >= 256) {
        init_pair(UI_SUCCESS, 157, 235);
        init_pair(UI_WARNING, 222, 235);
        init_pair(UI_ERROR, 210, 235);
        init_pair(UI_PREVIEW_WARNING, 222, 235);
        init_pair(UI_BASE, 255, 235);
        init_pair(UI_BORDER, 242, 235);
        init_pair(UI_HEADER, 255, 237);
        init_pair(UI_COLUMNS, 250, 236);
        init_pair(UI_DISABLED, 242, 235);
        init_pair(UI_SELECTED, 255, 25);
        init_pair(UI_DIR, 117, 235);
        init_pair(UI_MUTED, 250, 235);
        init_pair(UI_STATUS, 255, 235);
        init_pair(UI_PATH, 255, 235);
        init_pair(UI_FILE, 255, 235);
        init_pair(UI_LINK, 222, 235);
        init_pair(UI_DIR_SELECTED, 159, 25);
        init_pair(UI_FILE_SELECTED, 255, 25);
        init_pair(UI_LINK_SELECTED, 229, 25);
        init_pair(UI_EXEC, 157, 235);
        init_pair(UI_HIDDEN, 183, 235);
        init_pair(UI_SPECIAL, 210, 235);
        init_pair(UI_EXEC_SELECTED, 194, 25);
        init_pair(UI_HIDDEN_SELECTED, 225, 25);
        init_pair(UI_SPECIAL_SELECTED, 224, 25);
    } else {
        init_pair(UI_SUCCESS, COLOR_GREEN, COLOR_BLACK);
        init_pair(UI_WARNING, COLOR_YELLOW, COLOR_BLACK);
        init_pair(UI_ERROR, COLOR_RED, COLOR_BLACK);
        init_pair(UI_PREVIEW_WARNING, COLOR_YELLOW, COLOR_BLACK);
        init_pair(UI_BASE, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_BORDER, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_HEADER, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_COLUMNS, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_DISABLED, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_SELECTED, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_DIR, COLOR_CYAN, COLOR_BLACK);
        init_pair(UI_MUTED, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_STATUS, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_PATH, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_FILE, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_LINK, COLOR_YELLOW, COLOR_BLACK);
        init_pair(UI_DIR_SELECTED, COLOR_CYAN, COLOR_BLUE);
        init_pair(UI_FILE_SELECTED, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_LINK_SELECTED, COLOR_YELLOW, COLOR_BLUE);
        init_pair(UI_EXEC, COLOR_GREEN, COLOR_BLACK);
        init_pair(UI_HIDDEN, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(UI_SPECIAL, COLOR_RED, COLOR_BLACK);
        init_pair(UI_EXEC_SELECTED, COLOR_GREEN, COLOR_BLUE);
        init_pair(UI_HIDDEN_SELECTED, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_SPECIAL_SELECTED, COLOR_WHITE, COLOR_BLUE);
    }
    if (COLORS >= 256) {
        init_pair(UI_INACTIVE, 242, 235);
        init_pair(UI_POP_BODY, 255, 238);
        init_pair(UI_POP_MUTED, 250, 238);
        init_pair(UI_POP_BORDER, 250, 238);
        init_pair(UI_POP_TITLE, 255, 25);
        init_pair(UI_POP_FOOTER, 250, 237);
        init_pair(UI_POP_ERROR, 210, 238);
        init_pair(UI_POP_FOOT_ERROR, 210, 237);
        init_pair(UI_POP_WARNING, 222, 238);
        init_pair(UI_POP_FOOT_WARNING, 222, 237);
        init_pair(UI_POP_DISABLED, 244, 237);
    } else {
        init_pair(UI_INACTIVE, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_POP_BODY, COLOR_BLACK, COLOR_WHITE);
        init_pair(UI_POP_MUTED, COLOR_BLACK, COLOR_WHITE);
        init_pair(UI_POP_BORDER, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_POP_TITLE, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_POP_FOOTER, COLOR_BLACK, COLOR_WHITE);
        init_pair(UI_POP_ERROR, COLOR_RED, COLOR_WHITE);
        init_pair(UI_POP_FOOT_ERROR, COLOR_RED, COLOR_WHITE);
        init_pair(UI_POP_WARNING, COLOR_BLACK, COLOR_YELLOW);
        init_pair(UI_POP_FOOT_WARNING, COLOR_BLACK, COLOR_YELLOW);
        init_pair(UI_POP_DISABLED, COLOR_BLACK, COLOR_WHITE);
    }
    init_pair(UI_COMMAND, COLORS >= 256 ? 80 : COLOR_CYAN, COLORS >= 256 ? 237 : COLOR_BLACK);
    init_pair(UI_FOCUS, COLORS >= 256 ? 117 : COLOR_BLUE, COLORS >= 256 ? 235 : COLOR_BLACK);
    init_pair(UI_MARK, COLORS >= 256 ? 222 : COLOR_YELLOW, COLORS >= 256 ? 235 : COLOR_BLACK);
    init_pair(UI_MARK_SELECTED, COLORS >= 256 ? 229 : COLOR_YELLOW, COLORS >= 256 ? 25 : COLOR_BLUE);
    bkgd(COLOR_PAIR(UI_BASE));
}

