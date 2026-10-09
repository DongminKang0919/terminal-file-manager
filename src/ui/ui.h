#ifndef TFILE_UI_H
#define TFILE_UI_H
#include "../core/core.h"
#include "text.h"
#include "../core/terminal.h"
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
enum { UI_BACK = KEY_MAX + 1, UI_FORWARD, UI_SGR_MOUSE, UI_RENAME, UI_QUICK_FIND, UI_RESULT, UI_SELECT_ALL, UI_CLEAR_SELECTION, UI_MODE_PREVIEW, UI_MODE_FILES, UI_MODE_DUAL, UI_FAVORITES, UI_FILTER, UI_EDIT, UI_EXTERNAL, UI_TRASH };
void input_init(void);
void input_terminal_begin(TerminalReply *reply);
void input_terminal_begin_timed(TerminalReply *reply,uint64_t deadline);
void input_terminal_end(void);
int terminal_input_wide(WINDOW *win,wint_t *key);
void terminal_input_reset(void);
bool terminal_input_armed(void);
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
    bool present, visible, refresh_attempted, peer_refresh_attempted;
    unsigned source_panel;
    NoticeKind kind;
    Result operation, refresh, peer_refresh;
    BatchJob batch;
    char action[32];
    char *source, *destination;
} OperationNotice;
/* One file panel owns its list, names, history and logical viewport. */
typedef struct {
    AppState app;
    size_t selected, top;
    bool stale; /* A failed dual-panel refresh blocks writes until a successful load. */
} UiFilePanel;
typedef enum { UI_LIST_ONLY, UI_LIST_PREVIEW, UI_LIST_LIST } UiMode;
typedef struct {
    SettingsStore settings;
    Favorites favorites;
    ExternalJob *external_job;
    char settings_warning[256];
    UiFilePanel panels[2];
    bool second_initialized;
    unsigned active;
    UiMode mode;
    int wheel_step;
    unsigned modal_depth;
    UiFocus focus;
    size_t preview_offset;
    bool preview_more, preview_scrollable;
    char *preview_path, *preview_link;
    PreviewSession *preview_session;
    PreviewText preview_page;
    Result preview_result;
    size_t preview_start, preview_limit;
    uint64_t preview_checked;
    bool preview_ready, preview_directory_empty;
    bool image_auto, sixel_confirmed, graphics_visible;
    /* Converted data and pixels surviving on the terminal are separate states. */
    uint64_t media_generation, graphics_generation;
    int graphics_x, graphics_y, graphics_rows, graphics_columns;
    unsigned graphics_width, graphics_height, graphics_cell_width, graphics_cell_height;
    enum { IMAGE_UNCONFIRMED, IMAGE_CHECKING, IMAGE_ENABLED, IMAGE_NO_RESPONSE, IMAGE_NO_CELLS, IMAGE_UNSUPPORTED, IMAGE_QUERY_FAILED, IMAGE_CANCELLED } image_status;
    /* Capability belongs to this process/terminal, never to a selected file or settings. */
    enum { IMAGE_PROBE_IDLE, IMAGE_PROBE_WAITING, IMAGE_PROBE_DONE } image_probe;
    TerminalReply image_reply;
    uint64_t image_deadline;
    bool image_probe_cells_only, image_cells_fixed, image_cells_stale;
    int image_columns, image_rows;
    unsigned cell_width, cell_height, media_width, media_height;
    unsigned media_output_width, media_output_height, media_target_width, media_target_height;
    uint64_t media_resize_due;
    PreviewMediaKind media_kind;
    MediaPreview *media_job;
    bool media_done, media_text, media_fallback;
    char *media_data;
    size_t media_len;
    Result media_result;
    char media_hint[128], media_fallback_detail[256];
    char status[512]; /* Routine/transient guidance, separate from the retained result. */
    NoticeKind status_kind;
    bool status_priority; /* New navigation/search issues can override a retained result display. */
    OperationNotice notice;
} UiContext;
static inline UiFilePanel *ui_panel(UiContext *ui) { return &ui->panels[ui->active]; }
static inline bool ui_preview_enabled(const UiContext *ui) { return ui->mode==UI_LIST_PREVIEW; }
/* Initialize a fresh or ui_free'd context, not a live context.
   After success or failure, ui_free releases all owned UI, settings and panels
   before reuse. Repeated ui_free on a released context is safe. */
