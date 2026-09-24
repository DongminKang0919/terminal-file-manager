#define _GNU_SOURCE
#include "../src/platform/platform.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test-only synchronization points in the backend. No timing/scheduler races:
   replace the exact entry between metadata lookup/open or open/traversal. */
static const char *hook_stage, *hook_name, *hook_target;
static bool hook_fired, hook_limit_write;
static struct rlimit original_size;
void platform_test_hook(const char *stage, int parent, const char *name) {
    if (!hook_stage || strcmp(stage, hook_stage) || strcmp(name, hook_name)) return;
    hook_stage = NULL; hook_fired = true;
    assert(renameat(parent, name, parent, "saved-entry") == 0);
    assert(symlinkat(hook_target, parent, name) == 0);
    if (hook_limit_write) {
        struct rlimit limit = original_size; limit.rlim_cur = 0;
        assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    }
}
static char *join(const char *a, const char *b) {
    char *p = platform_path_join(a, b); assert(p); return p;
}
static void ok(Result r) {
    if (r.code != RESULT_OK) fprintf(stderr, "Unexpected failure: %s\n", r.detail);
    assert(r.code == RESULT_OK && !r.partial);
}
static void error_at(Result r, const char *path, bool partial) {
    if (r.code == RESULT_OK || r.partial != partial || !strstr(r.path, path))
        fprintf(stderr, "Unexpected result code=%d partial=%d path=%s detail=%s\n", r.code, r.partial, r.path, r.detail);
    assert(r.code != RESULT_OK && r.partial == partial && strstr(r.path, path));
    assert(strstr(r.detail, partial ? "Partial operation" : "No changes"));
}
static void put(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600); assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text)); assert(close(fd) == 0);
}
static void content(const char *path, const char *expected) {
    char b[128]; int fd = open(path, O_RDONLY); assert(fd >= 0);
    ssize_t n = read(fd, b, sizeof b - 1); assert(n >= 0); b[n] = 0;
    assert(!strcmp(b, expected)); assert(close(fd) == 0);
}
static void mode_is(const char *path, mode_t mode) {
    struct stat st; assert(lstat(path, &st) == 0); assert((st.st_mode & 07777) == mode);
}
static int fd_count(void) {
    DIR *d = opendir("/proc/self/fd"); assert(d); int count = 0;
    while (readdir(d)) count++;
    assert(closedir(d) == 0); return count;
}
static char *case_dir(const char *root) {
    static unsigned next;
    char name[32]; snprintf(name, sizeof name, "case-%u", next++);
    char *p = join(root, name); assert(mkdir(p, 0700) == 0); return p;
}
static void arm(const char *stage, const char *name, const char *outside) {
    hook_stage = stage; hook_name = name; hook_target = outside; hook_fired = false;
}
static void source_swap(const char *root, bool copy, const char *stage, bool nested) {
    char *base = case_dir(root), *src = join(base, "tree"), *dst = join(base, "output");
    char *outside = join(base, "outside"), *marker = join(outside, "marker");
    assert(mkdir(src, 0700) == 0); assert(mkdir(outside, 0700) == 0); put(marker, "outside marker");
    char *child = join(src, "child"); assert(mkdir(child, 0700) == 0);
    char *payload = join(child, "payload"); put(payload, "original");
    arm(stage, nested ? "child" : "tree", outside);
    int before = fd_count();
    Result r = copy ? platform_copy(src, dst) : platform_remove(src);
    assert(hook_fired && fd_count() == before);
    bool partial = copy ? (nested || !strcmp(stage, "copy-open")) : !strcmp(stage, "remove-open");
    error_at(r, nested ? "child" : "tree", partial);
    content(marker, "outside marker");
    char *unexpected = join(dst, nested ? "child/marker" : "marker");
    assert(access(unexpected, F_OK) != 0);
    free(unexpected); free(payload); free(child); free(marker); free(outside); free(dst); free(src); free(base);
}
static void destination_swap(const char *root, const char *stage, bool nested) {
    char *base = case_dir(root), *src = join(base, "tree"), *dst = join(base, "output");
    char *outside = join(base, "outside"), *marker = join(outside, "marker");
    assert(mkdir(src, 0700) == 0); assert(mkdir(outside, 0700) == 0); put(marker, "outside marker");
    char *child = join(src, "child"); assert(mkdir(child, 0700) == 0);
    char *payload = join(nested ? child : src, "new-only"); put(payload, "original");
    arm(stage, nested ? "child" : "output", outside);
    int before = fd_count(); Result r = platform_copy(src, dst);
    assert(hook_fired && fd_count() == before); error_at(r, nested ? "child" : "output", true);
    content(marker, "outside marker");
    char *unexpected = join(outside, "new-only"); assert(access(unexpected, F_OK) != 0);
    free(unexpected); free(payload); free(child); free(marker); free(outside); free(dst); free(src); free(base);
}
static void readonly_copy(const char *root, mode_t mask) {
    assert(geteuid() != 0); /* Permission checks must actually constrain this process. */
    char *base = case_dir(root), *src = join(base, "source"), *dst = join(base, "copy");
    assert(mkdir(src, 0700) == 0);
    char *child = join(src, "readonly"), *payload = join(child, "payload");
    assert(mkdir(child, 0700) == 0); put(payload, "read-only tree");
    assert(chmod(payload, 06755) == 0); assert(chmod(child, 0555) == 0); assert(chmod(src, 0555) == 0);
    mode_t saved = umask(mask); ok(platform_copy(src, dst));
    mode_t after = umask(saved); assert(after == mask);
    mode_is(dst, 0555 & ~mask);
    /* An intentionally restrictive mask can remove read/search access. Verify
       each final mode before granting access for checking the copied contents. */
    assert(chmod(dst, 0700) == 0);
    char *copied_child = join(dst, "readonly"), *copied_file = join(copied_child, "payload");
    mode_is(copied_child, 0555 & ~mask); assert(chmod(copied_child, 0700) == 0);
    mode_is(copied_file, 0755 & ~mask); assert(chmod(copied_file, 0600) == 0);
    content(copied_file, "read-only tree");
    assert(chmod(src, 0700) == 0); assert(chmod(child, 0700) == 0);
    free(copied_file); free(copied_child); free(payload); free(child); free(src); free(dst); free(base);
}
static void links_and_conflicts(const char *root) {
    char *base = case_dir(root), *src = join(base, "source"), *dst = join(base, "copy");
    char *outside = join(base, "outside"), *marker = join(outside, "marker");
    assert(mkdir(src, 0700) == 0); assert(mkdir(outside, 0700) == 0); put(marker, "keep");
    char *link = join(src, "link"), *dangling = join(src, "dangling");
    assert(symlink(outside, link) == 0); assert(symlink("missing", dangling) == 0);
    PlatformDirectory *dir = NULL; ok(platform_directory_open(link, &dir)); platform_directory_close(dir);
    ok(platform_copy(src, dst));
    char *copied = join(dst, "link"), *target = NULL;
    ok(platform_link_target(copied, &target)); assert(!strcmp(target, outside)); free(target);
    char *copied_dangling = join(dst, "dangling");
    ok(platform_link_target(copied_dangling, &target)); assert(!strcmp(target, "missing")); free(target);
    ok(platform_remove(src)); ok(platform_remove(dst)); content(marker, "keep");
    assert(mkdir(src, 0700) == 0);
    char *inside = join(src, "inside"); Result r = platform_copy(src, inside);
    assert(r.code == RESULT_SELF_TRANSFER); error_at(r, "inside", false); assert(access(inside, F_OK) != 0);
    put(dst, "do not overwrite"); r = platform_copy(marker, dst);
    assert(r.code == RESULT_EXISTS); error_at(r, "copy", false); content(dst, "do not overwrite");
    assert(unlink(dst) == 0); assert(symlink(marker, dst) == 0);
    r = platform_copy(marker, dst); assert(r.code == RESULT_EXISTS); content(marker, "keep");
    /* Even a symlink in a recursive operation's ancestor path is not traversed. */
    char *alias = join(base, "alias"); assert(symlink(outside, alias) == 0);
    char *through = join(alias, "marker"); error_at(platform_remove(through), "marker", false); content(marker, "keep");
    free(through); free(alias); free(inside); free(copied_dangling); free(copied); free(dangling); free(link);
    free(marker); free(outside); free(src); free(dst); free(base);
}
static void write_failure(const char *root, bool swap) {
    char *base = case_dir(root), *src = join(base, "source"), *dst = join(base, "output");
    char *marker = join(base, "marker"); put(src, "long payload"); put(marker, "keep original user file");
    assert(getrlimit(RLIMIT_FSIZE, &original_size) == 0);
    if (swap) { hook_limit_write = true; arm("copy-file-created", "output", marker); }
    else { struct rlimit limit = original_size; limit.rlim_cur = 1; assert(setrlimit(RLIMIT_FSIZE, &limit) == 0); }
    int before = fd_count(); Result r = platform_copy(src, dst);
    assert(setrlimit(RLIMIT_FSIZE, &original_size) == 0); hook_limit_write = false;
    assert(fd_count() == before); error_at(r, "output", true); content(marker, "keep original user file");
    if (swap) {
        assert(hook_fired); char *target = NULL; ok(platform_link_target(dst, &target));
        assert(!strcmp(target, marker)); free(target);
    } else { content(dst, "l"); mode_is(dst, 0600); }
    free(marker); free(src); free(dst); free(base);
}
static void deep_success(const char *root) {
    char *base = case_dir(root), *src = join(base, "source"), *dst = join(base, "copy");
    assert(mkdir(src, 0700) == 0);
    char component[101]; memset(component, 'd', 100); component[100] = 0;
    int fd = open(src, O_RDONLY | O_DIRECTORY); assert(fd >= 0);
    for (unsigned i = 0; i < 48; ++i) {
        assert(mkdirat(fd, component, 0700) == 0);
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY); assert(next >= 0); close(fd); fd = next;
    }
    int leaf = openat(fd, "payload", O_WRONLY | O_CREAT | O_EXCL, 0600); assert(leaf >= 0);
    assert(write(leaf, "deep", 4) == 4); close(leaf); close(fd);
    ok(platform_copy(src, dst));
    fd = open(dst, O_RDONLY | O_DIRECTORY); assert(fd >= 0);
    for (unsigned i = 0; i < 48; ++i) {
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY); assert(next >= 0); close(fd); fd = next;
    }
    char text[5] = {0}; leaf = openat(fd, "payload", O_RDONLY); assert(leaf >= 0);
    assert(read(leaf, text, 4) == 4 && !strcmp(text, "deep")); close(leaf); close(fd);
    ok(platform_remove(src)); ok(platform_remove(dst)); free(src); free(dst); free(base);
}
static void unsupported_partial(const char *root) {
    char *base = case_dir(root), *src = join(base, "source"), *dst = join(base, "copy");
    assert(mkdir(src, 0700) == 0);
    char *fifo = join(src, "pipe"); assert(mkfifo(fifo, 0600) == 0);
    int before = fd_count(); Result r = platform_copy(src, dst);
    assert(r.code == RESULT_UNSUPPORTED); error_at(r, "pipe", true);
    assert(fd_count() == before); mode_is(dst, 0700);
    /* A retry must not merge into or remove the leftover destination. */
    char *user = join(dst, "user-file"); put(user, "keep user file");
    r = platform_copy(src, dst); assert(r.code == RESULT_EXISTS);
    error_at(r, "copy", false); content(user, "keep user file");
    free(user); free(fifo); free(src); free(dst); free(base);
}
static void deep_limits(const char *root) {
    char *base = case_dir(root), *src = join(base, "source"); assert(mkdir(src, 0700) == 0);
    int fd = open(src, O_RDONLY | O_DIRECTORY); assert(fd >= 0);
    /* Paths exceed PATH_MAX, but each component is legal. Creation uses FDs. */
    const char *component = "deep-directory-component-abcdefghijklmnopqrstuvwxyz-0123456789-abcdef";
    for (unsigned i = 0; i < 80; ++i) {
        assert(mkdirat(fd, component, 0700) == 0);
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY); assert(next >= 0); close(fd); fd = next;
    }
    int leaf = openat(fd, "keep", O_WRONLY | O_CREAT | O_EXCL, 0600); assert(leaf >= 0);
    assert(write(leaf, "deep marker", 11) == 11); close(leaf); close(fd);
    struct rlimit saved; assert(getrlimit(RLIMIT_NOFILE, &saved) == 0);
    for (unsigned i = 0; i < 12; ++i) {
        char name[40]; snprintf(name, sizeof name, "copy-%u", i); char *dst = join(base, name);
        int before = fd_count();
        if (i < 6) { struct rlimit low = saved; low.rlim_cur = 24; assert(setrlimit(RLIMIT_NOFILE, &low) == 0); }
        Result r = platform_copy(src, dst);
        assert(setrlimit(RLIMIT_NOFILE, &saved) == 0);
        error_at(r, "deep-directory", true); assert(fd_count() == before);
        if (i >= 6) assert(strstr(r.detail, "depth limit"));
        if (i < 6) { struct rlimit low = saved; low.rlim_cur = 24; assert(setrlimit(RLIMIT_NOFILE, &low) == 0); }
        r = platform_remove(src);
        assert(setrlimit(RLIMIT_NOFILE, &saved) == 0);
        error_at(r, "deep-directory", false); assert(fd_count() == before);
        if (i >= 6) assert(strstr(r.detail, "depth limit"));
        free(dst);
    }
    /* Traverse all the way to confirm failure never reported success or removed
       the leaf. Descriptor use stays constant in the test itself. */
    fd = open(src, O_RDONLY | O_DIRECTORY); assert(fd >= 0);
    for (unsigned i = 0; i < 80; ++i) {
        int next = openat(fd, component, O_RDONLY | O_DIRECTORY); assert(next >= 0); close(fd); fd = next;
    }
    leaf = openat(fd, "keep", O_RDONLY); assert(leaf >= 0); char text[12] = {0};
    assert(read(leaf, text, 11) == 11 && !strcmp(text, "deep marker")); close(leaf); close(fd);
    free(src); free(base);
}
int main(int argc, char **argv) {
    assert(argc == 2 && geteuid() != 0);
    signal(SIGXFSZ, SIG_IGN);
    mode_t saved = umask(0022);
    int before = fd_count();
    for (unsigned nested = 0; nested < 2; ++nested) {
        source_swap(argv[1], false, "remove-stat", nested);
        source_swap(argv[1], false, "remove-open", nested);
        source_swap(argv[1], true, "copy-stat", nested);
        source_swap(argv[1], true, "copy-open", nested);
        destination_swap(argv[1], "copy-created", nested);
        destination_swap(argv[1], "copy-destination-open", nested);
    }
    readonly_copy(argv[1], 0022); readonly_copy(argv[1], 0077); readonly_copy(argv[1], 0777);
    links_and_conflicts(argv[1]); write_failure(argv[1], false); write_failure(argv[1], true);
    deep_success(argv[1]); deep_limits(argv[1]); unsupported_partial(argv[1]);
    assert(fd_count() == before); umask(saved);
    puts("PASS: controlled source/destination swaps, outside markers, non-root 0555/umask, links, no-clobber, partial failures, depth/FD limits and FD cleanup");
}
