# Compiler architecture

This document is for people working on the compiler itself.

## Pipeline

```
source bytes
   │  lexer.c      tokens (whitespace and comments dropped; braces are tokens)
   ▼
tokens
   │  parser.c     recursive descent + validation against a property table
   ▼
AST (ast.h)       owned tree of nodes with validated, typed values
   │  compiler.c   semantic checks that need the file system (images exist)
   │  codegen.c    HTML for each page + the shared stylesheet, into memory
   ▼
output buffers
   │  compiler.c / platform.c   atomic writes into dist/, copy assets/
   ▼
dist/
```

## Source layout

| File | Responsibility |
| --- | --- |
| `src/main.c` | Command-line parsing and the `build`, `check`, `fmt`, `dev`, `init` commands. |
| `src/lexer.c` | Tokens, line/column tracking (columns in code points), error tokens for malformed input. |
| `src/parser.c` | Grammar, property table (`PROPERTIES`), value validation, diagnostics, page file names. |
| `src/ast.c` | Node allocation, tree links, and freeing. |
| `src/codegen.c` | HTML and CSS generation, HTML escaping. |
| `src/compiler.c` | Orchestration: entry-file resolution, image checks, output writing. |
| `src/formatter.c` | `genesis fmt`, a token-based pretty printer. |
| `src/server.c` | `genesis dev`: HTTP server, safe path resolution, change polling, live reload. |
| `src/platform.c` | The only file-system code: directories, atomic replace, recursive copy, timestamps. |
| `src/diag.c` | Collecting and printing diagnostics (human and machine formats). |
| `src/buffer.c` | Growable string buffer used everywhere output is built. |

Everything except `platform.c` and `server.c` is plain ISO C17 using only the standard library. Those two files contain the Windows (`_WIN32`) and POSIX code paths.

## Memory ownership rules

1. **Tokens never own memory.** A `Token` points into the source buffer. The source must outlive the lexer and parser, but **not** the AST.
2. **The AST owns all of its strings.** Every `char *` field in an `AstNode` (`value`, `href`, `alt`, `title`, `file_name`) is either `NULL` or a heap string owned by that node. String values are decoded (escapes resolved) into new allocations.
3. **Colors are stored inline** (`AstColor`, a fixed `char[8]`) after validation, so they need no freeing and can only ever contain `#RGB` or `#RRGGBB`.
4. **Nodes are attached immediately.** The parser calls `ast_add_child` right after creating a node, before parsing anything inside it. Therefore any error path can simply stop: the partially built node is already part of the tree, and `ast_free(root)` releases everything. There is no separate cleanup on error paths.
5. **The caller owns the root.** `parse_program` always returns the tree (unless memory runs out), even when errors were reported; the caller frees it with `ast_free`.
6. **`ast_free` is iterative over siblings and recursive over depth.** Depth is limited to `PARSER_MAX_DEPTH` (64), so recursion is bounded.
7. **`Compilation` owns everything it points to** (AST, output buffers, file names) and is released with `compilation_free`.
8. **`Buffer` never aborts.** If an allocation fails, it sets `failed` and ignores later appends. Code that builds output checks `failed` once at the end.
9. **Diagnostics never allocate.** Messages are formatted into fixed arrays inside `DiagList`.

## Error handling strategy

- **Value errors** (out-of-range numbers, bad colors, unknown or duplicate properties) are reported and parsing continues, so several can be fixed in one go.
- **Structural errors** (missing or extra braces, unexpected tokens, lexer errors) set `parser->failed` and the parser unwinds immediately. This avoids cascades of misleading follow-up errors.
- Every loop in the parser either consumes a token or exits on `failed` or end-of-file, so malformed input cannot cause an infinite loop. The fuzz tests check this with tens of thousands of random and mutated inputs.
- Errors point at the token that is actually wrong (the bad value, the unclosed `{`, the misplaced keyword), not wherever the parser happened to be.

## Code generation

- Layout and text styles are emitted as inline `style` attributes. This keeps the stylesheet identical for every project and makes each element's styling explicit. Only numbers and validated colors are ever written into `style`, so user text cannot inject CSS.
- Padding declarations are always emitted from least to most specific (`padding`, `padding-block`, `padding-inline`, then individual sides), so the most specific value wins regardless of source order.
- Widths are stored as hundredths of a percent (`int`), never as floating point, so output is exact and does not depend on locale.
- All text and attribute values go through `codegen_escape_html`, which escapes `& < > " '`.
- The responsive behaviour (stacking rows below 640 px) lives in the static stylesheet, using `!important` only to override inline widths inside stacked rows.

## Adding a property

1. Add an entry to `PropertyId` and `PROPERTIES` in `src/parser.c` (name, allowed elements, value kind, range, example).
2. Add a field to `AstNode` if needed, and set it in `apply_property`.
3. Emit it in `src/codegen.c`.
4. Update `vscode-extension/lib/language.js` and the grammar. The extension tests compile every listed combination with the real compiler and fail if the two disagree.
5. Add parser, codegen and (if output changes) golden tests, and update `docs/language-reference.md`.

## Known static-analysis notes

`make analyze` (GCC `-fanalyzer`) reports three warnings in `src/server.c` on Windows (`-Wanalyzer-fd-leak`, `-Wanalyzer-fd-use-without-check`). They are false positives: the analyzer models sockets as POSIX file descriptors (valid when `>= 0`), while Winsock sockets are unsigned values that are correctly checked against `INVALID_SOCKET`. The listener is validated before use, and every accepted client socket is closed after its request is handled.
