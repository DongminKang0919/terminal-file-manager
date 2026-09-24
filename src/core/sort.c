#include "core.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int name_compare(const char *a, const char *b) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (*x && *y && tolower(*x) == tolower(*y)) { x++; y++; }
    int folded = (tolower(*x) > tolower(*y)) - (tolower(*x) < tolower(*y));
    if (folded) return folded;
    int raw = strcmp(a, b);
    return (raw > 0) - (raw < 0);
}
int main_file_compare(const FileInfo *a, const FileInfo *b, SortSettings sort) {
    /* Missing metadata is last, even if a synthetic invalid entry has a kind.
       Links to directories remain FILE_LINK, matching the legacy grouping. */
    if (sort.key != SORT_NAME && a->valid != b->valid) return a->valid ? -1 : 1;
    if (sort.key == SORT_NAME || (a->valid && b->valid)) {
        bool ad = a->kind == FILE_DIRECTORY, bd = b->kind == FILE_DIRECTORY;
        if (ad != bd) return ad ? -1 : 1;
    }
    int primary = 0;
    if (sort.key == SORT_NAME) primary = name_compare(a->name, b->name);
    else if (a->valid && b->valid) {
        switch (sort.key) {
            case SORT_SIZE: primary = (a->size > b->size) - (a->size < b->size); break;
            case SORT_MODIFIED: primary = (a->modified > b->modified) - (a->modified < b->modified); break;
            case SORT_KIND: primary = (a->kind > b->kind) - (a->kind < b->kind); break;
            default: break;
        }
    }
    if (primary) return sort.descending ? -primary : primary;
    return name_compare(a->name, b->name);
}
/* Eight fixed adapters keep ISO C qsort, with no mutable global context or
   qsort_r ABI. The policy lives in one comparator; adapters only pass values. */
#define ADAPTER(name, key, descending) \
    static int name(const void *a, const void *b) { \
        return main_file_compare(a, b, (SortSettings){key, descending}); \
    }
ADAPTER(name_up, SORT_NAME, false)
ADAPTER(name_down, SORT_NAME, true)
ADAPTER(size_up, SORT_SIZE, false)
ADAPTER(size_down, SORT_SIZE, true)
ADAPTER(time_up, SORT_MODIFIED, false)
ADAPTER(time_down, SORT_MODIFIED, true)
ADAPTER(kind_up, SORT_KIND, false)
ADAPTER(kind_down, SORT_KIND, true)
#undef ADAPTER
void main_list_sort(FileList *list, SortSettings sort) {
    static int (*const comparisons[4][2])(const void *, const void *) = {
        {name_up, name_down}, {size_up, size_down}, {time_up, time_down}, {kind_up, kind_down}
    };
    if (sort.key < SORT_NAME || sort.key > SORT_KIND || list->len < 2) return;
    qsort(list->entries, list->len, sizeof *list->entries, comparisons[sort.key][sort.descending]);
}
void app_set_sort(AppState *app, SortSettings sort, size_t *selected) {
    if (sort.key < SORT_NAME || sort.key > SORT_KIND) return;
    /* Strings retain their allocation and lifetime while only structs move.
       Do not retain a FileInfo pointer across qsort. No allocation or I/O. */
    const char *path = *selected < app->files.len ? app->files.entries[*selected].path : NULL;
    app->sort = sort; main_list_sort(&app->files, sort);
    *selected = 0;
    if (path) for (size_t i = 0; i < app->files.len; i++)
        if (!strcmp(app->files.entries[i].path, path)) { *selected = i; break; }
}
