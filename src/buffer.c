#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void buffer_init(Buffer *buffer)
{
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->failed = 0;
}

void buffer_free(Buffer *buffer)
{
    free(buffer->data);
    buffer_init(buffer);
}

void buffer_clear(Buffer *buffer)
{
    buffer->length = 0;
    buffer->failed = 0;

    if (buffer->data != NULL) {
        buffer->data[0] = '\0';
    }
}

static int buffer_reserve(Buffer *buffer, size_t extra)
{
    if (buffer->failed) {
        return 0;
    }

    /* +1 keeps room for the terminating NUL. */
    if (extra > (size_t)-1 - buffer->length - 1) {
        buffer->failed = 1;
        return 0;
    }

    size_t needed = buffer->length + extra + 1;

    if (needed <= buffer->capacity) {
        return 1;
    }

    size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;

    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }

    char *data = realloc(buffer->data, capacity);

    if (data == NULL) {
        buffer->failed = 1;
        return 0;
    }

    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

void buffer_append(Buffer *buffer, const char *bytes, size_t length)
{
    if (length == 0) {
        /* Still make sure data is a valid C string. */
        if (buffer->data == NULL) {
            buffer_reserve(buffer, 0);
            if (buffer->data != NULL) {
                buffer->data[0] = '\0';
            }
        }
        return;
    }

    if (!buffer_reserve(buffer, length)) {
        return;
    }

    memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

void buffer_append_str(Buffer *buffer, const char *text)
{
    buffer_append(buffer, text, strlen(text));
}

void buffer_append_char(Buffer *buffer, char c)
{
    buffer_append(buffer, &c, 1);
}

void buffer_vappendf(Buffer *buffer, const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);

    if (needed < 0) {
        buffer->failed = 1;
        return;
    }

    if (!buffer_reserve(buffer, (size_t)needed)) {
        return;
    }

    vsnprintf(buffer->data + buffer->length, (size_t)needed + 1, format, args);
    buffer->length += (size_t)needed;
}

void buffer_appendf(Buffer *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    buffer_vappendf(buffer, format, args);
    va_end(args);
}

const char *buffer_cstr(const Buffer *buffer)
{
    return buffer->data != NULL ? buffer->data : "";
}
