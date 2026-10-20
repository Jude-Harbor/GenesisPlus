#ifndef GENESIS_COMPILER_H
#define GENESIS_COMPILER_H

#include <stddef.h>

#include "ast.h"
#include "buffer.h"
#include "diag.h"

/* The default entry file looked for in a project folder. */
#define GENESIS_DEFAULT_ENTRY "lead.gns"

typedef struct {
    char *name; /* file name inside dist/, e.g. "index.html" */
    Buffer content;
} OutputFile;

/*
 * One compilation of one source file. Owns everything it points to;
 * release it with compilation_free().
 */
typedef struct {
    DiagList diags;
    AstNode *program;
    OutputFile *files;
    size_t file_count;
} Compilation;

/*
 * Resolves the entry file from a command-line argument: NULL means
 * "lead.gns in the current folder", a folder means "lead.gns inside it",
 * anything else is used as a file path. On failure appends a message to
 * `error` and returns 0.
 */
int compiler_resolve_entry(const char *argument, Buffer *entry, Buffer *error);

/*
 * Parses, validates and generates output for `source`, entirely in memory.
 * `display_name` is used in diagnostics. `project_dir` is used to check
 * that local images exist; pass NULL to skip that check.
 * Returns 1 when there are no errors. `out` must be released with
 * compilation_free() in every case.
 */
int compiler_compile(Compilation *out, const char *display_name, const char *source,
                     size_t length, const char *project_dir);

void compilation_free(Compilation *compilation);

/*
 * Writes a successful compilation to `<project_dir>/dist/` and copies
 * `<project_dir>/assets/` to `dist/assets/` if it exists. Only files that
 * the build produces are created or replaced; nothing else in dist/ is
 * touched. On failure appends a message to `error` and returns 0.
 */
int compiler_write_output(const Compilation *compilation, const char *project_dir,
                          Buffer *error);

/*
 * The complete `genesis build` workflow used by the CLI and dev server.
 * Human-readable progress and diagnostics are appended to `log`.
 * Returns 1 on success.
 */
int compiler_build(const char *entry_path, Buffer *log, int quiet);

#endif /* GENESIS_COMPILER_H */
