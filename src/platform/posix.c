#define _GNU_SOURCE
#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static Result failure(void) {
    ResultCode code;
    switch (errno) {
        case ENOENT: code = RESULT_NOT_FOUND; break;
        case EEXIST: code = RESULT_EXISTS; break;
        case EACCES: case EPERM: code = RESULT_ACCESS; break;
        case ENOTDIR: code = RESULT_NOT_DIRECTORY; break;
        case EXDEV: code = RESULT_CROSS_DEVICE; break;
        case ENOMEM: code = RESULT_NO_MEMORY; break;
        case ENOTSUP: code = RESULT_UNSUPPORTED; break;
        default: code = RESULT_IO; break;
    }
    return result_make(code, strerror(errno));
}
static Result oom(void) { return result_make(RESULT_NO_MEMORY, "Out of memory"); }
char *platform_path_join(const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name);
    bool separator = a && dir[a - 1] != '/';
    if (a > SIZE_MAX - b - 2) return NULL;
    char *path = malloc(a + b + 2);
    if (path) snprintf(path, a + b + 2, "%s%s%s", dir, separator ? "/" : "", name);
    return path;
}
char *platform_path_parent(const char *path) {
    char *out = text_copy(path); if (!out) return NULL;
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') out[--n] = 0;
    char *slash = strrchr(out, '/');
    if (!slash) { free(out); return text_copy("."); }
    if (slash == out) out[1] = 0; else *slash = 0;
    return out;
}
char *platform_path_name(const char *path) {
    char *copy = text_copy(path); if (!copy) return NULL;
    size_t n = strlen(copy);
    while (n > 1 && copy[n - 1] == '/') copy[--n] = 0;
    const char *slash = strrchr(copy, '/');
    char *name = text_copy(slash && slash[1] ? slash + 1 : copy);
    free(copy); return name;
}
char *platform_path_relative(const char *root, const char *path) {
    size_t n = strlen(root);
    if (!strcmp(root, "/") && *path == '/') return text_copy(path + 1);
    if (!strncmp(root, path, n) && path[n] == '/') return text_copy(path + n + 1);
    return text_copy(path);
}
bool platform_name_valid(const char *name) {
    return *name && !strchr(name, '/') && strcmp(name, ".") && strcmp(name, "..");
}
Result platform_resolve(const char *base, const char *input, char **out) {
    *out = NULL;
    char *candidate = base && input[0] != '/' ? platform_path_join(base, input) : text_copy(input);
    if (!candidate) return oom();
    *out = realpath(candidate, NULL); free(candidate);
    return *out ? result_make(RESULT_OK, NULL) : failure();
}
Result platform_descendant(const char *source, const char *directory, bool *out) {
    char *a = NULL, *b = NULL; *out = false;
    Result r = platform_resolve(NULL, source, &a);
    if (r.code == RESULT_OK) r = platform_resolve(NULL, directory, &b);
    if (r.code == RESULT_OK) {
        size_t n = strlen(a);
        *out = !strcmp(a, "/") || (!strncmp(a, b, n) && (b[n] == '/' || b[n] == 0));
    }
    free(a); free(b); return r;
}
Result platform_info(const char *path, FileInfo *out) {
    *out = (FileInfo){0};
    struct stat st;
    if (lstat(path, &st) < 0) return failure();
    out->name = platform_path_name(path); out->path = text_copy(path);
    if (!out->name || !out->path) { file_info_free(out); return oom(); }
    out->valid = true;
    out->kind = S_ISDIR(st.st_mode) ? FILE_DIRECTORY : S_ISLNK(st.st_mode) ? FILE_LINK : S_ISREG(st.st_mode) ? FILE_REGULAR : FILE_OTHER;
    out->hidden = out->name[0] == '.';
    out->executable = S_ISREG(st.st_mode) && (st.st_mode & 0111);
    out->has_posix_mode = true; out->posix_mode = st.st_mode & 07777;
    out->size = st.st_size < 0 ? 0 : (uint64_t)st.st_size;
    out->modified = (int64_t)st.st_mtime;
    out->directory_target = out->kind == FILE_DIRECTORY || (out->kind == FILE_LINK && stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    return result_make(RESULT_OK, NULL);
}
struct PlatformDirectory { DIR *handle; char *path; };
Result platform_directory_open(const char *path, PlatformDirectory **out) {
    *out = NULL; DIR *handle = opendir(path); if (!handle) return failure();
    PlatformDirectory *dir = calloc(1, sizeof *dir);
    if (!dir) { closedir(handle); return oom(); }
    dir->handle = handle; dir->path = text_copy(path);
    if (!dir->path) { platform_directory_close(dir); return oom(); }
    *out = dir; return result_make(RESULT_OK, NULL);
}
Result platform_directory_next(PlatformDirectory *dir, FileInfo *out, bool *end, Result *metadata_error) {
    *out = (FileInfo){0}; *end = false;
    if (metadata_error) *metadata_error = result_make(RESULT_OK, NULL);
    for (;;) {
        errno = 0; struct dirent *entry = readdir(dir->handle);
        if (!entry) { *end = true; return errno ? failure() : result_make(RESULT_OK, NULL); }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char *path = platform_path_join(dir->path, entry->d_name);
        if (!path) return oom();
        Result r = platform_info(path, out);
        if (r.code != RESULT_OK && r.code != RESULT_NO_MEMORY) {
            if (metadata_error) *metadata_error = r;
            /* Racy/unreadable metadata still leaves a selectable directory entry. */
            out->path = text_copy(path); out->name = text_copy(entry->d_name);
            out->kind = FILE_OTHER; out->hidden = entry->d_name[0] == '.';
            if (!out->path || !out->name) { file_info_free(out); r = oom(); }
            else r = result_make(RESULT_OK, NULL);
        }
        free(path); return r;
    }
}
void platform_directory_close(PlatformDirectory *dir) {
    if (dir) { if (dir->handle) closedir(dir->handle); free(dir->path); free(dir); }
}
Result platform_create(const char *path, bool directory) {
    if (directory) return mkdir(path, 0777) < 0 ? failure() : result_make(RESULT_OK, NULL);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return failure();
    return close(fd) < 0 ? failure() : result_make(RESULT_OK, NULL);
}
Result platform_link_target(const char *path, char **out) {
    *out = NULL;
    for (size_t size = 256; size <= SIZE_MAX / 2; size *= 2) {
        char *text = malloc(size + 1); if (!text) return oom();
        ssize_t n = readlink(path, text, size);
        if (n < 0) { free(text); return failure(); }
        if ((size_t)n < size) { text[n] = 0; *out = text; return result_make(RESULT_OK, NULL); }
        free(text);
    }
    return oom();
}
/* Recursive file operations deliberately do not use the browsing API: browsing
   follows directory links, whereas these operations never traverse a link. */
#define OP_MAX_DEPTH 64
#ifdef TFILE_TEST_HOOKS
extern void platform_test_hook(const char *stage, int parent, const char *name);
#define OP_HOOK(stage, parent, name) platform_test_hook(stage, parent, name)
#else
#define OP_HOOK(stage, parent, name) ((void)0)
#endif

typedef struct {
    Result error;
    OperationCallback callback;
    void *context;
    bool changed;
    mode_t mask;
    char *buffer; /* Reused file buffer; never allocated on recursive frames. */
} Operation;

static void diagnostic_path(char *out, size_t size, const char *path) {
    size_t n = strlen(path);
    if (n < size) memcpy(out, path, n + 1);
    else snprintf(out, size, "...%s", path + n - (size - 4));
}
static bool op_error(Operation *op, const char *path, ResultCode code, const char *detail) {
    if (op->error.code == RESULT_OK) {
        op->error.code = code;
        snprintf(op->error.detail, sizeof op->error.detail, "%s", detail);
        diagnostic_path(op->error.path, sizeof op->error.path, path);
    }
    return false;
}
static bool op_poll(Operation *op, const char *path) {
    if (!op->callback) return true;
    OperationProgress progress = {path, op->error.completed_items, op->error.copied_bytes};
    return op->callback(&progress, op->context) ||
        op_error(op, path, RESULT_CANCELLED, "Cancelled by user");
}
static bool op_complete(Operation *op, bool ok) {
    if (ok) op->error.completed_items++;
    return ok;
}
static bool op_errno(Operation *op, const char *path) {
    Result r = failure(); return op_error(op, path, r.code, r.detail);
}
static bool op_changed(Operation *op, const char *path) {
    return op_error(op, path, RESULT_IO, "Entry changed during operation");
}
static bool same_entry(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
           (a->st_mode & S_IFMT) == (b->st_mode & S_IFMT);
}
static bool verify_name(Operation *op, int parent, const char *name,
                        const struct stat *expected, const char *path) {
    struct stat now;
    if (fstatat(parent, name, &now, AT_SYMLINK_NOFOLLOW) < 0) return op_errno(op, path);
    return same_entry(expected, &now) || op_changed(op, path);
}
/* Every ancestor is opened separately, with no symlink traversal. Once opened,
   renaming/replacing an ancestor cannot redirect descendant accesses. */
static int operation_parent(Operation *op, const char *path, char **name) {
    *name = NULL;
    char *copy = text_copy(path);
    if (!copy) { op_error(op, path, RESULT_NO_MEMORY, "Out of memory"); return -1; }
    size_t len = strlen(copy);
    while (len > 1 && copy[len - 1] == '/') copy[--len] = 0;
    char *last = strrchr(copy, '/');
    const char *leaf = last ? last + 1 : copy;
    if (!platform_name_valid(leaf)) {
        free(copy); op_error(op, path, RESULT_INVALID_NAME, "Invalid operation target"); return -1;
    }
    *name = text_copy(leaf);
    if (!*name) { free(copy); op_error(op, path, RESULT_NO_MEMORY, "Out of memory"); return -1; }
    int fd = open(copy[0] == '/' ? "/" : ".", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) { op_errno(op, path); free(copy); return -1; }
    if (last) {
        *last = 0;
        char *save = NULL;
        for (char *part = strtok_r(copy, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
            int next = openat(fd, part, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) { op_errno(op, path); close(fd); free(copy); return -1; }
            close(fd); fd = next;
        }
    }
    free(copy); return fd;
}
static int open_verified(Operation *op, int parent, const char *name,
                         const struct stat *expected, const char *path, int flags) {
    int fd = openat(parent, name, flags | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { op_errno(op, path); return -1; }
    struct stat st;
    if (fstat(fd, &st) < 0) { op_errno(op, path); close(fd); return -1; }
    if (!same_entry(expected, &st)) { op_changed(op, path); close(fd); return -1; }
    return fd;
}
/* Check the actual pinned destination parent, not a second pathname lookup. */
static bool outside_source(Operation *op, const struct stat *source, int parent, const char *path) {
    int fd = dup(parent);
    if (fd < 0) return op_errno(op, path);
    bool ok = false;
    for (unsigned i = 0; i < 4096; ++i) {
        struct stat here, up;
        if (fstat(fd, &here) < 0) { op_errno(op, path); break; }
        if (same_entry(source, &here)) {
            op_error(op, path, RESULT_SELF_TRANSFER, "Cannot copy a directory into itself"); break;
        }
        int next = openat(fd, "..", O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) { op_errno(op, path); break; }
        if (fstat(next, &up) < 0) { op_errno(op, path); close(next); break; }
        if (same_entry(&here, &up)) { close(next); ok = true; break; }
        close(fd); fd = next;
    }
    close(fd);
    if (!ok && op->error.code == RESULT_OK)
        op_error(op, path, RESULT_IO, "Destination ancestry limit reached");
    return ok;
}
static bool copy_file_at(Operation *op, int sp, const char *sn, int dp, const char *dn,
                         const struct stat *st, const char *src, const char *dst) {
    int in = open_verified(op, sp, sn, st, src, O_RDONLY | O_NONBLOCK);
    if (in < 0) return false;
    if (!op->buffer) op->buffer = malloc(65536);
    if (!op->buffer) { op_error(op, src, RESULT_NO_MEMORY, "Out of memory"); close(in); return false; }
    /* EXCL also rejects a dangling destination symlink. Failed copies stay in
       place: unlinking by name during cleanup could delete someone else's file. */
    int out = openat(dp, dn, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (out < 0) { op_errno(op, dst); close(in); return false; }
    op->changed = true;
    struct stat created;
    bool ok = fstat(out, &created) == 0;
    if (!ok) op_errno(op, dst);
    OP_HOOK("copy-file-created", dp, dn);
    while (ok) {
        if (!op_poll(op, src)) { ok = false; break; }
        ssize_t n = read(in, op->buffer, 65536);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = op_errno(op, src); break; }
        if (!n) break;
        for (ssize_t at = 0; at < n;) {
            if (!op_poll(op, src)) { ok = false; break; }
            ssize_t written = write(out, op->buffer + at, (size_t)(n - at));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { if (!written) errno = EIO; ok = op_errno(op, dst); break; }
            at += written; op->error.copied_bytes += (uint64_t)written;
        }
    }
    if (ok && fchmod(out, st->st_mode & 0777 & ~op->mask) < 0) ok = op_errno(op, dst);
    if (ok) ok = verify_name(op, dp, dn, &created, dst);
    if (close(out) < 0 && ok) ok = op_errno(op, dst);
    if (close(in) < 0 && ok) ok = op_errno(op, src);
    return ok;
}
static bool copy_link_at(Operation *op, int sp, const char *sn, int dp, const char *dn,
                         const struct stat *st, const char *src, const char *dst) {
    for (size_t size = 256; size <= SIZE_MAX / 2; size *= 2) {
        char *target = malloc(size + 1);
        if (!target) return op_error(op, src, RESULT_NO_MEMORY, "Out of memory");
        ssize_t n = readlinkat(sp, sn, target, size);
        if (n < 0) { op_errno(op, src); free(target); return false; }
        if ((size_t)n == size) { free(target); continue; }
        target[n] = 0;
        bool ok = verify_name(op, sp, sn, st, src);
        if (ok && symlinkat(target, dp, dn) < 0) ok = op_errno(op, dst);
        else if (ok) op->changed = true;
        free(target); return ok;
    }
    return op_error(op, src, RESULT_NO_MEMORY, "Out of memory");
}
static bool copy_at(Operation *op, int sp, const char *sn, int dp, const char *dn,
                    const char *src, const char *dst, unsigned depth) {
    if (!op_poll(op, src)) return false;
    if (depth >= OP_MAX_DEPTH)
        return op_error(op, src, RESULT_IO, "Recursive operation depth limit (64) reached");
    struct stat st;
    if (fstatat(sp, sn, &st, AT_SYMLINK_NOFOLLOW) < 0) return op_errno(op, src);
    OP_HOOK("copy-stat", sp, sn);
    if (S_ISLNK(st.st_mode)) return op_complete(op, copy_link_at(op, sp, sn, dp, dn, &st, src, dst));
    if (S_ISREG(st.st_mode)) return op_complete(op, copy_file_at(op, sp, sn, dp, dn, &st, src, dst));
    if (!S_ISDIR(st.st_mode)) return op_error(op, src, RESULT_UNSUPPORTED, "Unsupported file type");
    int in = open_verified(op, sp, sn, &st, src, O_RDONLY | O_DIRECTORY);
    if (in < 0) return false;
    if (!outside_source(op, &st, dp, dst)) { close(in); return false; }
    DIR *dir = fdopendir(in);
    if (!dir) { op_errno(op, src); close(in); return false; }
    OP_HOOK("copy-open", sp, sn);
    /* This single-threaded backend temporarily clears umask only for mkdir.
       Private owner access is needed even for umask 0777 and source mode 0555. */
    mode_t saved_mask = umask(0);
    int made = mkdirat(dp, dn, 0700), saved_errno = errno;
    umask(saved_mask); errno = saved_errno;
    if (made < 0) { op_errno(op, dst); closedir(dir); return false; }
    op->changed = true;
    struct stat created;
    if (fstatat(dp, dn, &created, AT_SYMLINK_NOFOLLOW) < 0) {
        op_errno(op, dst); closedir(dir); return false;
    }
    OP_HOOK("copy-created", dp, dn);
    int out = open_verified(op, dp, dn, &created, dst, O_RDONLY | O_DIRECTORY);
    if (out < 0) { closedir(dir); return false; }
    OP_HOOK("copy-destination-open", dp, dn);
    bool ok = true;
    for (;;) {
        errno = 0; struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) ok = op_errno(op, src); break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char *a = platform_path_join(src, entry->d_name), *b = platform_path_join(dst, entry->d_name);
        if (!a || !b) ok = op_error(op, !a ? src : dst, RESULT_NO_MEMORY, "Out of memory");
        else ok = copy_at(op, in, entry->d_name, out, entry->d_name, a, b, depth + 1);
        free(a); free(b);
        if (!ok) break;
    }
    if (ok) ok = op_poll(op, src);
    if (closedir(dir) < 0 && ok) ok = op_errno(op, src);
    if (ok) ok = verify_name(op, sp, sn, &st, src);
    if (ok) ok = verify_name(op, dp, dn, &created, dst);
    if (ok && fchmod(out, st.st_mode & 0777 & ~op->mask) < 0) ok = op_errno(op, dst);
    if (close(out) < 0 && ok) ok = op_errno(op, dst);
    return op_complete(op, ok);
}
static bool remove_at(Operation *op, int parent, const char *name, const char *path, unsigned depth) {
    if (!op_poll(op, path)) return false;
    if (depth >= OP_MAX_DEPTH)
        return op_error(op, path, RESULT_IO, "Recursive operation depth limit (64) reached");
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) < 0) return op_errno(op, path);
    OP_HOOK("remove-stat", parent, name);
    if (S_ISDIR(st.st_mode)) {
        int fd = open_verified(op, parent, name, &st, path, O_RDONLY | O_DIRECTORY);
        if (fd < 0) return false;
        DIR *dir = fdopendir(fd);
        if (!dir) { op_errno(op, path); close(fd); return false; }
        OP_HOOK("remove-open", parent, name);
        bool ok = true;
        for (;;) {
            errno = 0; struct dirent *entry = readdir(dir);
            if (!entry) { if (errno) ok = op_errno(op, path); break; }
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char *child = platform_path_join(path, entry->d_name);
            ok = child ? remove_at(op, fd, entry->d_name, child, depth + 1) : op_error(op, path, RESULT_NO_MEMORY, "Out of memory");
            free(child);
            if (!ok) break;
        }
        if (closedir(dir) < 0 && ok) ok = op_errno(op, path);
        if (!ok) return false;
    }
    if (!op_poll(op, path)) return false;
    OP_HOOK("remove-unlink", parent, name);
    if (!verify_name(op, parent, name, &st, path)) return false;
    if (unlinkat(parent, name, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) < 0) return op_errno(op, path);
    op->changed = true; return op_complete(op, true);
}
static Result operation_result(Operation *op) {
    if (op->error.code != RESULT_OK) {
        char reason[80], path[144];
        snprintf(reason, sizeof reason, "%.79s", op->error.detail);
        diagnostic_path(path, sizeof path, op->error.path);
        op->error.partial = op->changed;
        snprintf(op->error.detail, sizeof op->error.detail, "%s: %s: %s",
                 op->changed ? "Partial operation" : "No changes", reason, path);
    }
    return op->error;
}
Result platform_copy(const char *src, const char *dst) {
    return platform_copy_progress(src, dst, NULL, NULL);
}
Result platform_copy_progress(const char *src, const char *dst, OperationCallback callback, void *context) {
    Operation op = {.callback = callback, .context = context};
    if (!op_poll(&op, src)) return operation_result(&op);
    op.mask = umask(0); umask(op.mask);
    char *sn = NULL, *dn = NULL;
    int sp = operation_parent(&op, src, &sn), dp = -1;
    if (sp >= 0) dp = operation_parent(&op, dst, &dn);
    if (dp >= 0) copy_at(&op, sp, sn, dp, dn, src, dst, 0);
    if (sp >= 0) close(sp);
    if (dp >= 0) close(dp);
    free(sn); free(dn); free(op.buffer); return operation_result(&op);
}
Result platform_remove(const char *path) {
    return platform_remove_progress(path, NULL, NULL);
}
Result platform_remove_progress(const char *path, OperationCallback callback, void *context) {
    Operation op = {.callback = callback, .context = context}; char *name = NULL;
    if (!op_poll(&op, path)) return operation_result(&op);
    int parent = operation_parent(&op, path, &name);
    if (parent >= 0) { remove_at(&op, parent, name, path, 0); close(parent); }
    free(name); return operation_result(&op);
}
Result platform_move(const char *src, const char *dst) {
    /* Linux/WSL backend: guarantee no replacement even if a target appears after validation. */
    return renameat2(AT_FDCWD, src, AT_FDCWD, dst, RENAME_NOREPLACE) < 0 ? failure() : result_make(RESULT_OK, NULL);
}
struct PlatformReader { FILE *handle; struct stat version; };
Result platform_reader_open(const char *path, PlatformReader **out) {
    *out = NULL;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return failure();
    struct stat st;
    if (fstat(fd, &st) < 0) { Result r = failure(); close(fd); return r; }
    if (!S_ISREG(st.st_mode)) { close(fd); return result_make(RESULT_UNSUPPORTED, "Not a regular file"); }
    FILE *f = fdopen(fd, "rb");
    if (!f) { Result r = failure(); close(fd); return r; }
    PlatformReader *reader = malloc(sizeof *reader);
    if (!reader) { fclose(f); return oom(); }
    reader->handle = f; reader->version = st; *out = reader; return result_make(RESULT_OK, NULL);
}
static bool same_version(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
Result platform_reader_changed(PlatformReader *reader, const char *path, bool *changed) {
    struct stat current, held; *changed = false;
    if (lstat(path, &current) < 0 || fstat(fileno(reader->handle), &held) < 0) return failure();
    *changed = !same_version(&reader->version, &current) || !same_version(&reader->version, &held);
    return result_make(RESULT_OK, NULL);
}
Result platform_reader_peek(PlatformReader *reader, unsigned char *buffer, size_t capacity, size_t *read_count) {
    *read_count = 0;
    fpos_t position;
    if (fgetpos(reader->handle, &position) != 0) return failure();
    *read_count = fread(buffer, 1, capacity, reader->handle);
    Result r = ferror(reader->handle) ? failure() : result_make(RESULT_OK, NULL);
    if (fsetpos(reader->handle, &position) != 0 && r.code == RESULT_OK) r = failure();
    return r;
}
Result platform_reader_line(PlatformReader *reader, char *buffer, size_t size, bool *end) {
    *end = false;
    if (!fgets(buffer, (int)size, reader->handle)) {
        *end = true; if (ferror(reader->handle)) return failure();
    }
    return result_make(RESULT_OK, NULL);
}
void platform_reader_close(PlatformReader *reader) {
    if (reader) { fclose(reader->handle); free(reader); }
}
uint64_t platform_monotonic_ms(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}
