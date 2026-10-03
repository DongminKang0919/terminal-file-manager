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
enum { UI_BACK = KEY_MAX + 1, UI_FORWARD, UI_SGR_MOUSE, UI_RENAME, UI_QUICK_FIND, UI_RESULT, UI_SELECT_ALL, UI_CLEAR_SELECTION };
void input_init(void);
int input_key(WINDOW *win);
int input_wide(WINDOW *win, wint_t *key);
typedef struct { wchar_t value[UI_INPUT_CAP]; size_t len, cursor, start; } UiField;
/* Initial bytes are decoded losslessly; false means the editor capacity was exceeded. */
bool field_init(UiField *field, const char *initial);
/* kind distinguishes ncurses keys from Unicode codepoints; true means handled. */
bool field_edit(UiField *field, int kind, wint_t key);
/* Returns the cursor column relative to x; caller places the cursor after other drawing. */
int field_draw(WINDOW *win, int y, int x, int width, UiField *field, bool focused);
void field_click(UiField *field, int column);
typedef FileInfo Item;
typedef enum { UI_FOCUS_FILES, UI_FOCUS_PREVIEW } UiFocus;
typedef enum { NOTICE_INFO, NOTICE_SUCCESS, NOTICE_CANCELLED, NOTICE_WARNING, NOTICE_ERROR } NoticeKind;
typedef struct {
    bool present, visible, refresh_attempted;
    NoticeKind kind;
    Result operation, refresh;
    BatchJob batch;
    char action[32];
    char *source, *destination;
} OperationNotice;
typedef struct {
    AppState app;
    unsigned modal_depth;
    UiFocus focus;
    size_t selected, top, preview_offset;
    bool preview_more;
    char *preview_path, *preview_link;
    PreviewSession *preview_session;
    PreviewText preview_page;
    Result preview_result;
    size_t preview_start, preview_limit;
    uint64_t preview_checked;
    bool preview_ready;
    char status[512]; /* Routine/transient guidance, separate from the retained result. */
    NoticeKind status_kind;
    OperationNotice notice;
} UiContext;
void quick_find(UiContext *ui);
void notice_clear(UiContext *ui);
Result notice_prepare(OperationNotice *out, const char *action, const char *source, const char *destination);
void notice_commit(UiContext *ui, OperationNotice *prepared, Result result);
void notice_record(UiContext *ui, const char *action, Result result, const char *source, const char *destination);
void notice_dismiss(UiContext *ui);
const char *notice_label(NoticeKind kind);
void show_result(UiContext *ui);
enum { UI_BASE = 1, UI_BORDER, UI_HEADER, UI_SELECTED, UI_DIR, UI_MUTED, UI_STATUS, UI_PATH,
       UI_FILE, UI_LINK, UI_DIR_SELECTED, UI_FILE_SELECTED, UI_LINK_SELECTED,
       UI_EXEC, UI_HIDDEN, UI_SPECIAL, UI_EXEC_SELECTED, UI_HIDDEN_SELECTED, UI_SPECIAL_SELECTED,
       UI_INACTIVE, UI_POP_BODY, UI_POP_MUTED, UI_POP_BORDER, UI_POP_TITLE,
       UI_POP_FOOTER, UI_POP_WARNING, UI_POP_FOOT_WARNING, UI_POP_DISABLED, UI_COLUMNS, UI_DISABLED };
typedef enum { TYPE_DIR, TYPE_FILE, TYPE_EXEC, TYPE_HIDDEN, TYPE_LINK, TYPE_SPECIAL } FileType;
void message(UiContext *ui, const char *text);
Result load_dir(UiContext *ui, const char *highlight);
bool open_search_result(UiContext *ui, const char *path);
bool navigate(UiContext *ui, const char *path, const char *highlight);
void parent_dir(UiContext *ui);
void history_dir(UiContext *ui, bool forward);
void enter_item(UiContext *ui);
bool panel_key(UiContext *ui, int key, int height);
int join(char *out, size_t size, const char *directory, const char *name);
bool create_named_entry(UiContext *ui, bool directory, const char *name, char *warning, size_t size);
void create_entry(UiContext *ui, bool directory);
void delete_entry(UiContext *ui);
void rename_entry(UiContext *ui);
/* True closes the form, including unchanged names and success with refresh failure. */
bool rename_named_entry(UiContext *ui, const char *source, const char *name, char *warning, size_t size);
Result run_file_operation(UiContext *ui, bool copy, const char *source,
                          const char *directory, const char *name, char **destination);
bool transfer_path(UiContext *ui, bool move_it, const char *source, const char *directory, const char *name, char *warning, size_t size);
bool mouse_click(const MEVENT *e);
void dialog_frame(WINDOW *win, const char *title);
void dialog_refresh(WINDOW *win);
void dialog_button(WINDOW *win, int y, int x, const char *label, bool focused, bool enabled);
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
typedef struct { int list_width, panel_height, list_y, list_rows; bool columns; } UiLayout;
UiLayout ui_layout(int width, int height, bool preview);
const char *ui_kind(const Item *it);
void ui_size(const Item *it, char *out, size_t size);
#define PREVIEW_METADATA_ROWS 3
void draw(UiContext *ui);
void draw_cached(UiContext *ui);
void fit_selection(UiContext *ui, int rows);
void change_sort(UiContext *ui, SortSettings sort);
const char *sort_label(SortKey key);
FileType item_type(const Item *it);
char type_letter(FileType type);
int item_color(const Item *it, bool active);
attr_t ui_selection(void);
attr_t ui_bar(void);
void init_theme(void);
void show_help(UiContext *ui);
void preview_prepare(UiContext *ui, int rows);
void preview_reset(UiContext *ui);
void preview_scroll(UiContext *ui, bool down);
void preview(UiContext *ui, int x, int y, int w, int h);
void search_items(UiContext *ui);
void transfer_entry(UiContext *ui, bool move_it);
void batch_entry(UiContext *ui, BatchAction action);
void batch_finish(UiContext *ui, BatchJob *job);
void run_batch_operation(UiContext *ui, BatchJob *job);
bool batch_review(UiContext *ui, const BatchJob *job);

/* Shared transfer picker; cancellation leaves result untouched. */
bool pick_path(UiContext *ui, bool folders_only, const char *initial, char *result, bool *resized);

bool prompt_value_status(UiContext *ui, const char *label, char *out, size_t size, const char *initial, bool *resized);

#endif
