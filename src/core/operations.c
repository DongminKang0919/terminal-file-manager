#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
Result core_create(const char *directory, const char *name, bool is_directory) {
    if (!platform_name_valid(name)) return result_make(RESULT_INVALID_NAME, NULL);
    char *path = platform_path_join(directory, name);
    if (!path) return result_make(RESULT_NO_MEMORY, "Out of memory");
    Result r = platform_create(path, is_directory); free(path); return r;
}
Result core_delete_progress(const char *path, OperationCallback callback, void *context) {
    return platform_remove_progress(path, callback, context);
}
Result core_delete(const char *path) { return core_delete_progress(path, NULL, NULL); }
Result core_transfer(bool move, const char *source, const char *directory, const char *name, char **destination) {
    return core_transfer_progress(move, source, directory, name, destination, NULL, NULL);
}
Result core_transfer_progress(bool move, const char *source, const char *directory, const char *name,
                              char **destination, OperationCallback callback, void *context) {
    *destination = NULL;
    if (!*source) return result_make(RESULT_NOT_FOUND, "Source is empty");
    if (!platform_name_valid(name)) return result_make(RESULT_INVALID_NAME, NULL);
    char *resolved = NULL;
    Result r = core_resolve_directory(NULL, directory, &resolved);
    if (r.code != RESULT_OK) return r;
    char *dst = platform_path_join(resolved, name);
    if (!dst) { free(resolved); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
    FileInfo info;
    r = platform_info(dst, &info);
    if (r.code == RESULT_OK) { file_info_free(&info); r = result_make(RESULT_EXISTS, NULL); goto done; }
    if (r.code != RESULT_NOT_FOUND) goto done;
    r = platform_info(source, &info);
    if (r.code != RESULT_OK) goto done;
    bool directory_source = info.kind == FILE_DIRECTORY; file_info_free(&info);
    if (directory_source) {
        bool descendant;
        r = platform_descendant(source, resolved, &descendant);
        if (r.code != RESULT_OK) goto done;
        if (descendant) { r = result_make(RESULT_SELF_TRANSFER, NULL); goto done; }
    }
    r = move ? platform_move(source, dst) : platform_copy_progress(source, dst, callback, context);
    if (r.code == RESULT_OK) { *destination = dst; dst = NULL; }
done:
    free(resolved); free(dst); return r;
}
