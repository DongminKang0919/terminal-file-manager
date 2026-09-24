#ifndef TFILE_UI_H
#define TFILE_UI_H
#include "../core/core.h"
#include "text.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

/* Existing terminal form input limit; core/platform paths are dynamically allocated. */
#define UI_INPUT_CAP 4096
enum { UI_BACK = KEY_MAX + 1, UI_FORWARD, UI_SGR_MOUSE };
void input_init(void);
int input_key(WINDOW *win);
int input_wide(WINDOW *win, wint_t *key);
typedef FileInfo Item;
typedef struct {
    AppState app;
    size_t selected, top, preview_offset;
    bool preview_more;
    char *preview_path, *preview_link;
    PreviewSession *preview_session;
    PreviewText preview_page;
    Result preview_result;
    size_t preview_start, preview_limit;
    uint64_t preview_checked;
    bool preview_ready;
    char status[512];
} UiContext;
enum { UI_BASE = 1, UI_BORDER, UI_HEADER, UI_SELECTED, UI_DIR, UI_MUTED, UI_STATUS, UI_PATH,
       UI_FILE, UI_LINK, UI_DIR_SELECTED, UI_FILE_SELECTED, UI_LINK_SELECTED,
       UI_EXEC, UI_HIDDEN, UI_SPECIAL, UI_EXEC_SELECTED, UI_HIDDEN_SELECTED, UI_SPECIAL_SELECTED };
typedef enum { TYPE_DIR, TYPE_FILE, TYPE_EXEC, TYPE_HIDDEN, TYPE_LINK, TYPE_SPECIAL } FileType;
void message(UiContext *ui, const char *text);
Result load_dir(UiContext *ui, const char *highlight);
bool open_search_result(UiContext *ui, const char *path);
bool navigate(UiContext *ui, const char *path, const char *highlight);
void parent_dir(UiContext *ui);
void history_dir(UiContext *ui, bool forward);
void enter_item(UiContext *ui);
int join(char *out, size_t size, const char *directory, const char *name);
bool create_named_entry(UiContext *ui, bool directory, const char *name, char *warning, size_t size);
void create_entry(UiContext *ui, bool directory);
void delete_entry(UiContext *ui);
Result run_file_operation(UiContext *ui, bool copy, const char *source,
                          const char *directory, const char *name, char **destination);
bool transfer_path(UiContext *ui, bool move_it, const char *source, const char *directory, const char *name, char *warning, size_t size);
bool mouse_click(const MEVENT *e);
void dialog_frame(WINDOW *win, const char *title);
WINDOW *dialog_open(UiContext *ui, const char *title, int height, int width);
bool dialog_closed(WINDOW *win, const MEVENT *e);
int mouse_key(WINDOW *win, int key);
void dialog_close(UiContext *ui, WINDOW *win);
bool search_prompt(UiContext *ui, WINDOW *win, char *out, size_t size);
bool prompt(UiContext *ui, const char *label, char *out, size_t size);
bool new_entry_dialog(UiContext *ui, bool directory, char *name, size_t size);
bool prompt_value(UiContext *ui, const char *label, char *out, size_t size, const char *initial);
bool confirm(UiContext *ui, const char *name, bool directory);
int show_menu(UiContext *ui);
void show_options(UiContext *ui);
void draw_text(int y, int x, int width, const char *s);
void draw_window_text(WINDOW *win, int y, int x, int width, const char *text);
size_t draw_window_page(WINDOW *win, int y, int x, int width, const char *text, size_t start);
int header_action(int x, int width);
void draw(UiContext *ui);
FileType item_type(const Item *it);
char type_letter(FileType type);
int item_color(const Item *it, bool active);
void init_theme(void);
void show_help(UiContext *ui);
void preview_prepare(UiContext *ui, int rows);
void preview_reset(UiContext *ui);
void preview_scroll(UiContext *ui, bool down);
void preview(UiContext *ui, int x, int y, int w, int h);
void search_items(UiContext *ui);
void transfer_entry(UiContext *ui, bool move_it);
#endif
