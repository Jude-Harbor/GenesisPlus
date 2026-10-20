#ifndef GENESIS_DIAG_H
#define GENESIS_DIAG_H

#include <stddef.h>

#include "buffer.h"

/*
 * Diagnostics (errors and warnings) collected while compiling one file.
 *
 * Messages are formatted into fixed-size arrays inside each Diagnostic, so
 * reporting never allocates and a DiagList can be freed simply by letting it
 * go out of scope.
 */

#define DIAG_MAX 32
#define DIAG_MESSAGE_MAX 320
#define DIAG_HINT_MAX 240

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING
} DiagSeverity;

typedef struct {
    DiagSeverity severity;
    size_t line;   /* 1-based */
    size_t column; /* 1-based, counted in Unicode code points */
    size_t length; /* number of code points to underline (at least 1) */
    char message[DIAG_MESSAGE_MAX];
    char hint[DIAG_HINT_MAX];
} Diagnostic;

typedef struct {
    const char *filename;
    const char *source;
    size_t source_length;

    Diagnostic items[DIAG_MAX];
    size_t count;
    size_t error_count;
    size_t warning_count;
    int truncated; /* more diagnostics were reported than could be stored */
} DiagList;

void diag_init(DiagList *list, const char *filename,
               const char *source, size_t source_length);

/* `hint` may be NULL. `message` is a printf-style format string. */
void diag_report(DiagList *list, DiagSeverity severity,
                 size_t line, size_t column, size_t length,
                 const char *hint, const char *message, ...)
GENESIS_PRINTF(7, 8);

/* Human-readable output with the offending source line and a caret. */
void diag_format(const DiagList *list, Buffer *out);

/*
 * Machine-readable output for editor integrations. One diagnostic per line:
 *   severity<TAB>line<TAB>column<TAB>length<TAB>message
 * where severity is "error" or "warning". Tabs and newlines inside messages
 * are replaced with spaces. A hint, if present, is appended to the message.
 */
void diag_format_machine(const DiagList *list, Buffer *out);

#endif /* GENESIS_DIAG_H */
