#ifndef GENESIS_PARSER_H
#define GENESIS_PARSER_H

#include <stddef.h>

#include "ast.h"
#include "diag.h"

/* Maximum nesting depth of { } blocks. Deeper input is rejected. */
#define PARSER_MAX_DEPTH 64

/*
 * Parses and validates a complete Genesis+ source file.
 *
 * Always returns a tree rooted at an AST_PROGRAM node (or NULL if memory ran
 * out). The tree is only meaningful when diags->error_count == 0. The caller
 * owns the returned tree and must release it with ast_free(), whether or not
 * errors were reported.
 */
AstNode *parse_program(const char *source, size_t length, DiagList *diags);

#endif /* GENESIS_PARSER_H */
