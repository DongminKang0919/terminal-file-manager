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
    switch (item_type(it)) {
        case TYPE_DIR: return active ? UI_DIR_SELECTED : UI_DIR;
        case TYPE_EXEC: return active ? UI_EXEC_SELECTED : UI_EXEC;
        case TYPE_HIDDEN: return active ? UI_HIDDEN_SELECTED : UI_HIDDEN;
        case TYPE_LINK: return active ? UI_LINK_SELECTED : UI_LINK;
        case TYPE_SPECIAL: return active ? UI_SPECIAL_SELECTED : UI_SPECIAL;
        default: return active ? UI_FILE_SELECTED : UI_FILE;
    }
}

void init_theme(void) {
    if (!has_colors()) return;
    start_color();
    if (COLORS >= 256) {
        init_pair(UI_BASE, 255, 235);
        init_pair(UI_BORDER, 111, 235);
        init_pair(UI_HEADER, 255, 24);
        init_pair(UI_SELECTED, 255, 25);
        init_pair(UI_DIR, 117, 235);
        init_pair(UI_MUTED, 250, 235);
        init_pair(UI_STATUS, 255, 238);
        init_pair(UI_PATH, 16, 153);
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
        init_pair(UI_BASE, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_BORDER, COLOR_BLUE, COLOR_BLACK);
        init_pair(UI_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_SELECTED, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_DIR, COLOR_CYAN, COLOR_BLACK);
        init_pair(UI_MUTED, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_STATUS, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_PATH, COLOR_BLACK, COLOR_CYAN);
        init_pair(UI_FILE, COLOR_WHITE, COLOR_BLACK);
        init_pair(UI_LINK, COLOR_YELLOW, COLOR_BLACK);
        init_pair(UI_DIR_SELECTED, COLOR_CYAN, COLOR_BLUE);
        init_pair(UI_FILE_SELECTED, COLOR_WHITE, COLOR_BLUE);
        init_pair(UI_LINK_SELECTED, COLOR_YELLOW, COLOR_BLUE);
        init_pair(UI_EXEC, COLOR_GREEN, COLOR_BLACK);
        init_pair(UI_HIDDEN, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(UI_SPECIAL, COLOR_RED, COLOR_BLACK);
        init_pair(UI_EXEC_SELECTED, COLOR_GREEN, COLOR_BLUE);
        init_pair(UI_HIDDEN_SELECTED, COLOR_MAGENTA, COLOR_BLUE);
        init_pair(UI_SPECIAL_SELECTED, COLOR_RED, COLOR_BLUE);
    }
    bkgd(COLOR_PAIR(UI_BASE));
}

