#include "lexer.h"
#include "test.h"

/* Lexes `source` and returns up to `max` tokens (stopping after EOF/ERROR). */
static size_t lex_all(const char *source, Token *tokens, size_t max)
{
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    size_t count = 0;

    while (count < max) {
        Token token = lexer_next_token(&lexer);
        tokens[count++] = token;
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }
    return count;
}

static int token_text_is(Token token, const char *text)
{
    return token.length == strlen(text) && memcmp(token.start, text, token.length) == 0;
}

static void test_all_token_types(void)
{
    Token t[32];
    size_t n = lex_all("page column row heading text link button image name_2 "
                       "{ } = ; : \"str\" 12 3.5 50% 33.33%",
                       t, 32);
    TokenType expected[] = {
        TOKEN_PAGE, TOKEN_COLUMN, TOKEN_ROW, TOKEN_HEADING, TOKEN_TEXT, TOKEN_LINK,
        TOKEN_BUTTON, TOKEN_IMAGE, TOKEN_IDENTIFIER, TOKEN_LBRACE, TOKEN_RBRACE,
        TOKEN_EQUAL, TOKEN_SEMICOLON, TOKEN_COLON, TOKEN_STRING, TOKEN_NUMBER,
        TOKEN_NUMBER, TOKEN_PERCENTAGE, TOKEN_PERCENTAGE, TOKEN_EOF,
    };
    CHECK(n == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < n && i < sizeof(expected) / sizeof(expected[0]); i++) {
        CHECK(t[i].type == expected[i]);
    }
    CHECK(token_text_is(t[8], "name_2"));
    CHECK(token_text_is(t[14], "\"str\""));
    CHECK(token_text_is(t[16], "3.5"));
    CHECK(token_text_is(t[18], "33.33%"));
}

static void test_keywords_are_case_sensitive(void)
{
    Token t[4];
    lex_all("Page COLUMN", t, 4);
    CHECK(t[0].type == TOKEN_IDENTIFIER);
    CHECK(t[1].type == TOKEN_IDENTIFIER);
}

static void test_braces_adjacent_to_tokens(void)
{
    Token t[16];
    size_t n = lex_all("page Home{column{text\"a\"}}", t, 16);
    CHECK(n == 10);
    CHECK(t[0].type == TOKEN_PAGE);
    CHECK(t[1].type == TOKEN_IDENTIFIER && token_text_is(t[1], "Home"));
    CHECK(t[2].type == TOKEN_LBRACE);
    CHECK(t[3].type == TOKEN_COLUMN);
    CHECK(t[4].type == TOKEN_LBRACE);
    CHECK(t[5].type == TOKEN_TEXT);
    CHECK(t[6].type == TOKEN_STRING);
    CHECK(t[7].type == TOKEN_RBRACE);
    CHECK(t[8].type == TOKEN_RBRACE);
}

static void test_strings_and_escapes(void)
{
    Token t[4];
    lex_all("\"say \\\"hi\\\" \\\\ ok\"", t, 4);
    CHECK(t[0].type == TOKEN_STRING);
    CHECK(t[1].type == TOKEN_EOF);

    lex_all("\"\"", t, 4);
    CHECK(t[0].type == TOKEN_STRING && t[0].length == 2);

    lex_all("\"tab\tinside\"", t, 4);
    CHECK(t[0].type == TOKEN_STRING);

    lex_all("\"caf\xC3\xA9 \xF0\x9F\x9A\x80\"", t, 4);
    CHECK(t[0].type == TOKEN_STRING);
    CHECK(t[0].width == 8); /* quotes + "café" + space + rocket */

    lex_all("\"bad \\n escape\"", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_INVALID_ESCAPE);
    CHECK(t[0].column == 6);
}

static void test_unterminated_strings(void)
{
    Token t[4];
    lex_all("\"no end", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_UNTERMINATED_STRING);
    CHECK(t[0].line == 1 && t[0].column == 1);

    lex_all("\"line one\nline two\"", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_UNTERMINATED_STRING);

    lex_all("\"crlf\r\n\"", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_UNTERMINATED_STRING);

    lex_all("\"", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_UNTERMINATED_STRING);

    lex_all("\"ends with backslash\\", t, 4);
    CHECK(t[0].type == TOKEN_ERROR);
}

static void test_numbers(void)
{
    Token t[4];
    lex_all("007", t, 4);
    CHECK(t[0].type == TOKEN_NUMBER && t[0].length == 3);

    lex_all("16px", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_NUMBER_WITH_UNIT);
    CHECK(token_text_is(t[0], "16px"));

    lex_all("2rem", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_NUMBER_WITH_UNIT);

    lex_all("12.", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_MALFORMED_NUMBER);

    lex_all("1.2.3", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_MALFORMED_NUMBER);

    lex_all("-5", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_NEGATIVE_NUMBER);

    lex_all("50 %", t, 4);
    CHECK(t[0].type == TOKEN_NUMBER);
    CHECK(t[1].type == TOKEN_ERROR && t[1].error == LEX_UNEXPECTED_CHARACTER);
}

static void test_line_and_column_tracking(void)
{
    Token t[8];
    size_t n = lex_all("page A {\n  text \"x\"\n}", t, 8);
    CHECK(n == 7);
    CHECK(t[3].type == TOKEN_TEXT && t[3].line == 2 && t[3].column == 3);
    CHECK(t[4].type == TOKEN_STRING && t[4].line == 2 && t[4].column == 8);
    CHECK(t[5].type == TOKEN_RBRACE && t[5].line == 3 && t[5].column == 1);

    /* Columns count code points, not bytes. */
    lex_all("\"\xC3\xA9\xC3\xA9\" x", t, 8);
    CHECK(t[1].column == 6);
}

