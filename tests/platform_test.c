#include "../src/platform/platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ok(Result r) { if (r.code != RESULT_OK) fprintf(stderr, "%s\n", r.detail); assert(r.code == RESULT_OK); }
int main(int argc, char **argv) {
    assert(argc >= 2);
    char *p = platform_path_parent("/"); assert(!strcmp(p, "/")); free(p);
    p = platform_path_name("/a/b/"); assert(!strcmp(p, "b")); free(p);
    p = platform_path_parent("/a/b/"); assert(!strcmp(p, "/a")); free(p);
    p = platform_path_join("/", "a"); assert(!strcmp(p, "/a")); free(p);
    p = platform_path_relative("/", "/a/b"); assert(!strcmp(p, "a/b")); free(p);
    assert(!platform_name_valid("a/b")); assert(platform_name_valid("a\\b"));
    char *long_name = malloc(12001); assert(long_name);
    memset(long_name, 'x', 12000); long_name[12000] = 0;
    p = platform_path_join(argv[1], long_name); assert(p && strlen(p) > 12000); free(p); free(long_name);
    char *resolved = NULL; ok(platform_resolve(argv[1], "sub/..", &resolved));
    assert(!strcmp(resolved, argv[1])); free(resolved);
    char *sample = platform_path_join(argv[1], "sample.txt");
    PlatformDirectory *dir = NULL;
    assert(platform_directory_open(sample, &dir).code == RESULT_NOT_DIRECTORY && !dir);
    FileInfo info; ok(platform_info(sample, &info));
    assert(info.kind == FILE_REGULAR && info.has_posix_mode && info.size > 0); file_info_free(&info);
    char *dangling = platform_path_join(argv[1], "dangling");
    ok(platform_info(dangling, &info)); assert(info.kind == FILE_LINK && !info.directory_target); file_info_free(&info);
    char *target = NULL; ok(platform_link_target(dangling, &target)); assert(strstr(target, "missing-target")); free(target);
    char *alias = platform_path_join(argv[1], "alias");
    ok(platform_info(alias, &info)); assert(info.kind == FILE_LINK && info.directory_target); file_info_free(&info);
    bool inside; ok(platform_descendant(argv[1], alias, &inside)); assert(inside);
    /* Destination created between validation and move must never be replaced. */
    char *existing = platform_path_join(argv[1], "existing.txt");
    assert(platform_move(sample, existing).code == RESULT_EXISTS);
    ok(platform_info(sample, &info)); file_info_free(&info);
    PlatformReader *reader = NULL; bool end; char buffer[128];
    ok(platform_reader_open(existing, &reader));
    unsigned char sample_bytes[8]; size_t sampled;
    ok(platform_reader_peek(reader, sample_bytes, sizeof sample_bytes, &sampled));
    assert(sampled == 8 && !memcmp(sample_bytes, "keep me\n", 8));
    ok(platform_reader_line(reader, buffer, sizeof buffer, &end)); assert(!end && !strcmp(buffer, "keep me\n"));
    platform_reader_close(reader);
    if (argc > 2) {
        char *locked = platform_path_join(argv[1], "locked");
        assert(platform_directory_open(locked, &dir).code == RESULT_ACCESS); free(locked);
    }
    uint64_t before = platform_monotonic_ms(); assert(platform_monotonic_ms() >= before);
    free(existing); free(alias); free(dangling); free(sample);
    puts("PASS: POSIX paths, metadata, links, errors, no-replace move and monotonic clock");
}
