#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void diag_init(DiagList *list, const char *filename,
               const char *source, size_t source_length)
{
    memset(list, 0, sizeof(*list));
    list->filename = filename != NULL ? filename : "<input>";
    list->source = source;
    list->source_length = source_length;
}

void diag_report(DiagList *list, DiagSeverity severity,
                 size_t line, size_t column, size_t length,
                 const char *hint, const char *message, ...)
{
    if (severity == DIAG_ERROR) {
        list->error_count++;
    } else {
        list->warning_count++;
    }

    if (list->count >= DIAG_MAX) {
        list->truncated = 1;
        return;
    }

    Diagnostic *d = &list->items[list->count++];
    d->severity = severity;
    d->line = line == 0 ? 1 : line;
    d->column = column == 0 ? 1 : column;
    d->length = length == 0 ? 1 : length;

    va_list args;
    va_start(args, message);
    vsnprintf(d->message, sizeof(d->message), message, args);
    va_end(args);

    if (hint != NULL) {
        snprintf(d->hint, sizeof(d->hint), "%s", hint);
    } else {
        d->hint[0] = '\0';
    }
}

/* Finds the start of 1-based line `line`, or NULL if it does not exist. */
static const char *find_line(const DiagList *list, size_t line,
                             size_t *line_length)
{
    if (list->source == NULL) {
        return NULL;
    }

    const char *p = list->source;
    const char *end = list->source + list->source_length;
    size_t current = 1;

    while (current < line && p < end) {
        if (*p == '\n') {
            current++;
        } else if (*p == '\r') {
            current++;
            if (p + 1 < end && p[1] == '\n') {
                p++;
            }
        }
        p++;
    }

    if (current != line || p > end) {
        return NULL;
    }

    const char *stop = p;
    while (stop < end && *stop != '\n' && *stop != '\r') {
        stop++;
    }

    *line_length = (size_t)(stop - p);
    return p;
}

static int is_continuation_byte(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

static void format_source_excerpt(const DiagList *list, const Diagnostic *d,
                                  Buffer *out)
{
    size_t line_length = 0;
    const char *line = find_line(list, d->line, &line_length);

    if (line == NULL) {
        return;
    }

    /* Very long lines are not worth echoing in full. */
    if (line_length > 200) {
        return;
    }

    char number[32];
    int width = snprintf(number, sizeof(number), "%zu", d->line);

    buffer_appendf(out, "\n    %s | ", number);

    for (size_t i = 0; i < line_length; i++) {
        unsigned char c = (unsigned char)line[i];
        /* Keep the excerpt printable: control characters become '?'. */
        if (c < 0x20 && c != '\t') {
            buffer_append_char(out, '?');
        } else {
            buffer_append_char(out, (char)c);
        }
    }

    buffer_appendf(out, "\n    %*s | ", width, "");

    /* Walk to the diagnostic column, mirroring tabs so the caret lines up. */
    size_t column = 1;
    size_t i = 0;

    while (i < line_length && column < d->column) {
        unsigned char c = (unsigned char)line[i];

        if (!is_continuation_byte(c)) {
            buffer_append_char(out, c == '\t' ? '\t' : ' ');
            column++;
        }
        i++;
        while (i < line_length && is_continuation_byte((unsigned char)line[i])) {
            i++;
        }
    }

    size_t carets = d->length;
    if (carets > 80) {
        carets = 80;
    }

    for (size_t k = 0; k < carets; k++) {
        buffer_append_char(out, '^');
    }

    buffer_append_char(out, '\n');
}

void diag_format(const DiagList *list, Buffer *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const Diagnostic *d = &list->items[i];

        buffer_appendf(out, "Genesis+ %s in %s at line %zu, column %zu:\n  %s\n",
                       d->severity == DIAG_ERROR ? "error" : "warning",
                       list->filename, d->line, d->column, d->message);

        format_source_excerpt(list, d, out);

        if (d->hint[0] != '\0') {
            buffer_appendf(out, "  Hint: %s\n", d->hint);
        }

        buffer_append_char(out, '\n');
    }

    if (list->truncated) {
        buffer_appendf(out, "Genesis+: too many problems; only the first %d are shown.\n\n",
                       DIAG_MAX);
    }
}

static void append_sanitized(Buffer *out, const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        char c = *p;
        buffer_append_char(out, (c == '\t' || c == '\n' || c == '\r') ? ' ' : c);
    }
}

void diag_format_machine(const DiagList *list, Buffer *out)
{
    for (size_t i = 0; i < list->count; i++) {
        const Diagnostic *d = &list->items[i];

        buffer_appendf(out, "%s\t%zu\t%zu\t%zu\t",
                       d->severity == DIAG_ERROR ? "error" : "warning",
                       d->line, d->column, d->length);
        append_sanitized(out, d->message);

        if (d->hint[0] != '\0') {
            buffer_append_str(out, " Hint: ");
            append_sanitized(out, d->hint);
        }

        buffer_append_char(out, '\n');
    }
}
