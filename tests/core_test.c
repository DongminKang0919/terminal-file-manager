#include "../src/core/core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ok(Result r) {
    if (r.code != RESULT_OK) fprintf(stderr, "Unexpected error %d: %s\n", r.code, r.detail);
    assert(r.code == RESULT_OK);
}
static bool stop_after_batch(const SearchResult *progress, const char *path, void *context) {
    size_t *calls = context;
    assert(path && *path); (*calls)++;
    return progress->visited < 64;
}
static bool cancel_immediately(const SearchResult *progress, const char *path, void *context) {
    (void)progress; (void)path; (void)context; return false;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *root = argv[1];
    AppState first, second;
    ok(app_init(&first, root)); ok(app_init(&second, root));
    assert(first.wheel_step == 1);
    assert(first.files.len > 0 && first.files.entries[0].kind == FILE_DIRECTORY);
    size_t full_count = first.files.len;
    first.show_hidden = false;
    ok(app_refresh(&first)); assert(first.files.len == full_count - 1);
    assert(second.files.len == full_count && second.show_hidden);
    for (size_t i = 0; i < first.files.len; i++) assert(!first.files.entries[i].hidden);
    char *sub = core_path_join(root, "sub"); assert(sub);
    FileList choices; ok(core_list(root, true, true, &choices));
    for (size_t i = 0; i < choices.len; i++) assert(choices.entries[i].directory_target);
    file_list_free(&choices);
    ok(app_navigate(&first, sub)); assert(!strcmp(second.directory, root));
    assert(app_navigate(&first, "missing").code == RESULT_NOT_FOUND);
    assert(!strcmp(first.directory, sub)); /* Failed navigation does not clear current data. */
    char *parent = core_path_parent(sub); ok(app_navigate(&first, parent)); free(parent);
    assert(text_contains("ReadMe.TXT", "me.txt")); assert(!text_contains("ReadMe", "xyz"));

    ok(core_create(root, "new-file", false));
    assert(core_create(root, "new-file", false).code == RESULT_EXISTS);
    assert(core_create(root, "", false).code == RESULT_INVALID_NAME);
    assert(core_create(root, "..", true).code == RESULT_INVALID_NAME);
    ok(core_create(root, "new-directory", true));
    char *source = core_path_join(root, "sample.txt"), *destination = NULL;
    ok(core_transfer(false, source, sub, "sample-copy.txt", &destination));
    assert(destination);
    PreviewText text; ok(core_preview_text(destination, 0, 2, &text));
    assert(text.len == 2 && text.more && !strcmp(text.lines[0], "first line"));
    assert(!strcmp(text.lines[1], "second line")); preview_text_free(&text);
    char *copy = destination; destination = NULL;
    assert(core_transfer(false, source, sub, "sample-copy.txt", &destination).code == RESULT_EXISTS);
    assert(!destination);
    ok(core_transfer(true, copy, sub, "renamed.txt", &destination));
    FileInfo info;
    assert(core_info(copy, &info).code == RESULT_NOT_FOUND);
    ok(core_info(source, &info)); file_info_free(&info);
    ok(core_delete(destination)); free(copy); free(destination); destination = NULL;
    assert(core_transfer(false, sub, sub, "nested", &destination).code == RESULT_SELF_TRANSFER);
    assert(core_transfer(true, sub, sub, "nested", &destination).code == RESULT_SELF_TRANSFER);
    char *alias = core_path_join(root, "alias");
    assert(core_transfer(false, sub, alias, "nested", &destination).code == RESULT_SELF_TRANSFER);
    assert(core_transfer(true, sub, alias, "nested", &destination).code == RESULT_SELF_TRANSFER);
    char *lexical = core_path_join(sub, "../sub");
    assert(core_transfer(false, sub, lexical, "nested", &destination).code == RESULT_SELF_TRANSFER);
    free(lexical); free(alias);
    ok(core_transfer(false, sub, root, "tree-copy", &destination));
    char *nested_copy = core_path_join(destination, "sample.txt");
    ok(core_preview_text(nested_copy, 0, 2, &text));
    assert(text.len == 1 && !strcmp(text.lines[0], "nested"));
    preview_text_free(&text); free(nested_copy);
    ok(core_delete(destination)); free(destination); destination = NULL;
    char *dangling = core_path_join(root, "dangling");
    assert(core_create(root, "dangling", false).code == RESULT_EXISTS);
    ok(core_transfer(false, dangling, sub, "link-copy", &destination));
    ok(core_info(destination, &info)); assert(info.kind == FILE_LINK); file_info_free(&info);
    ok(core_delete(destination)); free(destination); free(dangling); destination = NULL;

    ok(core_preview_text(source, 2, 2, &text));
    assert(text.skipped == 2 && text.len == 1 && !text.more && !strcmp(text.lines[0], "third line"));
    preview_text_free(&text);
    ok(core_preview_text(source, 99, 10, &text));
    assert(text.skipped == 3 && text.len == 0 && !text.more); preview_text_free(&text);
    char *binary = core_path_join(root, "binary.bin");
    ok(core_preview_text(binary, 0, 10, &text)); assert(text.binary && !text.len); preview_text_free(&text); free(binary);
    char *missing = core_path_join(root, "absent");
    assert(core_preview_text(missing, 0, 10, &text).code == RESULT_NOT_FOUND); preview_text_free(&text); free(missing);
    assert(core_preview_text(sub, 0, 10, &text).code == RESULT_UNSUPPORTED); preview_text_free(&text);

    SearchResult search = core_search(root, "sample", search_default_limits(), NULL, NULL);
    ok(search.result); assert(search.matches.len == 2); /* root and sub, no symlink traversal */
    assert(!search.stopped && !search.limited); search_result_free(&search);
    SearchLimits limits = search_default_limits(); limits.max_results = 1;
    search = core_search(root, "sample", limits, NULL, NULL);
    assert(search.matches.len == 1 && search.limited); search_result_free(&search);
    limits = search_default_limits(); limits.max_visited = 3;
    search = core_search(root, "", limits, NULL, NULL);
    assert(search.visited == 3 && search.limited); search_result_free(&search);
    limits = search_default_limits(); limits.max_depth = 0;
    search = core_search(root, "sample", limits, NULL, NULL);
    assert(search.matches.len == 1 && search.limited); search_result_free(&search);
    search = core_search(root, "", search_default_limits(), cancel_immediately, NULL);
    assert(search.stopped && search.visited == 0); search_result_free(&search);
    size_t calls = 0;
    search = core_search(root, "", search_default_limits(), stop_after_batch, &calls);
    assert(search.stopped && search.visited == 64 && calls >= 2); search_result_free(&search);
    assert(!strcmp(first.directory, root) && !strcmp(second.directory, root));

    char *new_directory = core_path_join(root, "new-directory");
    ok(core_create(new_directory, "child", false)); ok(core_delete(new_directory));
    assert(core_info(new_directory, &info).code == RESULT_NOT_FOUND); free(new_directory);
    size_t history_len = first.history_len;
    ok(app_refresh(&first)); assert(first.history_len == history_len);
    ok(app_navigate(&first, sub)); ok(app_history(&first, false));
    assert(!strcmp(first.directory, root));
    ok(app_history(&first, true)); assert(!strcmp(first.directory, sub));
    ok(app_history(&first, false));
    ok(core_create(root, "history-branch", true));
    char *branch = core_path_join(root, "history-branch");
    ok(app_navigate(&first, branch));
    assert(app_history(&first, true).code == RESULT_NOT_FOUND);
    ok(app_history(&first, false));
    ok(core_delete(branch));
    size_t history_at = first.history_at;
    assert(app_history(&first, true).code == RESULT_NOT_FOUND);
    assert(first.history_at == history_at && !strcmp(first.directory, root));
    free(branch);
    for (int i = 0; i < 140; i++) ok(app_navigate(&first, i % 2 ? root : sub));
    assert(first.history_len == 128 && first.history_at == 127);
    assert(second.history_len == 1); /* history is per app, not global */
    char *unsupported = core_path_join(root, "unsupported-tree");
    Result partial = core_transfer(false, unsupported, root, "partial-copy", &destination);
    assert(partial.code == RESULT_UNSUPPORTED && partial.partial && !destination);
    assert(strstr(partial.path, "unsupported-tree/pipe") && strstr(partial.detail, "Partial operation"));
    char *leftover = core_path_join(root, "partial-copy");
    ok(core_info(leftover, &info)); assert(info.kind == FILE_DIRECTORY); file_info_free(&info);
    ok(core_delete(leftover)); free(leftover); free(unsupported);
    free(source); free(sub); app_free(&first); app_free(&second);
    puts("PASS: core state, operations, search limits/cancellation, paged preview (without ncurses)");
    return 0;
}