Result ui_init(UiContext *ui, const char *directory);
Result ui_set_mode(UiContext *ui, UiMode mode);
bool ui_activate_panel(UiContext *ui, unsigned panel);
Result ui_refresh_panel(UiContext *ui, unsigned panel, const char *highlight, bool require_fresh);
bool ui_operation_allowed(UiContext *ui, char *warning, size_t size);
void refresh_peer_after_operation(UiContext *ui);
void refresh_operation_lists(UiContext *ui, const char *highlight);
void ui_free(UiContext *ui);
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
       UI_POP_FOOTER, UI_POP_WARNING, UI_POP_FOOT_WARNING, UI_POP_DISABLED, UI_COLUMNS, UI_DISABLED, UI_PREVIEW_WARNING, UI_SUCCESS, UI_WARNING, UI_ERROR, UI_POP_ERROR, UI_POP_FOOT_ERROR };
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
bool dialog_button_hit(WINDOW *win, const MEVENT *event, int y, int x, const char *label, bool enabled);
int dialog_focus_next(int focus, int count, bool reverse, unsigned disabled);
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
void draw_window_path(WINDOW *win, int y, int x, int width, const char *path);
size_t draw_window_page(WINDOW *win, int y, int x, int width, const char *text, size_t start);
int header_action(int x, int width);
typedef struct { int list_width, panel_height, list_y, list_rows; bool columns; } UiLayout;
UiLayout ui_layout(int width, int height, bool preview);
typedef struct { int top, rows; } UiPreviewBody;
UiPreviewBody ui_preview_body(int panel_height);
typedef struct {
    UiLayout list;
    int x[2], width[2];
    bool visible[2];
} UiScreenLayout;
UiScreenLayout ui_screen_layout(const UiContext *ui, int width, int height);
/* File panel paths/content share hit regions; hidden panels have none. */
int ui_panel_at(const UiScreenLayout *layout, int x, int y);
const char *ui_kind(const Item *it);
void ui_size(const Item *it, char *out, size_t size);
#define PREVIEW_METADATA_ROWS 5
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
attr_t ui_notice_style(NoticeKind kind);
void init_theme(void);
void show_help(UiContext *ui);
void show_favorites(UiContext *ui);
void show_filter(UiContext *ui);
void external_entry(UiContext *ui,bool edit);
bool external_prepare(UiContext *ui);
int ui_choices(UiContext *ui,const char *title,const char **labels,int count);
void graphics_init(UiContext *ui);
void graphics_probe_poll(UiContext *ui);
void graphics_probe_prepare(UiContext *ui, int rows);
void graphics_probe_cancel(UiContext *ui);
const char *image_status_label(const UiContext *ui);
bool image_setup(UiContext *ui,char *result,size_t capacity);
void graphics_clear(UiContext *ui);
void graphics_present(UiContext *ui);
void graphics_refresh(UiContext *ui);
typedef struct { int x, y, columns, rows; } UiMediaArea;
UiMediaArea ui_media_area(int width, int height);
bool graphics_inspect(const char *data, size_t len, unsigned width, unsigned height, unsigned *output_width, unsigned *output_height);
bool graphics_validate(const char *data, size_t len, unsigned width, unsigned height);
void media_reset(UiContext *ui);
void media_prepare(UiContext *ui, int rows, bool changed);
bool media_pending(const UiContext *ui);
void preview_prepare(UiContext *ui, int rows);
void preview_reset(UiContext *ui);
/* Existing preview actions currently consist of scrolling. Measure the same
   content layout used for drawing; never infer capability from file type. */
bool preview_can_focus(const UiContext *ui);
void preview_update_actions(UiContext *ui, int width, int height);
void preview_scroll(UiContext *ui, bool down);
void preview(UiContext *ui, int x, int y, int w, int h);
void search_items(UiContext *ui);
/* Form-owned paths; never borrow a panel directory across refreshes. */
typedef struct {
    unsigned source_panel;
    bool dual;
    char *base, *destination;
    Result destination_status;
} UiTransferContext;
bool ui_transfer_context(UiContext *ui, UiTransferContext *out);
void ui_transfer_context_free(UiTransferContext *context);
void transfer_entry(UiContext *ui, bool move_it);
void batch_entry(UiContext *ui, BatchAction action);
void batch_finish(UiContext *ui, BatchJob *job);
void run_batch_operation(UiContext *ui, BatchJob *job);
bool batch_review(UiContext *ui, const BatchJob *job);

/* Shared transfer picker; cancellation leaves result untouched. */
bool pick_path(UiContext *ui, bool folders_only, const char *initial, char *result, bool *resized);

bool prompt_value_status(UiContext *ui, const char *label, char *out, size_t size, const char *initial, bool *resized);


void trash_entry(UiContext *ui);
bool confirm_trash(UiContext *ui,const char *name,bool directory);
Result run_trash_operation(UiContext *ui,const char *source,char **destination);

#endif
