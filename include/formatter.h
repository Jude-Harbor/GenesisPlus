#ifndef GENESIS_FORMATTER_H
#define GENESIS_FORMATTER_H

#include <stddef.h>

#include "buffer.h"
#include "diag.h"

/*
 * Formats Genesis+ source in the canonical style: one element per line,
 * four-space indentation per block, '{' at the end of the opening line,
 * '}' on its own line, single spaces around '='. Comments are kept, and up
 * to one blank line between statements is preserved. Line endings follow
 * the input (CRLF if the first line break is CRLF, LF otherwise).
 *
 * The source is parsed first; if it has errors, nothing is written to
 * `out`, the errors are reported to `diags`, and 0 is returned. Formatting
 * never changes the meaning of a program.
 */
int format_source(const char *source, size_t length, Buffer *out, DiagList *diags);

#endif /* GENESIS_FORMATTER_H */
