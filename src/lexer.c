#include "lexer.h"

#include <string.h>

void lexer_init(Lexer *lexer, const char *source, size_t length)
{
    lexer->source = source;
    lexer->length = length;
    lexer->current = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->keep_comments = 0;

    /* Skip a UTF-8 byte order mark, which some Windows editors add. */
    if (length >= 3 &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        lexer->current = 3;
    }
}

static int is_at_end(const Lexer *lexer)
{
    return lexer->current >= lexer->length;
}

static unsigned char peek_at(const Lexer *lexer, size_t offset)
{
    if (lexer->current + offset >= lexer->length) {
        return '\0';
    }
    return (unsigned char)lexer->source[lexer->current + offset];
}

static unsigned char peek(const Lexer *lexer)
{
    return peek_at(lexer, 0);
}

static int is_continuation_byte(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

/* Advances one byte, keeping line and column (in code points) up to date. */
static void advance(Lexer *lexer)
{
    unsigned char c = (unsigned char)lexer->source[lexer->current++];

    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else if (c == '\r') {
        /* CRLF is counted once, by its '\n'. A lone CR is a line break. */
        if (peek(lexer) != '\n') {
            lexer->line++;
            lexer->column = 1;
        }
    } else if (!is_continuation_byte(c)) {
        lexer->column++;
    }
}

static int is_letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

/*
 * Returns the byte length of an invisible Unicode space at the current
 * position (non-breaking space, zero-width space, or a stray byte order
 * mark), or 0. These often sneak in when code is copied from web pages or
 * chat applications, and they are invisible, so we treat them as spaces.
 */
static size_t unicode_space_length(const Lexer *lexer)
{
    unsigned char a = peek_at(lexer, 0);
    unsigned char b = peek_at(lexer, 1);
    unsigned char c = peek_at(lexer, 2);

    if (a == 0xC2 && b == 0xA0) {
        return 2; /* U+00A0 NO-BREAK SPACE */
    }
    if (a == 0xE2 && b == 0x80 && (c == 0x8B || c == 0xAF)) {
        return 3; /* U+200B ZERO WIDTH SPACE, U+202F NARROW NO-BREAK SPACE */
    }
    if (a == 0xEF && b == 0xBB && c == 0xBF) {
        return 3; /* U+FEFF BYTE ORDER MARK */
    }
    return 0;
}

/* Returns 1 if a curly quotation mark (“ ” ‘ ’) starts at the current position. */
static int at_curly_quote(const Lexer *lexer)
{
    return peek_at(lexer, 0) == 0xE2 && peek_at(lexer, 1) == 0x80 &&
           (peek_at(lexer, 2) == 0x9C || peek_at(lexer, 2) == 0x9D ||
            peek_at(lexer, 2) == 0x98 || peek_at(lexer, 2) == 0x99);
}

static void skip_whitespace(Lexer *lexer)
{
    while (!is_at_end(lexer)) {
        unsigned char c = peek(lexer);

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '\f' || c == '\v') {
            advance(lexer);
            continue;
        }

        size_t unicode = unicode_space_length(lexer);
        if (unicode > 0) {
            for (size_t i = 0; i < unicode; i++) {
                advance(lexer);
            }
            continue;
        }

        break;
    }
}

static size_t count_code_points(const char *start, size_t length)
{
    size_t count = 0;
    for (size_t i = 0; i < length; i++) {
        if (!is_continuation_byte((unsigned char)start[i])) {
            count++;
        }
    }
    return count;
}

typedef struct {
    size_t offset;
    size_t line;
    size_t column;
} TokenStart;

static TokenStart mark(const Lexer *lexer)
{
    TokenStart start;
    start.offset = lexer->current;
    start.line = lexer->line;
    start.column = lexer->column;
    return start;
}

static Token make_token(const Lexer *lexer, TokenType type, TokenStart start)
{
    Token token;
    token.type = type;
    token.start = lexer->source + start.offset;
    token.length = lexer->current - start.offset;
    token.line = start.line;
    token.column = start.column;
    token.width = count_code_points(token.start, token.length);
    token.error = LEX_OK;
    return token;
}

static Token make_error(const Lexer *lexer, LexError error, TokenStart start)
{
    Token token = make_token(lexer, TOKEN_ERROR, start);
    token.error = error;
    if (token.width == 0) {
        token.width = 1;
    }
    return token;
}

static TokenType identify_word(const char *word, size_t length)
{
    static const struct {
        const char *text;
        TokenType type;
    } keywords[] = {
        {"page", TOKEN_PAGE},       {"column", TOKEN_COLUMN},
        {"row", TOKEN_ROW},         {"heading", TOKEN_HEADING},
        {"text", TOKEN_TEXT},       {"link", TOKEN_LINK},
        {"button", TOKEN_BUTTON},   {"image", TOKEN_IMAGE},
    };

    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strlen(keywords[i].text) == length &&
            memcmp(keywords[i].text, word, length) == 0) {
            return keywords[i].type;
        }
    }

    return TOKEN_IDENTIFIER;
}

