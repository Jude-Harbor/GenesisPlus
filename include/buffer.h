#ifndef GENESIS_BUFFER_H
#define GENESIS_BUFFER_H

#include <stdarg.h>
#include <stddef.h>

/* Lets GCC check printf-style format strings. MinGW needs the GNU dialect
   to accept C99 conversions such as %zu. */
#if defined(__MINGW32__)
#define GENESIS_PRINTF(fmt, args) __attribute__((format(gnu_printf, fmt, args)))
#elif defined(__GNUC__)
#define GENESIS_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define GENESIS_PRINTF(fmt, args)
#endif

/*
 * A growable, NUL-terminated byte buffer.
 *
 * Appending never aborts. If an allocation fails the buffer sets `failed`
 * and ignores further appends, so callers can perform many appends and
 * check for failure once at the end.
 */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    int failed;
} Buffer;

void buffer_init(Buffer *buffer);
void buffer_free(Buffer *buffer);
void buffer_clear(Buffer *buffer);

void buffer_append(Buffer *buffer, const char *bytes, size_t length);
void buffer_append_str(Buffer *buffer, const char *text);
void buffer_append_char(Buffer *buffer, char c);
void buffer_appendf(Buffer *buffer, const char *format, ...)
GENESIS_PRINTF(2, 3);
void buffer_vappendf(Buffer *buffer, const char *format, va_list args);

/* Returns the buffer contents, or "" when the buffer is empty. */
const char *buffer_cstr(const Buffer *buffer);

#endif /* GENESIS_BUFFER_H */
