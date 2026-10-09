#ifndef TFILE_CORE_H
#define TFILE_CORE_H
#include "../model.h"

typedef enum { SORT_NAME, SORT_SIZE, SORT_MODIFIED, SORT_KIND } SortKey;
typedef struct { SortKey key; bool descending; } SortSettings;
/* Values match the UI screen modes, without depending on curses. */
typedef struct {
    int mode, wheel_step;
    SortSettings sort;
    bool show_hidden, image_auto;
} StartupSettings;
typedef struct {
    StartupSettings defaults;
    char *path;
    bool replace_required;
    Result load_result;
} SettingsStore;
StartupSettings settings_defaults(void);
Result settings_parse(const char *data, size_t len, StartupSettings *out);
/* Load into a fresh or settings_free'd store. The store owns path until
   settings_free, including read/parse failures; do not reload a live store. */
void settings_load(SettingsStore *store);
Result settings_save(SettingsStore *store, const StartupSettings *settings, bool replace);
void settings_free(SettingsStore *store);

#define FAVORITES_MAX 64
#define FAVORITES_BYTES 65536
#define FAVORITE_NAME_BYTES 128
typedef struct { char *name, *directory; } Favorite;
typedef struct { Favorite *entries; size_t len; char *path; Result load_result; } Favorites;
void favorites_load(Favorites *store);
void favorites_free(Favorites *store);
Result favorites_add(Favorites *store,const char *directory,const char *name);
Result favorites_rename(Favorites *store,size_t index,const char *name);
Result favorites_remove(Favorites *store,size_t index);

/* Owned history data; positions are logical list ordinals, never terminal coordinates. */
typedef struct { char *directory, *selected_name; size_t selected, top; } HistoryEntry;
typedef struct { char *name; bool seen; } SelectionName;
typedef enum { FILTER_NONE, FILTER_CONTAINS, FILTER_GLOB } FilterKind;
typedef struct {
    SelectionName *marks; size_t marks_len, marks_cap;
    SortSettings sort;
    char *directory;
    FileList files;
    bool show_hidden;
    FilterKind filter_kind; char *filter; size_t unfiltered_count;
    HistoryEntry *history;
    size_t history_len, history_at;
} AppState;
/* Main-list sorting only; core_list/search keep their legacy ordering.
   Reordering preserves owned strings and performs no filesystem queries. */
int main_file_compare(const FileInfo *a, const FileInfo *b, SortSettings sort);
void main_list_sort(FileList *list, SortSettings sort);
void app_set_sort(AppState *app, SortSettings sort, size_t *selected);
bool app_marked(const AppState *app, const char *name);
Result app_mark_toggle(AppState *app, const char *name);
Result app_mark_all(AppState *app);
void app_unmark(AppState *app, const char *name);
void app_marks_clear(AppState *app);
void app_marks_reconcile(AppState *app);
Result app_init(AppState *app, const char *directory);
/* Applies validated startup settings before the first directory load. */
Result app_init_settings(AppState *app, const char *directory, const StartupSettings *settings);
void app_free(AppState *app);
Result app_navigate(AppState *app, const char *directory);
Result app_refresh(AppState *app);
/* Successful filter apply (including clear/reapply) clears all marks. */
Result app_set_filter(AppState *app,FilterKind kind,const char *pattern);
Result app_history(AppState *app, bool forward);
/* No filesystem I/O. Allocation failure leaves the previous saved state intact. */
Result app_remember_selection(AppState *app, size_t selected, size_t top);
/* Commit navigation/filter/history only if the searched item can be opened and
   selected. selected changes only on success; revealed reports a filter change. */
Result app_open_search_result(AppState *app, const char *path, size_t *selected, bool *revealed);
/* All returned lists/strings are caller-owned; free lists via file_list_free,
   strings via free. Failed pointer-producing calls leave NULL outputs. */
Result core_list(const char *directory, bool hidden, bool directories_only, FileList *out);
int file_compare(const void *a, const void *b);
bool text_contains(const char *text, const char *needle);
char *core_path_join(const char *directory, const char *name);
char *core_path_parent(const char *path);
char *core_path_name(const char *path);
Result core_resolve_directory(const char *base, const char *input, char **out);
Result core_info(const char *path, FileInfo *out);
uint64_t core_monotonic_ms(void);
Result core_create(const char *directory, const char *name, bool is_directory);
Result core_transfer(bool move, const char *source, const char *directory, const char *name, char **destination);
Result core_delete_progress(const char *path, OperationCallback callback, void *context);
/* Same-filesystem move remains a single non-cancellable call; callback ignored. */
Result core_transfer_progress(bool move, const char *source, const char *directory, const char *name,
                              char **destination, OperationCallback callback, void *context);
Result core_delete(const char *path);
Result core_trash_progress(const char *path,char **destination,OperationCallback callback,void *context);
Result core_link_target(const char *path, char **out);

/* Owned target snapshot and fixed results, prepared before any file changes.
   This fixes paths/order, not filesystem identities. Single APIs revalidate. */
