#define _GNU_SOURCE
#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <linux/magic.h>
#include <sys/vfs.h>
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
Result platform_directory_empty(const char *path, bool *empty) {
    *empty = false;
    DIR *handle = opendir(path);
    if (!handle) return failure();
    Result r = result_make(RESULT_OK, NULL);
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(handle);
        if (!entry) {
            if (errno) r = failure();
            else *empty = true;
            break;
        }
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) break;
    }
    if (closedir(handle) < 0 && r.code == RESULT_OK) r = failure();
    if (r.code != RESULT_OK) *empty = false;
    return r;
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
    Operation op = {0};
    char *sn = NULL, *dn = NULL;
    int sp = operation_parent(&op, src, &sn), dp = -1;
    if (sp >= 0) dp = operation_parent(&op, dst, &dn);
    Result r = op.error;
    if (dp >= 0) {
        /* Pin both parents; NOREPLACE also covers a late destination leaf.
           The source leaf itself is not atomically bound to earlier metadata. */
        r = renameat2(sp, sn, dp, dn, RENAME_NOREPLACE) < 0 ? failure() : result_make(RESULT_OK, NULL);
    }
    if (sp >= 0) close(sp);
    if (dp >= 0) close(dp);
    free(sn); free(dn);
    return r;
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

Result platform_settings_path(char **out) {
    *out=NULL;
    const char *xdg=getenv("XDG_CONFIG_HOME"), *home=getenv("HOME");
    char *base=NULL;
    if(xdg && xdg[0]=='/') base=text_copy(xdg);
    else if(home && home[0]=='/') base=platform_path_join(home,".config");
    else return result_make(RESULT_ACCESS,"No absolute XDG_CONFIG_HOME or HOME; settings unavailable");
    if(!base) return oom();
    *out=platform_path_join(base,"tfile/settings.conf"); free(base);
    return *out ? result_make(RESULT_OK,NULL) : oom();
}
/* Pin each directory; never traverse symlinks. Existing modes are untouched. */
static int settings_directory(const char *path,bool create) {
    char *parent=platform_path_parent(path);
    if(!parent) { errno=ENOMEM; return -1; }
    int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    char *save=NULL;
    for(char *part=strtok_r(parent,"/",&save);fd>=0 && part;part=strtok_r(NULL,"/",&save)) {
        if(!strcmp(part,"..")) { close(fd); fd=-1; errno=EINVAL; break; }
        int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0 && errno==ENOENT && create) {
            if(mkdirat(fd,part,0700)<0 && errno!=EEXIST) { int e=errno; close(fd); fd=-1; errno=e; break; }
            next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        }
        int e=errno; close(fd); fd=next; errno=e;
    }
    free(parent); return fd;
}
static Result config_read(const char *path,const char *name,char *data,size_t cap,size_t *len) {
    *len=0;
    int dir=settings_directory(path,false);
    if(dir<0) return failure();
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    int e=errno; close(dir); errno=e;
    if(fd<0) return failure();
    struct stat st;
    Result r=result_make(RESULT_OK,NULL);
    if(fstat(fd,&st)<0) r=failure();
    else if(!S_ISREG(st.st_mode)) r=result_make(RESULT_UNSUPPORTED,"Settings must be a regular file");
    else if(st.st_size>(off_t)(cap-1)) r=result_make(RESULT_IO,cap==4097 ? "Settings file exceeds 4096 bytes" : "Favorites file exceeds 65536 bytes");
    while(r.code==RESULT_OK && *len<cap) {
        ssize_t n=read(fd,data+*len,cap-*len);
        if(n<0 && errno==EINTR) continue;
        if(n<0) { r=failure(); break; }
        if(!n) break;
        *len+=(size_t)n;
    }
    if(r.code==RESULT_OK && *len>cap-1) r=result_make(RESULT_IO,cap==4097 ? "Settings file exceeds 4096 bytes" : "Favorites file exceeds 65536 bytes");
    if(close(fd)<0 && r.code==RESULT_OK) r=failure();
    return r;
}
static Result settings_target(int dir,const char *name) {
    struct stat st;
    if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)<0)
        return errno==ENOENT ? result_make(RESULT_OK,NULL) : failure();
    return S_ISREG(st.st_mode) ? result_make(RESULT_OK,NULL) : result_make(RESULT_UNSUPPORTED,"Refusing symlink or special settings file");
}
static Result config_write(const char *path,const char *name,const char *data,size_t len) {
    int dir=settings_directory(path,true);
    if(dir<0) return failure();
    Result r=settings_target(dir,name);
    char temp[80]=""; int fd=-1;
    static unsigned sequence;
    if(r.code==RESULT_OK) {
        for(unsigned attempt=0;attempt<100;attempt++) {
            snprintf(temp,sizeof temp,".settings-%ld-%u.tmp",(long)getpid(),++sequence);
            fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(fd>=0 || errno!=EEXIST) break;
        }
        if(fd<0) { temp[0]=0; r=failure(); }
    }
    size_t at=0;
    while(r.code==RESULT_OK && at<len) {
        ssize_t n=write(fd,data+at,len-at);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) { if(!n) errno=EIO; r=failure(); break; }
        at+=(size_t)n;
    }
    if(fd>=0) {
        if(r.code==RESULT_OK && fsync(fd)<0) r=failure();
        if(close(fd)<0 && r.code==RESULT_OK) r=failure();
    }
    if(r.code==RESULT_OK) r=settings_target(dir,name);
    if(r.code==RESULT_OK && renameat(dir,temp,dir,name)<0) r=failure();
    if(r.code!=RESULT_OK && temp[0]) unlinkat(dir,temp,0);
    close(dir); return r;
}

