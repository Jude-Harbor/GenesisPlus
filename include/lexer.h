#ifndef GENESIS_LEXER_H
#define GENESIS_LEXER_H

#include <stddef.h>

/*
 * The Genesis+ lexer.
 *
 * Whitespace (spaces, tabs, newlines in any style) only separates tokens; it
 * never carries meaning. Blocks are delimited by '{' and '}'.
 *
 * Tokens point into the source buffer; they do not own memory. The source
 * buffer must outlive every token produced from it.
 */

typedef enum {
    TOKEN_EOF,
    TOKEN_ERROR,

    TOKEN_LBRACE,     /* { */
    TOKEN_RBRACE,     /* } */
    TOKEN_EQUAL,      /* = */
    TOKEN_SEMICOLON,  /* ; (optional statement separator) */
    TOKEN_COLON,      /* : (legacy syntax; only used for a helpful error) */

    TOKEN_STRING,     /* "text" (token includes the quotes) */
    TOKEN_NUMBER,     /* 12 or 12.5 */
    TOKEN_PERCENTAGE, /* 50% or 33.33% */
    TOKEN_IDENTIFIER,
    TOKEN_COMMENT,    /* # ... (only produced when keep_comments is set) */

    /* Keywords. */
    TOKEN_PAGE,
    TOKEN_COLUMN,
    TOKEN_ROW,
    TOKEN_HEADING,
    TOKEN_TEXT,
    TOKEN_LINK,
    TOKEN_BUTTON,
    TOKEN_IMAGE
} TokenType;

typedef enum {
    LEX_OK,
    LEX_UNEXPECTED_CHARACTER,
    LEX_UNTERMINATED_STRING,
    LEX_INVALID_ESCAPE,
    LEX_CONTROL_CHARACTER_IN_STRING,
    LEX_CURLY_QUOTE,
    LEX_NUMBER_WITH_UNIT,
    LEX_MALFORMED_NUMBER,
    LEX_NEGATIVE_NUMBER,
    LEX_NUL_BYTE
} LexError;

typedef struct {
    TokenType type;
    const char *start;
    size_t length;  /* in bytes */
    size_t line;    /* 1-based */
    size_t column;  /* 1-based, in Unicode code points */
    size_t width;   /* length in code points, for underlining */
    LexError error; /* set when type == TOKEN_ERROR */
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t current;
    size_t line;
    size_t column;
    int keep_comments;
} Lexer;

void lexer_init(Lexer *lexer, const char *source, size_t length);
Token lexer_next_token(Lexer *lexer);

/* Human-readable description of a token type, e.g. "'{'" or "a number". */
const char *token_type_describe(TokenType type);

/* Returns 1 for keyword token types (page, column, ...). */
int token_is_keyword(TokenType type);

#endif /* GENESIS_LEXER_H */
