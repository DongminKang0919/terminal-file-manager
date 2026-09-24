#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int file_compare(const void *a, const void *b) {
    const FileInfo *x = a, *y = b;
    bool xd = x->kind == FILE_DIRECTORY, yd = y->kind == FILE_DIRECTORY;
    if (xd != yd) return yd - xd;
    const unsigned char *s = (const unsigned char *)x->name, *t = (const unsigned char *)y->name;
    while (*s && *t && tolower(*s) == tolower(*t)) { s++; t++; }
    return tolower(*s) - tolower(*t);
}
bool text_contains(const char *text, const char *needle) {
    if (!*needle) return true;
    for (; *text; text++) {
        const char *a = text, *b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}
Result core_list(const char *directory, bool hidden, bool directories_only, FileList *out) {
    *out = (FileList){0}; PlatformDirectory *dir = NULL;
    Result r = platform_directory_open(directory, &dir);
    size_t capacity = 0;
    while (r.code == RESULT_OK) {
        FileInfo entry; bool end;
        r = platform_directory_next(dir, &entry, &end, NULL);
        if (r.code != RESULT_OK || end) break;
        if ((!hidden && entry.hidden) || (directories_only && !entry.directory_target)) { file_info_free(&entry); continue; }
        if (out->len == capacity) {
            size_t next = capacity ? capacity * 2 : 64;
            FileInfo *entries = realloc(out->entries, next * sizeof *entries);
            if (!entries) { file_info_free(&entry); r = result_make(RESULT_NO_MEMORY, "Out of memory"); break; }
            out->entries = entries; capacity = next;
        }
        out->entries[out->len++] = entry;
    }
    platform_directory_close(dir);
    if (r.code != RESULT_OK) file_list_free(out);
    else if (out->len > 1) qsort(out->entries, out->len, sizeof *out->entries, file_compare);
    return r;
}
Result core_resolve_directory(const char *base, const char *input, char **out) {
    *out = NULL; char *resolved = NULL;
    Result r = platform_resolve(base, input, &resolved);
    if (r.code != RESULT_OK) return r;
    PlatformDirectory *dir = NULL;
    r = platform_directory_open(resolved, &dir); platform_directory_close(dir);
    if (r.code == RESULT_OK) *out = resolved; else free(resolved);
    return r;
}
static Result navigate_to(AppState *app, const char *directory, bool record, bool hidden, const char *required_name, size_t *selected) {
    char *resolved = NULL;
    Result r = core_resolve_directory(app->directory, directory, &resolved);
    if (r.code != RESULT_OK) return r;
    FileList list;
    r = core_list(resolved, hidden, false, &list);
    if (r.code != RESULT_OK) { free(resolved); return r; }
    main_list_sort(&list, app->sort);
    size_t choice = 0;
    if (required_name) {
        for (; choice < list.len; ++choice)
            if (!strcmp(list.entries[choice].name, required_name) && list.entries[choice].valid) break;
        if (choice == list.len) {
            free(resolved); file_list_free(&list);
            return result_make(RESULT_NOT_FOUND, "Search result is missing or unreadable");
        }
    }
    if (record && (!app->directory || strcmp(app->directory, resolved))) {
        char *entry = text_copy(resolved);
        size_t length = app->history_len ? app->history_at + 1 : 0;
        char **history = entry ? realloc(app->history, (length + 1 > app->history_len ? length + 1 : app->history_len) * sizeof *history) : NULL;
        if (!history) { free(entry); free(resolved); file_list_free(&list); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
        app->history = history;
        for (size_t i = length; i < app->history_len; i++) free(history[i]);
        if (length == 128) { free(history[0]); memmove(history, history + 1, (--length) * sizeof *history); }
        history[length] = entry; app->history_at = length; app->history_len = length + 1;
    }
    free(app->directory); file_list_free(&app->files);
    app->directory = resolved; app->files = list; app->show_hidden = hidden;
    if (selected) *selected = choice;
    return r;
}
Result app_navigate(AppState *app, const char *directory) { return navigate_to(app, directory, true, app->show_hidden, NULL, NULL); }
Result app_init(AppState *app, const char *directory) {
    *app = (AppState){ .show_hidden = true, .show_preview = true, .wheel_step = 1 };
    return app_navigate(app, directory ? directory : ".");
}
void app_free(AppState *app) {
    free(app->directory); file_list_free(&app->files);
    for (size_t i = 0; i < app->history_len; i++) free(app->history[i]);
    free(app->history); *app = (AppState){0};
}
Result app_refresh(AppState *app) { return navigate_to(app, app->directory, false, app->show_hidden, NULL, NULL); }
char *core_path_join(const char *directory, const char *name) { return platform_path_join(directory, name); }
char *core_path_parent(const char *path) { return platform_path_parent(path); }
char *core_path_name(const char *path) { return platform_path_name(path); }
Result core_info(const char *path, FileInfo *out) { return platform_info(path, out); }
uint64_t core_monotonic_ms(void) { return platform_monotonic_ms(); }
Result core_link_target(const char *path, char **out) { return platform_link_target(path, out); }

Result app_history(AppState *app, bool forward) {
    if (!app->history_len || (forward ? app->history_at + 1 >= app->history_len : app->history_at == 0))
        return result_make(RESULT_NOT_FOUND, forward ? "No forward history" : "No back history");
    size_t next = forward ? app->history_at + 1 : app->history_at - 1;
    Result r = navigate_to(app, app->history[next], false, app->show_hidden, NULL, NULL);
    if (r.code == RESULT_OK) app->history_at = next;
    return r;
}

Result app_open_search_result(AppState *app, const char *path, size_t *selected, bool *revealed) {
    *revealed = false;
    FileInfo info;
    Result r = platform_info(path, &info);
    if (r.code != RESULT_OK) return r;
    bool directory = info.kind == FILE_DIRECTORY;
    bool hidden = app->show_hidden || (!directory && info.hidden);
    char *parent = directory ? text_copy(path) : platform_path_parent(path);
    if (!parent) { file_info_free(&info); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
    bool was_hidden = app->show_hidden;
    r = navigate_to(app, parent, true, hidden, directory ? NULL : info.name, selected);
    if (r.code == RESULT_OK) *revealed = !was_hidden && hidden;
    free(parent); file_info_free(&info); return r;
}