Result platform_settings_read(const char *p,char *d,size_t c,size_t *n) { return config_read(p,"settings.conf",d,c,n); }
Result platform_settings_write(const char *p,const char *d,size_t n) { return config_write(p,"settings.conf",d,n); }
Result platform_favorites_path(char **out) {
    *out=NULL; char *settings=NULL;
    Result r=platform_settings_path(&settings);
    if(r.code!=RESULT_OK) return r;
    char *parent=platform_path_parent(settings); free(settings);
    if(!parent) return oom();
    *out=platform_path_join(parent,"favorites.conf"); free(parent);
    return *out ? result_make(RESULT_OK,NULL) : oom();
}
Result platform_favorites_read(const char *p,char *d,size_t c,size_t *n) { return config_read(p,"favorites.conf",d,c,n); }
Result platform_favorites_write(const char *p,const char *d,size_t n) { return config_write(p,"favorites.conf",d,n); }
bool platform_path_absolute(const char *p) { return p && p[0]=='/'; }

bool platform_name_glob(const char *name,const char *pattern) { return fnmatch(pattern,name,0)==0; }

/* Trash keeps payload moves atomic on one filesystem and records recovery
   metadata first. It never falls back to copy/delete or permanent deletion. */
#ifdef TFILE_TRASH_TEST_HOOKS
extern int platform_trash_test_top(int source_parent,char **path);
extern void platform_trash_test_hook(const char *stage,int parent,const char *name);
#define TRASH_HOOK(stage,parent,name) platform_trash_test_hook(stage,parent,name)
#else
#define TRASH_HOOK(stage,parent,name) ((void)0)
#endif
typedef struct { int root,files,info; char *path; bool home; } TrashLocation;
static TrashLocation trash_empty(void) { return (TrashLocation){.root=-1,.files=-1,.info=-1}; }
static void trash_location_free(TrashLocation *t) {
    if(t->info>=0) close(t->info);
    if(t->files>=0) close(t->files);
    if(t->root>=0) close(t->root);
    free(t->path);*t=trash_empty();
}
static bool trash_private(Operation *op,int fd,const char *path) {
    struct stat st;
    if(fstat(fd,&st)<0) return op_errno(op,path);
    return (S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&(st.st_mode&07777)==0700) ||
        op_error(op,path,RESULT_ACCESS,"Trash directories must be user-owned mode 0700, without links");
}
/* No threads: bound the process-wide mask change to this one mkdir syscall.
   Existing directories are never chmod'ed. Restore even on EEXIST/error. */
