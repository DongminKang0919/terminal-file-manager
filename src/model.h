#ifndef TFILE_MODEL_H
#define TFILE_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { FILE_REGULAR, FILE_DIRECTORY, FILE_LINK, FILE_OTHER } FileKind;
typedef struct {
    char *name, *path; /* Owned strings; release through file_list_free. */
    FileKind kind;
    bool valid, hidden, executable, directory_target, has_posix_mode;
    uint64_t size;
    int64_t modified;
    unsigned posix_mode;
} FileInfo;
typedef struct { FileInfo *entries; size_t len; } FileList;
typedef enum {
    RESULT_OK, RESULT_NOT_FOUND, RESULT_EXISTS, RESULT_ACCESS, RESULT_INVALID_NAME,
    RESULT_NOT_DIRECTORY, RESULT_SELF_TRANSFER, RESULT_CROSS_DEVICE,
    RESULT_NO_MEMORY, RESULT_IO, RESULT_UNSUPPORTED
} ResultCode;
typedef struct {
    ResultCode code;
    char detail[256];
    /* Recursive operation errors: tail-preserving diagnostic path, and whether
       this operation already changed the filesystem. No rollback is implied. */
    char path[1024];
    bool partial;
} Result;
Result result_make(ResultCode code, const char *detail);
char *text_copy(const char *text); /* Caller frees returned allocation. */
void file_info_free(FileInfo *info);
void file_list_free(FileList *list);
#endif
