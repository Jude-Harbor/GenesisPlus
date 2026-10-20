#include "formatter.h"

#include "ast.h"
#include "lexer.h"
#include "parser.h"

static int starts_statement(TokenType type)
{
    return token_is_keyword(type);
}

static void newline(Buffer *out, const char *eol)
{
    buffer_append_str(out, eol);
}

static void indent(Buffer *out, int depth)
{
    for (int i = 0; i < depth; i++) {
        buffer_append_str(out, "    ");
    }
}

static const char *detect_line_ending(const char *source, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if (source[i] == '\n') {
            return (i > 0 && source[i - 1] == '\r') ? "\r\n" : "\n";
        }
    }
    return "\n";
}

int format_source(const char *source, size_t length, Buffer *out, DiagList *diags)
{
    AstNode *program = parse_program(source, length, diags);
    ast_free(program);

    if (program == NULL || diags->error_count > 0) {
        return 0;
    }

    const char *eol = detect_line_ending(source, length);

    Lexer lexer;
    lexer_init(&lexer, source, length);
    lexer.keep_comments = 1;

    int depth = 0;
    int at_line_start = 1;   /* nothing written on the current output line yet */
    int wrote_anything = 0;
    size_t last_line = 0;    /* source line of the previous token */
    TokenType last_type = TOKEN_EOF;
    TokenType last_code_type = TOKEN_EOF; /* previous token, ignoring comments */
    int after_open_brace = 0; /* nothing but a trailing comment since '{' */

    for (;;) {
        Token token = lexer_next_token(&lexer);

        if (token.type == TOKEN_EOF) {
            break;
        }

        if (token.type == TOKEN_SEMICOLON) {
            continue;
        }

        int own_line;

        if (token.type == TOKEN_COMMENT) {
            /* A comment on the same source line as the previous token stays
               a trailing comment; otherwise it gets its own line. */
            own_line = !(wrote_anything && token.line == last_line && !at_line_start);
        } else {
            own_line = starts_statement(token.type) || token.type == TOKEN_RBRACE;
        }

        if (token.type == TOKEN_RBRACE && last_type == TOKEN_LBRACE) {
            /* Empty block: write "{}" on one line. */
            depth--;
            buffer_append_char(out, '}');
            at_line_start = 0;
            after_open_brace = 0;
            last_line = token.line;
            last_type = token.type;
            last_code_type = token.type;
            continue;
        }

        if (own_line) {
            if (token.type == TOKEN_RBRACE) {
                depth--;
            }

            if (wrote_anything) {
                if (!at_line_start) {
                    newline(out, eol);
                }

                int blank = token.line > last_line + 1;

                /* Always separate one page from the next by a blank line. */
                if (depth == 0 && token.type == TOKEN_PAGE &&
                    last_code_type == TOKEN_RBRACE && last_type != TOKEN_COMMENT) {
                    blank = 1;
                }

                /* No blank lines just inside '{' or just before '}'. */
                if (after_open_brace || token.type == TOKEN_RBRACE) {
                    blank = 0;
                }

                if (blank) {
                    newline(out, eol);
                }
            }

            indent(out, depth < 0 ? 0 : depth);
            at_line_start = 0;
        } else if (!at_line_start) {
            buffer_append_str(out, token.type == TOKEN_COMMENT ? "  " : " ");
        } else {
            indent(out, depth < 0 ? 0 : depth);
            at_line_start = 0;
        }

        buffer_append(out, token.start, token.length);
        wrote_anything = 1;

        if (token.type == TOKEN_LBRACE) {
            depth++;
        }

        /* Anything after a comment must start on a new line. */
        if (token.type == TOKEN_COMMENT) {
            newline(out, eol);
            at_line_start = 1;
        }

        if (token.type == TOKEN_LBRACE) {
            after_open_brace = 1;
        } else if (!(token.type == TOKEN_COMMENT && !own_line)) {
            after_open_brace = 0;
        }

        if (token.type != TOKEN_COMMENT) {
            last_code_type = token.type;
        }
        last_line = token.line;
        last_type = token.type;
    }

    if (wrote_anything && !at_line_start) {
        newline(out, eol);
    }

    return !out->failed;
}