static int trash_mkdir(int parent,const char *name) {
    mode_t previous=umask(0);
    int result=mkdirat(parent,name,0700),saved=errno;
    umask(previous);errno=saved;return result;
}
static int trash_child(Operation *op,int parent,const char *name,const struct stat *source,const char *path) {
    if(S_ISDIR(source->st_mode)&&!outside_source(op,source,parent,path)) return -1;
    if(trash_mkdir(parent,name)<0&&errno!=EEXIST) {op_errno(op,path);return -1;}
    int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0) {op_errno(op,path);return -1;}
    if(!trash_private(op,fd,path) || (S_ISDIR(source->st_mode)&&!outside_source(op,source,fd,path))) {close(fd);return -1;}
    return fd;
}
static int trash_home_root(Operation *op,const char *path,const struct stat *source) {
    char *copy=text_copy(path);if(!copy) {op_error(op,path,RESULT_NO_MEMORY,"Out of memory");return -1;}
    int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0) {free(copy);op_errno(op,path);return -1;}
    char *save=NULL;
    for(char *part=strtok_r(copy,"/",&save);part;part=strtok_r(NULL,"/",&save)) {
        if(!strcmp(part,"..")) {op_error(op,path,RESULT_INVALID_NAME,"Parent components in Trash location");close(fd);fd=-1;break;}
        if(S_ISDIR(source->st_mode)&&!outside_source(op,source,fd,path)) {close(fd);fd=-1;break;}
        if(trash_mkdir(fd,part)<0&&errno!=EEXIST) {op_errno(op,path);close(fd);fd=-1;break;}
        int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0) {op_errno(op,path);close(fd);fd=-1;break;}
        close(fd);fd=next;
    }
    free(copy);
    if(fd>=0 && !trash_private(op,fd,path)) {close(fd);fd=-1;}
    return fd;
}
static bool trash_contents(Operation *op,TrashLocation *t,const struct stat *source) {
    t->files=trash_child(op,t->root,"files",source,t->path);
    if(t->files>=0) t->info=trash_child(op,t->root,"info",source,t->path);
    if(t->info<0) return false;
    struct stat root,files,info;
    if(fstat(t->root,&root)<0||fstat(t->files,&files)<0||fstat(t->info,&info)<0) return op_errno(op,t->path);
    return (root.st_dev==source->st_dev && files.st_dev==source->st_dev && info.st_dev==source->st_dev) ||
        op_error(op,t->path,RESULT_CROSS_DEVICE,"Trash directories must be on the source filesystem");
}
static int trash_top(Operation *op,int source_parent,const char *parent_path,dev_t device,char **out) {
    *out=text_copy(parent_path);int fd=dup(source_parent);
    if(!*out||fd<0) {if(fd>=0)close(fd);free(*out);*out=NULL;op_error(op,parent_path,RESULT_IO,"Cannot pin filesystem root");return -1;}
    for(unsigned i=0;i<4096;i++) {
        struct stat here,up;
        int next=openat(fd,"..",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0 || fstat(fd,&here)<0 || fstat(next,&up)<0) {op_errno(op,parent_path);if(next>=0)close(next);close(fd);return -1;}
        if(here.st_dev!=device) {close(next);close(fd);op_error(op,parent_path,RESULT_UNSUPPORTED,"Mounted entry cannot be trashed");return -1;}
        if(up.st_dev!=device || same_entry(&here,&up)) {close(next);return fd;}
        char *parent=platform_path_parent(*out);
        if(!parent) {close(next);close(fd);op_error(op,parent_path,RESULT_NO_MEMORY,"Out of memory");return -1;}
        free(*out);*out=parent;close(fd);fd=next;
    }
    close(fd);op_error(op,parent_path,RESULT_UNSUPPORTED,"Filesystem ancestry limit reached");return -1;
}
static bool trash_top_location(Operation *op,int top,const char *top_path,const struct stat *source,TrashLocation *t) {
    struct stat st;char uid[32];snprintf(uid,sizeof uid,"%lu",(unsigned long)geteuid());
    if(fstatat(top,".Trash",&st,AT_SYMLINK_NOFOLLOW)==0 && S_ISDIR(st.st_mode) && (st.st_mode&S_ISVTX)) {
        int shared=openat(top,".Trash",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct stat pinned;
        if(shared>=0 && (fstat(shared,&pinned)<0 || !same_entry(&st,&pinned) || !(pinned.st_mode&S_ISVTX))) {close(shared);shared=-1;}
        if(shared>=0) {
            char *base=platform_path_join(top_path,".Trash");t->path=base?platform_path_join(base,uid):NULL;free(base);
            t->root=t->path?trash_child(op,shared,uid,source,t->path):-1;close(shared);
            if(t->root>=0 && trash_contents(op,t,source)) return true;
            trash_location_free(t);op->error=result_make(RESULT_OK,NULL);
        }
    }
    char name[48];snprintf(name,sizeof name,".Trash-%lu",(unsigned long)geteuid());
    t->path=platform_path_join(top_path,name);
    if(!t->path) return op_error(op,top_path,RESULT_NO_MEMORY,"Out of memory");
    t->root=trash_child(op,top,name,source,t->path);
    return t->root>=0 && trash_contents(op,t,source);
}
static char *trash_metadata(const char *path) {
    size_t len=strlen(path);if(len>(SIZE_MAX-128)/3) return NULL;
    char *data=malloc(len*3+128);if(!data) return NULL;
    size_t n=(size_t)sprintf(data,"[Trash Info]\nPath=");
    static const char digits[]="0123456789ABCDEF";
    for(size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)path[i];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("/-_.~",c)) data[n++]=(char)c;
        else {data[n++]='%';data[n++]=digits[c>>4];data[n++]=digits[c&15];}
    }
    time_t now=time(NULL);struct tm local;char date[32];
    if(!localtime_r(&now,&local)||!strftime(date,sizeof date,"%Y-%m-%dT%H:%M:%S",&local)) {free(data);return NULL;}
    snprintf(data+n,128,"\nDeletionDate=%s\n",date);return data;
}
static bool trash_info_remove(int parent,const char *name,const struct stat *expected) {
    struct stat now;
    return fstatat(parent,name,&now,AT_SYMLINK_NOFOLLOW)==0 && same_entry(&now,expected) && unlinkat(parent,name,0)==0;
}
Result platform_trash_progress(const char *src,char **destination,OperationCallback callback,void *context) {
    *destination=NULL;Operation op={.callback=callback,.context=context};
    TrashLocation location=trash_empty();int sp=-1,source_sync=-1,top=-1,info_fd=-1;
    char *sn=NULL,*parent=NULL,*canonical=NULL,*original=NULL,*home=NULL,*top_path=NULL,*metadata=NULL,*target_path=NULL,*info_path=NULL;
    char entry[96]="",info_name[112]="";bool reserved=false,moved=false,orphan=false;
    struct stat source={0},record={0};
    if(!op_poll(&op,src)) goto done;
    parent=platform_path_parent(src);
    if(!parent) {op_error(&op,src,RESULT_NO_MEMORY,"Out of memory");goto done;}
    Result resolved=platform_resolve(NULL,parent,&canonical);
    if(resolved.code!=RESULT_OK) {op_error(&op,src,resolved.code,resolved.detail);goto done;}
    char *leaf=platform_path_name(src);original=leaf?platform_path_join(canonical,leaf):NULL;free(leaf);
    if(!original) {op_error(&op,src,RESULT_NO_MEMORY,"Out of memory");goto done;}
    sp=operation_parent(&op,original,&sn);if(sp<0) goto done;
    if(fstatat(sp,sn,&source,AT_SYMLINK_NOFOLLOW)<0) {op_errno(&op,src);goto done;}
    if(!S_ISREG(source.st_mode)&&!S_ISDIR(source.st_mode)&&!S_ISLNK(source.st_mode)) {op_error(&op,src,RESULT_UNSUPPORTED,"Trash supports regular files, directories and links only");goto done;}
    source_sync=openat(sp,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(source_sync<0) {op_errno(&op,src);goto done;}
    struct stat parent_st;struct statfs fs;
    if(fstat(source_sync,&parent_st)<0 || fstatfs(source_sync,&fs)<0) {op_errno(&op,src);goto done;}
    if(parent_st.st_dev!=source.st_dev) {op_error(&op,src,RESULT_UNSUPPORTED,"Cannot trash a filesystem mount root");goto done;}
    if(fs.f_type!=EXT4_SUPER_MAGIC && fs.f_type!=BTRFS_SUPER_MAGIC && fs.f_type!=XFS_SUPER_MAGIC && fs.f_type!=TMPFS_MAGIC && fs.f_type!=OVERLAYFS_SUPER_MAGIC) {
        op_error(&op,src,RESULT_UNSUPPORTED,"Trash requires a supported local POSIX filesystem (no remote/DrvFS)");goto done;
    }
#ifdef TFILE_TRASH_TEST_HOOKS
    top=platform_trash_test_top(sp,&top_path);
#endif
    if(top<0) {
        const char *xdg=getenv("XDG_DATA_HOME"),*env_home=getenv("HOME");
        if(xdg&&*xdg=='/') home=platform_path_join(xdg,"Trash");
        else if(env_home&&*env_home=='/') home=platform_path_join(env_home,".local/share/Trash");
        else {op_error(&op,src,RESULT_ACCESS,"No absolute XDG_DATA_HOME/HOME for Trash");goto done;}
        if(!home) {op_error(&op,src,RESULT_NO_MEMORY,"Out of memory");goto done;}
        location.root=trash_home_root(&op,home,&source);if(location.root<0) goto done;
        struct stat st;if(fstat(location.root,&st)<0) {op_errno(&op,src);goto done;}
        if(st.st_dev==source.st_dev) {
            location.path=text_copy(home);location.home=true;
            if(!location.path) {op_error(&op,src,RESULT_NO_MEMORY,"Out of memory");goto done;}
            if(!trash_contents(&op,&location,&source)) goto done;
        } else {
            trash_location_free(&location);
            top=trash_top(&op,source_sync,canonical,source.st_dev,&top_path);if(top<0) goto done;
        }
    }
    if(top>=0 && !trash_top_location(&op,top,top_path,&source,&location)) goto done;
    char *record_path=location.home?text_copy(original):platform_path_relative(top_path,original);
    if(!record_path) {op_error(&op,src,RESULT_NO_MEMORY,"Out of memory");goto done;}
    if(!location.home&&platform_path_absolute(record_path)) {free(record_path);op_error(&op,src,RESULT_UNSUPPORTED,"Cannot derive mount-relative Trash path");goto done;}
    metadata=trash_metadata(record_path);free(record_path);
    if(!metadata) {op_error(&op,src,RESULT_NO_MEMORY,"Cannot allocate/date Trash metadata");goto done;}
    static unsigned sequence;
    for(unsigned attempt=0;attempt<256;attempt++) {
        snprintf(entry,sizeof entry,"tfile-%lld-%ld-%u",(long long)time(NULL),(long)getpid(),++sequence);
        snprintf(info_name,sizeof info_name,"%s.trashinfo",entry);
        struct stat occupied;
        if(fstatat(location.files,entry,&occupied,AT_SYMLINK_NOFOLLOW)==0) continue;
        if(errno!=ENOENT) {op_errno(&op,src);goto done;}
        char *files=platform_path_join(location.path,"files"),*infos=platform_path_join(location.path,"info");
        target_path=files?platform_path_join(files,entry):NULL;info_path=infos?platform_path_join(infos,info_name):NULL;free(files);free(infos);
        if(!target_path||!info_path) {op_error(&op,src,RESULT_NO_MEMORY,"Out of memory");goto done;}
        info_fd=openat(location.info,info_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(info_fd>=0) {reserved=true;break;}
        int reserve_error=errno;free(target_path);free(info_path);target_path=info_path=NULL;errno=reserve_error;
        if(errno!=EEXIST) {op_errno(&op,src);goto done;}
    }
    if(info_fd<0) {op_error(&op,src,RESULT_EXISTS,"Cannot reserve a unique Trash entry");goto done;}
    if(fstat(info_fd,&record)<0) {op_errno(&op,src);goto done;}
    if(fchmod(info_fd,0600)<0) {op_errno(&op,src);goto done;}
    TRASH_HOOK("trash-write",info_fd,info_name);
    size_t at=0,n=strlen(metadata);
    while(at<n) {ssize_t written=write(info_fd,metadata+at,n-at);if(written<0&&errno==EINTR)continue;if(written<=0){if(!written)errno=EIO;op_errno(&op,src);goto done;}at+=(size_t)written;}
    if(fsync(info_fd)<0) {op_errno(&op,src);goto done;}
    if(close(info_fd)<0) {info_fd=-1;op_errno(&op,src);goto done;}info_fd=-1;
    if(fsync(location.info)<0) {op_errno(&op,src);goto done;}
    TRASH_HOOK("trash-ready",sp,sn);
    if(!op_poll(&op,src) || !verify_name(&op,sp,sn,&source,src) || !verify_name(&op,location.info,info_name,&record,info_path)) goto done;
    TRASH_HOOK("trash-rename",location.files,entry);
    if(renameat2(sp,sn,location.files,entry,RENAME_NOREPLACE)<0) {op_errno(&op,src);goto done;}
    moved=true;TRASH_HOOK("trash-moved",location.files,entry);
    if(fsync(location.files)<0 || fsync(source_sync)<0) op_errno(&op,src);
done:
    if(info_fd>=0) close(info_fd);
    if(reserved&&!moved) orphan=!trash_info_remove(location.info,info_name,&record);
    Result r=op.error;r.partial=moved||orphan;r.completed_items=moved?1:0;
    char reason[112];snprintf(reason,sizeof reason,"%.111s",r.detail);
    if(moved) {
        snprintf(r.detail,sizeof r.detail,"%s; metadata kept; %s",r.code==RESULT_OK?"Moved to Trash":"Moved to Trash but final sync failed",entry);
        diagnostic_path(r.path,sizeof r.path,target_path);*destination=target_path;target_path=NULL;
        if(r.code==RESULT_OK) r.partial=false;
    } else if(orphan) {
        snprintf(r.detail,sizeof r.detail,"Original kept; orphan Trash metadata remains: %s; %.70s",info_name,reason);
        diagnostic_path(r.path,sizeof r.path,info_path);
    } else {
        snprintf(r.detail,sizeof r.detail,"Original kept; Trash setup dirs may remain; %.180s",reason);
        diagnostic_path(r.path,sizeof r.path,src);
    }
    if(sp>=0) close(sp);
    if(source_sync>=0) close(source_sync);
    if(top>=0) close(top);
    trash_location_free(&location);free(sn);free(parent);free(canonical);free(original);free(home);free(top_path);free(metadata);free(target_path);free(info_path);
    return r;
}