static void test_line_endings(void)
{
    const char *variants[] = {
        "page A {\n text \"x\"\n}\n",
        "page A {\r\n text \"x\"\r\n}\r\n",
        "page A {\r text \"x\"\r}\r",
    };

    for (size_t v = 0; v < 3; v++) {
        Token t[8];
        size_t n = lex_all(variants[v], t, 8);
        CHECK(n == 7);
        CHECK(t[3].line == 2 && t[3].column == 2);
        CHECK(t[5].type == TOKEN_RBRACE && t[5].line == 3 && t[5].column == 1);
        CHECK(t[6].type == TOKEN_EOF);
    }
}

static void test_whitespace_variants(void)
{
    Token t[8];
    /* Tabs, spaces, form feed, non-breaking space, zero-width space. */
    size_t n = lex_all("\t page \f\xC2\xA0 A\xE2\x80\x8B{ }", t, 8);
    CHECK(n == 5);
    CHECK(t[0].type == TOKEN_PAGE);
    CHECK(t[1].type == TOKEN_IDENTIFIER && token_text_is(t[1], "A"));
    CHECK(t[2].type == TOKEN_LBRACE);
    CHECK(t[3].type == TOKEN_RBRACE);
}

static void test_byte_order_mark(void)
{
    Token t[4];
    lex_all("\xEF\xBB\xBFpage", t, 4);
    CHECK(t[0].type == TOKEN_PAGE);
    CHECK(t[0].column == 1);
}

static void test_empty_input(void)
{
    Token t[2];
    size_t n = lex_all("", t, 2);
    CHECK(n == 1 && t[0].type == TOKEN_EOF);

    n = lex_all("   \n\t\r\n  ", t, 2);
    CHECK(n == 1 && t[0].type == TOKEN_EOF);

    /* Lexing past the end keeps returning EOF. */
    Lexer lexer;
    lexer_init(&lexer, "", 0);
    CHECK(lexer_next_token(&lexer).type == TOKEN_EOF);
    CHECK(lexer_next_token(&lexer).type == TOKEN_EOF);
}

static void test_comments(void)
{
    Token t[8];
    size_t n = lex_all("# full line\npage # trailing\n#end", t, 8);
    CHECK(n == 2);
    CHECK(t[0].type == TOKEN_PAGE && t[0].line == 2);

    /* '#' inside strings is not a comment. */
    n = lex_all("\"#7c3aed\" # c", t, 8);
    CHECK(n == 2 && t[0].type == TOKEN_STRING);

    Lexer lexer;
    const char *source = "page # note\n";
    lexer_init(&lexer, source, strlen(source));
    lexer.keep_comments = 1;
    CHECK(lexer_next_token(&lexer).type == TOKEN_PAGE);
    Token comment = lexer_next_token(&lexer);
    CHECK(comment.type == TOKEN_COMMENT && token_text_is(comment, "# note"));
    CHECK(lexer_next_token(&lexer).type == TOKEN_EOF);
}

static void test_invalid_characters(void)
{
    Token t[4];
    lex_all("@", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_UNEXPECTED_CHARACTER);

    lex_all("\xE2\x80\x9CHello\xE2\x80\x9D", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].error == LEX_CURLY_QUOTE);
    CHECK(t[0].width == 1);

    lex_all("\xF0\x9F\x98\x80", t, 4);
    CHECK(t[0].type == TOKEN_ERROR && t[0].length == 4 && t[0].width == 1);

    /* A lone invalid UTF-8 byte is reported, not skipped. */
    lex_all("\xFF", t, 4);
    CHECK(t[0].type == TOKEN_ERROR);

    /* An embedded NUL byte is reported rather than ending the input. */
    Lexer lexer;
    const char source[] = "page\0A";
    lexer_init(&lexer, source, sizeof(source) - 1);
    CHECK(lexer_next_token(&lexer).type == TOKEN_PAGE);
    Token nul = lexer_next_token(&lexer);
    CHECK(nul.type == TOKEN_ERROR && nul.error == LEX_NUL_BYTE);
}

static void test_buffer_boundaries(void)
{
    /* The lexer must never read past `length`, even if the buffer is not
       NUL-terminated there. */
    const char source[] = "page \"abc\" 12";
    Lexer lexer;
    Token token;

    lexer_init(&lexer, source, 8); /* ends inside the string: page "ab */
    CHECK(lexer_next_token(&lexer).type == TOKEN_PAGE);
    token = lexer_next_token(&lexer);
    CHECK(token.type == TOKEN_ERROR && token.error == LEX_UNTERMINATED_STRING);

    lexer_init(&lexer, source + 11, 1); /* "1" of "12" */
    token = lexer_next_token(&lexer);
    CHECK(token.type == TOKEN_NUMBER && token.length == 1);
}

void run_lexer_tests(void)
{
    test_all_token_types();
    test_keywords_are_case_sensitive();
    test_braces_adjacent_to_tokens();
    test_strings_and_escapes();
    test_unterminated_strings();
    test_numbers();
    test_line_and_column_tracking();
    test_line_endings();
    test_whitespace_variants();
    test_byte_order_mark();
    test_empty_input();
    test_comments();
    test_invalid_characters();
    test_buffer_boundaries();
}
