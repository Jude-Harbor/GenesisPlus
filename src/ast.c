#include "ast.h"

#include <stdlib.h>

AstNode *ast_create_node(AstNodeType type, size_t line, size_t column)
{
    AstNode *node = calloc(1, sizeof(AstNode));

    if (node == NULL) {
        return NULL;
    }

    node->type = type;
    node->line = line;
    node->column = column;

    return node;
}

void ast_add_child(AstNode *parent, AstNode *child)
{
    if (parent == NULL || child == NULL) {
        return;
    }

    child->parent = parent;

    if (parent->last_child == NULL) {
        parent->first_child = child;
    } else {
        parent->last_child->next_sibling = child;
    }

    parent->last_child = child;
}

void ast_free(AstNode *node)
{
    /* Iterative over siblings, recursive over depth. Depth is bounded by the
       parser's nesting limit, so recursion cannot overflow the stack. */
    while (node != NULL) {
        AstNode *next = node->next_sibling;

        ast_free(node->first_child);

        free(node->value);
        free(node->href);
        free(node->alt);
        free(node->title);
        free(node->file_name);
        free(node);

        node = next;
    }
}

const char *ast_type_name(AstNodeType type)
{
    switch (type) {
    case AST_PROGRAM: return "program";
    case AST_PAGE:    return "page";
    case AST_COLUMN:  return "column";
    case AST_ROW:     return "row";
    case AST_HEADING: return "heading";
    case AST_TEXT:    return "text";
    case AST_LINK:    return "link";
    case AST_BUTTON:  return "button";
    case AST_IMAGE:   return "image";
    }
    return "element";
}

int ast_is_layout(AstNodeType type)
{
    return type == AST_COLUMN || type == AST_ROW;
}

int ast_child_count(const AstNode *node)
{
    int count = 0;
    for (const AstNode *child = node->first_child; child != NULL;
         child = child->next_sibling) {
        count++;
    }
    return count;
}
