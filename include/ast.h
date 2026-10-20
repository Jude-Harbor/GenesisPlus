#ifndef GENESIS_AST_H
#define GENESIS_AST_H

#include <stddef.h>

/*
 * The Genesis+ abstract syntax tree.
 *
 * Ownership rules (see docs/architecture.md):
 *   - Every AstNode is heap-allocated with ast_create_node() and owned by its
 *     parent. The root (AST_PROGRAM) is owned by whoever called the parser.
 *   - Every `char *` field in a node is a NUL-terminated heap string owned by
 *     that node (or NULL). ast_free() releases them.
 *   - Colors are validated and stored inline in fixed-size arrays, so they
 *     never need freeing and can never contain anything but "#RGB"/"#RRGGBB".
 *   - The tree never points into the source buffer, so the source may be
 *     freed before the tree.
 *   - ast_free(root) releases the whole tree. Nodes must be attached to the
 *     tree (ast_add_child) as soon as they are created so that error paths
 *     never leak.
 */

typedef enum {
    AST_PROGRAM,
    AST_PAGE,
    AST_COLUMN,
    AST_ROW,
    AST_HEADING,
    AST_TEXT,
    AST_LINK,
    AST_BUTTON,
    AST_IMAGE
} AstNodeType;

typedef enum {
    ALIGN_DEFAULT,
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
} AstAlign;

/*
 * Padding properties, listed from least to most specific. Code generation
 * emits them in this order so that a more specific value always wins,
 * whatever order they were written in the source.
 */
typedef enum {
    PAD_ALL,
    PAD_BLOCK,
    PAD_INLINE,
    PAD_TOP,
    PAD_BOTTOM,
    PAD_LEFT,
    PAD_RIGHT,
    PAD_COUNT
} AstPaddingSide;

typedef struct {
    int set;
    char value[8]; /* "#RGB" or "#RRGGBB", NUL-terminated */
} AstColor;

typedef struct AstNode AstNode;

struct AstNode {
    AstNodeType type;
    size_t line;
    size_t column;

    /* Owned strings. */
    char *value;     /* text content, page name, or image source */
    char *href;      /* link and button target */
    char *alt;       /* image description */
    char *title;     /* page <title> */
    char *file_name; /* page output file, e.g. "index.html" */

    /* Text styling. 0 means "not set". */
    int size;  /* font size in px */
    int level; /* heading level 1-6 */
    AstAlign align;
    AstColor color;
    AstColor background;

    /* Layout styling. */
    int gap;
    int has_gap;
    int width_hundredths; /* width in 1/100 of a percent: 5000 == 50% */
    int has_width;
    int padding[PAD_COUNT];
    int has_padding[PAD_COUNT];

    /* Tree structure. */
    AstNode *parent;
    AstNode *first_child;
    AstNode *last_child;
    AstNode *next_sibling;
};

/* Returns NULL when out of memory. */
AstNode *ast_create_node(AstNodeType type, size_t line, size_t column);

void ast_add_child(AstNode *parent, AstNode *child);
void ast_free(AstNode *node);

/* Lowercase user-facing name, e.g. "column". */
const char *ast_type_name(AstNodeType type);

int ast_is_layout(AstNodeType type);
int ast_child_count(const AstNode *node);

#endif /* GENESIS_AST_H */
