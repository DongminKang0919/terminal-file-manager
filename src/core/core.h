#ifndef TFILE_CORE_H
#define TFILE_CORE_H
#include "../model.h"

typedef struct {
    char *directory;
    FileList files;
    bool show_hidden, show_preview;
    int wheel_step;
    char **history;
    size_t history_len, history_at;
} AppState;
Result app_init(AppState *app, const char *directory);
void app_free(AppState *app);
Result app_navigate(AppState *app, const char *directory);
Result app_refresh(AppState *app);
Result app_history(AppState *app, bool forward);
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
Result core_link_target(const char *path, char **out);

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
    bool binary, more;
} PreviewText;
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
#endif
