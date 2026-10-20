#ifndef GENESIS_CODEGEN_H
#define GENESIS_CODEGEN_H

#include "ast.h"
#include "buffer.h"

/*
 * Code generation. Output is written to memory buffers so that nothing is
 * written to disk unless the whole build succeeds. Output depends only on
 * the AST (never on source formatting) and is byte-for-byte deterministic.
 */

/* Appends the complete HTML document for one AST_PAGE node. */
void codegen_page_html(const AstNode *page, Buffer *out);

/* Appends the shared stylesheet (dist/style.css). It is the same for every
   project; per-element styling is emitted inline in the HTML. */
void codegen_css(Buffer *out);

/* Appends `text` with &, <, >, " and ' escaped for HTML text or attributes. */
void codegen_escape_html(Buffer *out, const char *text);

#endif /* GENESIS_CODEGEN_H */
