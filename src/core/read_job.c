#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>

enum { READ_FILE = 1, READ_STATE, READ_DIRECTORY, READ_DONE };
typedef struct { FileInfo info; size_t name, path; } WireFile;
struct ReadJob {
    PlatformTask *task;
    SearchResult result;
    char *directory;
    size_t capacity;
    bool search, done, pending;
};
typedef struct {
    const char *base, *path, *term;
    SearchLimits limits;
    bool search, hidden, directories_only;
} Request;
typedef struct { PlatformTaskWriter *writer; size_t sent; } Writer;

static bool send_file(PlatformTaskWriter *writer, const FileInfo *file) {
    WireFile wire = {.info = *file, .name = strlen(file->name) + 1, .path = strlen(file->path) + 1};
    wire.info.name = wire.info.path = NULL;
    if (wire.name > 1024u * 1024u - sizeof wire || wire.path > 1024u * 1024u - wire.name - sizeof wire) return false;
    size_t len = sizeof wire + wire.name + wire.path;
    unsigned char *data = malloc(len);
    if (!data) return false;
    memcpy(data, &wire, sizeof wire);
    memcpy(data + sizeof wire, file->name, wire.name);
    memcpy(data + sizeof wire + wire.name, file->path, wire.path);
    bool ok = platform_task_send(writer, READ_FILE, data, len);
    free(data); return ok;
}
static bool send_progress(const SearchResult *result, const char *path, void *context) {
    (void)path;
    Writer *writer = context;
    while (writer->sent < result->matches.len) {
        if (!send_file(writer->writer, &result->matches.entries[writer->sent++])) return false;
    }
    SearchResult state = *result; state.matches = (FileList){0};
    return platform_task_send(writer->writer, READ_STATE, &state, sizeof state);
}
static void read_work(PlatformTaskWriter *writer, void *context) {
    Request *request = context;
    SearchResult result = {0};
    bool complete = false;
    if (request->search) {
        Writer progress = {writer, 0};
        result = core_search_unsorted(request->path, request->term, request->limits, send_progress, &progress);
        if (!send_progress(&result, request->path, &progress)) goto done;
    } else {
        char *resolved = NULL;
        result.result = core_resolve_directory(request->base, request->path, &resolved);
        if (result.result.code == RESULT_OK) {
            if (!platform_task_send(writer, READ_DIRECTORY, resolved, strlen(resolved) + 1)) { free(resolved); goto done; }
            result.result = core_read_directory(resolved, request->hidden, request->directories_only, &result.matches);
            free(resolved);
            for (size_t i = 0; i < result.matches.len; i++)
                if (!send_file(writer, &result.matches.entries[i])) goto done;
        }
        SearchResult state = result; state.matches = (FileList){0};
        if (!platform_task_send(writer, READ_STATE, &state, sizeof state)) goto done;
    }
    complete = true;
done:
    search_result_free(&result);
    if (complete) (void)platform_task_send(writer, READ_DONE, NULL, 0);
}
static Result start(Request *request, ReadJob **out) {
    *out = NULL;
    ReadJob *job = calloc(1, sizeof *job);
    if (!job) return result_make(RESULT_NO_MEMORY, "Out of memory");
    job->search = request->search;
    Result r = platform_task_open(read_work, request, &job->task);
    if (r.code != RESULT_OK) { free(job); return r; }
    *out = job; return r;
}
Result core_read_search_start(const char *root, const char *term, SearchLimits limits, ReadJob **out) {
    Request request = {.path = root, .term = term, .limits = limits, .search = true};
    return start(&request, out);
}
Result core_read_directory_start(const char *base, const char *path, bool hidden, bool directories_only, ReadJob **out) {
    Request request = {.base = base, .path = path, .hidden = hidden, .directories_only = directories_only};
    return start(&request, out);
}
static Result receive_file(ReadJob *job, const void *data, size_t len) {
    if (len < sizeof(WireFile)) return result_make(RESULT_IO, "Invalid read worker file");
    WireFile wire; memcpy(&wire, data, sizeof wire);
    if (!wire.name || !wire.path || wire.name > len - sizeof wire || wire.path != len - sizeof wire - wire.name)
        return result_make(RESULT_IO, "Invalid read worker strings");
    const char *name = (const char *)data + sizeof wire, *path = name + wire.name;
    if (name[wire.name - 1] || path[wire.path - 1]) return result_make(RESULT_IO, "Unterminated read worker strings");
    FileInfo file = wire.info;
    file.name = text_copy(name); file.path = text_copy(path);
    if (!file.name || !file.path) { file_info_free(&file); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
    if (job->result.matches.len == job->capacity) {
        size_t next = job->capacity ? job->capacity * 2 : 64;
        if (next < job->capacity || next > SIZE_MAX / sizeof(FileInfo)) {
            file_info_free(&file); return result_make(RESULT_NO_MEMORY, "Read result too large");
        }
        FileInfo *files = realloc(job->result.matches.entries, next * sizeof *files);
        if (!files) { file_info_free(&file); return result_make(RESULT_NO_MEMORY, "Out of memory"); }
        job->result.matches.entries = files; job->capacity = next;
    }
    job->result.matches.entries[job->result.matches.len++] = file;
    return result_make(RESULT_OK, NULL);
}
Result core_read_poll(ReadJob *job, bool *done) {
    *done = job->done;
    if (job->done) return job->result.result;
    Result r = result_make(RESULT_OK, NULL);
    job->pending = false;
    /* Limit each UI tick independently of producer speed and list size. */
    for (unsigned i = 0; i < 128; i++) {
        unsigned type; void *data; size_t len; bool ready;
        r = platform_task_receive(job->task, &type, &data, &len, &ready);
        if (r.code != RESULT_OK || !ready) break;
        if (type == READ_FILE) r = receive_file(job, data, len);
        else if (type == READ_STATE && len == sizeof(SearchResult)) {
            FileList matches = job->result.matches;
            memcpy(&job->result, data, len); job->result.matches = matches;
        } else if (type == READ_DIRECTORY && len && !((char *)data)[len - 1] && !job->directory) {
            job->directory = data; data = NULL;
        } else if (type == READ_DONE && !len) job->done = true;
        else r = result_make(RESULT_IO, "Invalid read worker record");
        free(data);
        if (i == 127) job->pending = true;
        if (job->done || r.code != RESULT_OK) break;
    }
    if (r.code != RESULT_OK) { job->result.result = r; job->done = true; }
    if (job->done) {
        platform_task_close(job->task); job->task = NULL;
        if (job->search && job->result.matches.len > 1)
            qsort(job->result.matches.entries, job->result.matches.len, sizeof(FileInfo), file_compare);
    }
    *done = job->done;
    return job->result.result;
}
const SearchResult *core_read_snapshot(const ReadJob *job) { return &job->result; }
bool core_read_pending_messages(const ReadJob *job) { return job->pending; }
void core_read_cancel(ReadJob *job) {
    if (job->done) return;
    platform_task_close(job->task); job->task = NULL;
    job->done = true; job->result.stopped = true;
    if (!job->search) job->result.result = result_make(RESULT_CANCELLED, "Directory load cancelled; previous list kept");
    else if (job->result.matches.len > 1)
        qsort(job->result.matches.entries, job->result.matches.len, sizeof(FileInfo), file_compare);
}
void core_read_take(ReadJob *job, SearchResult *result, char **directory) {
    *result = job->result; job->result = (SearchResult){0};
    if (directory) { *directory = job->directory; job->directory = NULL; }
}
void core_read_close(ReadJob *job) {
    if (!job) return;
    platform_task_close(job->task); search_result_free(&job->result);
    free(job->directory); free(job);
}
bool core_read_cleanup_pending(void) { return platform_task_cleanup_pending(); }
void core_read_shutdown(void) { platform_task_shutdown(); }

bool core_read_available(void) { return platform_read_available(); }
