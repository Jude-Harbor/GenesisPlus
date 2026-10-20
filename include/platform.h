#ifndef GENESIS_PLATFORM_H
#define GENESIS_PLATFORM_H

#include <stddef.h>

#include "buffer.h"

/*
 * The only module (besides server.c) that uses operating-system APIs.
 * Everything else in the compiler is plain C17.
 *
 * Paths are passed as bytes to the C library. On Windows this means the
 * active code page is used, so project paths containing non-ASCII
 * characters may not work there (see docs/known-limitations in README).
 */

/* Maximum size of a source file the compiler will read. */
#define PLATFORM_MAX_FILE_SIZE (16u * 1024u * 1024u)

int platform_is_directory(const char *path);
int platform_is_file(const char *path);

/* Creates a directory. Succeeds if it already exists as a directory. */
int platform_make_directory(const char *path);

/* Reads a whole file into a new NUL-terminated heap buffer. On failure,
   appends a message to `error` and returns 0. */
int platform_read_file(const char *path, char **data, size_t *length, Buffer *error);

/* Writes `data` to `path` via a temporary file that is then moved into
   place, so readers never observe a half-written file. */
int platform_write_file_atomic(const char *path, const char *data, size_t length,
                               Buffer *error);

/* Recursively copies the directory `from` into `to` (creating `to`). */
int platform_copy_tree(const char *from, const char *to, Buffer *error);

/* A value that changes whenever the file's size or modification time
   changes. Returns 0 if the file does not exist. */
int platform_file_stamp(const char *path, unsigned long long *stamp);

/* Like platform_file_stamp, for every file below a directory. A missing
   directory has a stamp of 0. */
void platform_tree_stamp(const char *path, unsigned long long *stamp);

void platform_sleep_ms(unsigned milliseconds);

/* Stops Windows from translating "\n" to "\r\n" on stdin/stdout. */
void platform_set_binary_stdio(void);

/* Appends `a`, a '/' separator if needed, and `b`. */
void path_join(Buffer *out, const char *a, const char *b);

/* Appends the directory part of `path` ("." if there is none). */
void path_dirname(Buffer *out, const char *path);

#endif /* GENESIS_PLATFORM_H */