static Token scan_string(Lexer *lexer, TokenStart start)
{
    advance(lexer); /* opening quote */

    for (;;) {
        if (is_at_end(lexer) || peek(lexer) == '\n' || peek(lexer) == '\r') {
            return make_error(lexer, LEX_UNTERMINATED_STRING, start);
        }

        unsigned char c = peek(lexer);

        if (c == '"') {
            advance(lexer);
            return make_token(lexer, TOKEN_STRING, start);
        }

        if (c == '\\') {
            TokenStart escape = mark(lexer);
            advance(lexer);
            unsigned char next = peek(lexer);

            if (next == '"' || next == '\\') {
                advance(lexer);
                continue;
            }

            /* Point the error at the bad escape itself. */
            if (!is_at_end(lexer) && next != '\n' && next != '\r') {
                advance(lexer);
                while (!is_at_end(lexer) && is_continuation_byte(peek(lexer))) {
                    advance(lexer);
                }
            }
            return make_error(lexer, LEX_INVALID_ESCAPE, escape);
        }

        if (c == '\0') {
            TokenStart bad = mark(lexer);
            advance(lexer);
            return make_error(lexer, LEX_NUL_BYTE, bad);
        }

        if (c < 0x20 && c != '\t') {
            TokenStart bad = mark(lexer);
            advance(lexer);
            return make_error(lexer, LEX_CONTROL_CHARACTER_IN_STRING, bad);
        }

        advance(lexer);
    }
}

static Token scan_number(Lexer *lexer, TokenStart start)
{
    while (is_digit(peek(lexer))) {
        advance(lexer);
    }

    if (peek(lexer) == '.') {
        if (!is_digit(peek_at(lexer, 1))) {
            advance(lexer);
            return make_error(lexer, LEX_MALFORMED_NUMBER, start);
        }

        advance(lexer);
        while (is_digit(peek(lexer))) {
            advance(lexer);
        }

        if (peek(lexer) == '.') {
            while (peek(lexer) == '.' || is_digit(peek(lexer))) {
                advance(lexer);
            }
            return make_error(lexer, LEX_MALFORMED_NUMBER, start);
        }
    }

    TokenType type = TOKEN_NUMBER;

    if (peek(lexer) == '%') {
        advance(lexer);
        type = TOKEN_PERCENTAGE;
    }

    /* Catch CSS habits such as 16px or 2rem. */
    if (is_letter(peek(lexer))) {
        while (is_letter(peek(lexer)) || is_digit(peek(lexer))) {
            advance(lexer);
        }
        return make_error(lexer, LEX_NUMBER_WITH_UNIT, start);
    }

    return make_token(lexer, type, start);
}

Token lexer_next_token(Lexer *lexer)
{
    for (;;) {
        skip_whitespace(lexer);

        TokenStart start = mark(lexer);

        if (is_at_end(lexer)) {
            return make_token(lexer, TOKEN_EOF, start);
        }

        unsigned char c = peek(lexer);

        if (c == '#') {
            while (!is_at_end(lexer) && peek(lexer) != '\n' && peek(lexer) != '\r') {
                advance(lexer);
            }
            if (lexer->keep_comments) {
                return make_token(lexer, TOKEN_COMMENT, start);
            }
            continue;
        }

        switch (c) {
        case '{':
            advance(lexer);
            return make_token(lexer, TOKEN_LBRACE, start);
        case '}':
            advance(lexer);
            return make_token(lexer, TOKEN_RBRACE, start);
        case '=':
            advance(lexer);
            return make_token(lexer, TOKEN_EQUAL, start);
        case ';':
            advance(lexer);
            return make_token(lexer, TOKEN_SEMICOLON, start);
        case ':':
            advance(lexer);
            return make_token(lexer, TOKEN_COLON, start);
        case '"':
            return scan_string(lexer, start);
        case '\0':
            advance(lexer);
            return make_error(lexer, LEX_NUL_BYTE, start);
        default:
            break;
        }

        if (is_digit(c)) {
            return scan_number(lexer, start);
        }

        if (c == '-' && is_digit(peek_at(lexer, 1))) {
            advance(lexer);
            Token number = scan_number(lexer, start);
            if (number.type == TOKEN_ERROR) {
                return number;
            }
            return make_error(lexer, LEX_NEGATIVE_NUMBER, start);
        }

        if (is_letter(c)) {
            while (is_letter(peek(lexer)) || is_digit(peek(lexer))) {
                advance(lexer);
            }
            Token token = make_token(lexer, TOKEN_IDENTIFIER, start);
            token.type = identify_word(token.start, token.length);
            return token;
        }

        if (at_curly_quote(lexer)) {
            advance(lexer);
            advance(lexer);
            advance(lexer);
            return make_error(lexer, LEX_CURLY_QUOTE, start);
        }

        /* Unexpected character: consume one whole UTF-8 code point. */
        advance(lexer);
        while (!is_at_end(lexer) && is_continuation_byte(peek(lexer))) {
            advance(lexer);
        }
        return make_error(lexer, LEX_UNEXPECTED_CHARACTER, start);
    }
}

const char *token_type_describe(TokenType type)
{
    switch (type) {
    case TOKEN_EOF:        return "the end of the file";
    case TOKEN_ERROR:      return "an invalid token";
    case TOKEN_LBRACE:     return "'{'";
    case TOKEN_RBRACE:     return "'}'";
    case TOKEN_EQUAL:      return "'='";
    case TOKEN_SEMICOLON:  return "';'";
    case TOKEN_COLON:      return "':'";
    case TOKEN_STRING:     return "a quoted string";
    case TOKEN_NUMBER:     return "a number";
    case TOKEN_PERCENTAGE: return "a percentage";
    case TOKEN_IDENTIFIER: return "a name";
    case TOKEN_COMMENT:    return "a comment";
    case TOKEN_PAGE:       return "'page'";
    case TOKEN_COLUMN:     return "'column'";
    case TOKEN_ROW:        return "'row'";
    case TOKEN_HEADING:    return "'heading'";
    case TOKEN_TEXT:       return "'text'";
    case TOKEN_LINK:       return "'link'";
    case TOKEN_BUTTON:     return "'button'";
    case TOKEN_IMAGE:      return "'image'";
    }
    return "a token";
}

int token_is_keyword(TokenType type)
{
    return type >= TOKEN_PAGE && type <= TOKEN_IMAGE;
}