typedef enum { BATCH_COPY, BATCH_MOVE, BATCH_DELETE, BATCH_TRASH } BatchAction;
typedef enum { BATCH_UNEXECUTED, BATCH_SUCCESS, BATCH_FAILED, BATCH_CANCELLED, BATCH_SKIPPED } BatchStatus;
typedef struct {
    char *source, *name, *destination;
    FileKind kind;
    BatchStatus status;
    Result result;
} BatchTarget;
typedef enum { CONFLICT_STOP, CONFLICT_SKIP, CONFLICT_SKIP_ALL, CONFLICT_CANCEL } BatchConflictDecision;
typedef BatchConflictDecision (*BatchConflictCallback)(const BatchTarget *,const char *directory,void *);
typedef struct {
    BatchTarget *targets; size_t len, succeeded, skipped;
    BatchAction action;
    char *directory;
    Result result;
    bool executed;
    BatchConflictCallback conflict; void *conflict_context; /* borrowed for execution only */
} BatchJob;
typedef struct {
    size_t index, total, succeeded, skipped; /* index is zero-based; recursive counts separate */
    OperationProgress progress;
} BatchProgress;
typedef bool (*BatchCallback)(const BatchProgress *, void *);
Result batch_prepare(const AppState *app, size_t cursor, BatchAction action, BatchJob *out);
Result batch_destination(BatchJob *job, const char *directory);
void batch_cancel(BatchJob *job); /* confirmation/progress-window cancellation, no changes */
Result batch_execute(BatchJob *job, BatchCallback callback, void *context);
void batch_free(BatchJob *job);
void app_marks_apply_result(AppState *app, const BatchJob *job);

typedef struct { char **argv; size_t len; } EditorCommand;
Result editor_command_parse(const char *text,EditorCommand *out);
void editor_command_free(EditorCommand *command);
Result core_vim_check(const char *path,bool *read_only);
Result core_vim_run(const char *path);
Result core_editor_run(const char *path);
typedef struct ExternalJob ExternalJob;
Result core_external_open(const char *path,ExternalJob **out);
Result core_external_poll(ExternalJob *job,bool *done);
void core_external_close(ExternalJob *job);
bool core_external_cleanup_pending(void);
bool core_terminal_size(unsigned *rows,unsigned *columns);

typedef struct { size_t max_visited, max_results; unsigned max_depth; } SearchLimits;
typedef struct {
    FileList matches;
    size_t visited;
    bool stopped, limited, incomplete;
    size_t skipped_directories, skipped_entries;
    Result first_omission; /* First skipped path/reason; counts include all omissions. */
    Result result; /* Non-OK means fatal error, with already-found matches retained. */
} SearchResult;
/* Borrowed progress and current_path are valid only during callback.
   Return false to cancel. No input/event-loop dependency in the engine. */
typedef bool (*SearchProgress)(const SearchResult *progress, const char *current_path, void *context);
SearchLimits search_default_limits(void);
SearchResult core_search(const char *root, const char *term, SearchLimits limits, SearchProgress callback, void *context);
void search_result_free(SearchResult *result);

typedef struct {
    char **lines;
    size_t len, skipped;
    bool binary, more, empty;
} PreviewText;
/* Includes hidden names, ignores dot entries, and stops at the first child.
   No child metadata, recursive traversal or total count is needed. */
Result core_preview_directory_empty(const char *path, bool *empty);
/* Returns at most limit content chunks; chunks match the existing preview's
   4175-byte line limit. Memory usage is bounded by the requested page. */
Result core_preview_text(const char *path, size_t start, size_t limit, PreviewText *out);
/* Session owns one reader and a 512-chunk ring. Pages are independent owned copies.
   Check metadata before requesting a new viewport; changed files restart at row 0. */
#define PREVIEW_PAGE_MAX 256
typedef struct PreviewSession PreviewSession;
Result preview_session_open(const char *path, PreviewSession **out);
void preview_session_close(PreviewSession *session);
Result preview_session_check(PreviewSession *session, bool *changed);
Result preview_session_page(PreviewSession *session, size_t start, size_t limit, PreviewText *out);
void preview_text_free(PreviewText *text);
PreviewMediaKind preview_session_media(const PreviewSession *session);
/* Single asynchronous converter; CANCELLED means its predecessor is retiring.
   Poll transfers owned output bytes once on completion; close cancels/reaps.
   The caller caches one selected file/size/page result and discards it on refresh. */
typedef struct MediaPreview MediaPreview;
Result core_media_open(const char *path, PreviewMediaKind kind, bool text, unsigned width, unsigned height, MediaPreview **out);
Result core_media_poll(MediaPreview *media, bool *done, char **data, size_t *len);
void core_media_close(MediaPreview *media);
void core_media_reap(void);
void core_media_shutdown(void);
bool core_media_cleanup_pending(void);
void core_media_install_signals(void);
bool core_media_shutdown_requested(void);
bool core_terminal_pixels(unsigned *width, unsigned *height);
#endif
