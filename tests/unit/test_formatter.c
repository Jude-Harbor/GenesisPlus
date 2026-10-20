#include <stdlib.h>

#include "formatter.h"
#include "test.h"

static char *format_text(const char *source, int *ok)
{
    DiagList diags;
    diag_init(&diags, "test.gns", source, strlen(source));
    Buffer out;
    buffer_init(&out);
    *ok = format_source(source, strlen(source), &out, &diags);

    size_t length = out.length;
    char *result = malloc(length + 1);
    if (result != NULL) {
        memcpy(result, buffer_cstr(&out), length + 1);
    }
    buffer_free(&out);
    return result;
}

static void check_formats_to(const char *file, int line, const char *source,
                             const char *expected)
{
    int ok = 0;
    char *actual = format_text(source, &ok);
    test_checks++;

    if (!ok || actual == NULL || strcmp(actual, expected) != 0) {
        test_fail(file, line, "formatter output differs");
        fprintf(stderr, "    expected:\n%s\n    actual:\n%s\n", expected,
                actual ? actual : "(null)");
    }

    /* Formatting is idempotent. */
    if (ok && actual != NULL) {
        int ok_again = 0;
        char *again = format_text(actual, &ok_again);
        test_checks++;
        if (!ok_again || again == NULL || strcmp(again, actual) != 0) {
            test_fail(file, line, "formatter is not idempotent");
        }
        free(again);
    }

    free(actual);
}

#define CHECK_FORMAT(source, expected) check_formats_to(__FILE__, __LINE__, source, expected)

static void test_compact_input(void)
{
    CHECK_FORMAT("page Home{column{heading \"Welcome!\" text \"Hello!\"}}",
                 "page Home {\n"
                 "    column {\n"
                 "        heading \"Welcome!\"\n"
                 "        text \"Hello!\"\n"
                 "    }\n"
                 "}\n");
}

static void test_messy_indentation(void)
{
    CHECK_FORMAT("page Home {\n"
                 "\t\t  column   gap=4\n"
                 "{\n"
                 "heading    \"x\"   size=30\n"
                 "    color = \"#fff\"\n"
                 "  }\n"
                 "          }",
                 "page Home {\n"
                 "    column gap = 4 {\n"
                 "        heading \"x\" size = 30 color = \"#fff\"\n"
                 "    }\n"
                 "}\n");
}

static void test_comments_and_blank_lines(void)
{
    CHECK_FORMAT("# Site\n"
                 "page Home { # the home page\n"
                 "\n"
                 "  heading \"A\"\n"
                 "\n"
                 "\n"
                 "\n"
                 "  # about the text\n"
                 "  text \"B\"   # trailing\n"
                 "\n"
                 "}\n"
                 "page About { text \"C\" }\n",
                 "# Site\n"
                 "page Home {  # the home page\n"
                 "    heading \"A\"\n"
                 "\n"
                 "    # about the text\n"
                 "    text \"B\"  # trailing\n"
                 "}\n"
                 "\n"
                 "page About {\n"
                 "    text \"C\"\n"
                 "}\n");
}

static void test_empty_blocks_and_semicolons(void)
{
    CHECK_FORMAT("page A { column { } ; text \"x\"; }",
                 "page A {\n"
                 "    column {}\n"
                 "    text \"x\"\n"
                 "}\n");
}

static void test_strings_kept_verbatim(void)
{
    CHECK_FORMAT("page A { text \"  spaced   \\\"quotes\\\"  # not a comment \" }",
                 "page A {\n"
                 "    text \"  spaced   \\\"quotes\\\"  # not a comment \"\n"
                 "}\n");
}

static void test_crlf_preserved(void)
{
    CHECK_FORMAT("page A {\r\n text \"x\"\r\n}\r\n",
                 "page A {\r\n"
                 "    text \"x\"\r\n"
                 "}\r\n");
}

static void test_refuses_invalid_source(void)
{
    int ok = 1;
    char *result = format_text("page A { text \"x\" ", &ok);
    CHECK(!ok);
    CHECK(result != NULL && result[0] == '\0');
    free(result);
}

static void test_formatting_preserves_meaning(void)
{
    const char *sources[] = {
        "page A{row gap=4{column width=50%{text\"a\"}column{heading\"b\"level=3}}}",
        "page A title=\"T\"{column padding top=1 padding=2{link\"x\"to=\"y.html\"}} page B{text\"z\"}",
    };

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        int ok = 0;
        char *formatted = format_text(sources[i], &ok);
        CHECK(ok);
        char *before = html_of(sources[i]);
        char *after = html_of(formatted);
        CHECK_STR(after, before);
        free(before);
        free(after);
        free(formatted);
    }
}

void run_formatter_tests(void)
{
    test_compact_input();
    test_messy_indentation();
    test_comments_and_blank_lines();
    test_empty_blocks_and_semicolons();
    test_strings_kept_verbatim();
    test_crlf_preserved();
    test_refuses_invalid_source();
    test_formatting_preserves_meaning();
}
