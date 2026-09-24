#ifndef TFILE_PLATFORM_H
#define TFILE_PLATFORM_H
#include "../model.h"
/* Only this backend knows native paths, filesystem handles and error numbers.
   Output strings are owned by the caller. On failure pointer outputs stay NULL. */
char *platform_path_join(const char *directory, const char *name);
char *platform_path_parent(const char *path);
char *platform_path_name(const char *path);
char *platform_path_relative(const char *root, const char *path);
bool platform_name_valid(const char *name);
Result platform_resolve(const char *base, const char *input, char **out);
Result platform_descendant(const char *source, const char *directory, bool *out);
Result platform_info(const char *path, FileInfo *out);
typedef struct PlatformDirectory PlatformDirectory;
Result platform_directory_open(const char *path, PlatformDirectory **out);
Result platform_directory_next(PlatformDirectory *dir, FileInfo *out, bool *end);
void platform_directory_close(PlatformDirectory *dir);
Result platform_create(const char *path, bool directory);
Result platform_copy(const char *source, const char *destination);
Result platform_move(const char *source, const char *destination);
Result platform_remove(const char *path);
Result platform_link_target(const char *path, char **out);
typedef struct PlatformReader PlatformReader;
Result platform_reader_open(const char *path, PlatformReader **out);
/* Read a sample at the current position without advancing the reader. */
Result platform_reader_peek(PlatformReader *reader, unsigned char *buffer, size_t capacity, size_t *read_count);
Result platform_reader_line(PlatformReader *reader, char *buffer, size_t size, bool *end);
void platform_reader_close(PlatformReader *reader);
uint64_t platform_monotonic_ms(void);
#endif
