#include "model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
Result result_make(ResultCode code, const char *detail) {
    Result r = { .code = code };
    if (detail) snprintf(r.detail, sizeof r.detail, "%s", detail);
    return r;
}
char *text_copy(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}
void file_info_free(FileInfo *info) {
    free(info->name); free(info->path); *info = (FileInfo){0};
}
void file_list_free(FileList *list) {
    for (size_t i = 0; i < list->len; i++) file_info_free(&list->entries[i]);
    free(list->entries); *list = (FileList){0};
}
