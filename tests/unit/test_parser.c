#include <stdlib.h>

#include "parser.h"
#include "test.h"

static AstNode *parse_ok(const char *source, DiagList *diags)
{
    diag_init(diags, "test.gns", source, strlen(source));
    return parse_program(source, strlen(source), diags);
}

static void test_valid_page_structure(void)
{
    DiagList diags;
    const char *source =
        "page Home {\n"
        "    column gap = 8 {\n"
        "        heading \"Welcome\" size = 36 color = \"#7c3aed\"\n"
        "        text \"Hello\"\n"
        "        row {\n"
        "            text \"a\"\n"
        "            text \"b\"\n"
        "        }\n"
        "    }\n"
        "}\n";
    AstNode *program = parse_ok(source, &diags);

    CHECK(program != NULL);
    CHECK(diags.error_count == 0);
    CHECK(diags.warning_count == 0);

    AstNode *page = program->first_child;
    CHECK(page != NULL && page->type == AST_PAGE);
    CHECK_STR(page->value, "Home");
    CHECK_STR(page->file_name, "index.html");

    AstNode *column = page->first_child;
    CHECK(column != NULL && column->type == AST_COLUMN);
    CHECK(column->has_gap && column->gap == 8);
    CHECK(column->line == 2 && column->column == 5);

    AstNode *heading = column->first_child;
    CHECK(heading->type == AST_HEADING);
    CHECK_STR(heading->value, "Welcome");
    CHECK(heading->size == 36);
    CHECK(heading->level == 1);
    CHECK(heading->color.set);
    CHECK_STR(heading->color.value, "#7c3aed");

    AstNode *row = heading->next_sibling->next_sibling;
    CHECK(row->type == AST_ROW);
    CHECK(ast_child_count(row) == 2);
    CHECK(row->parent == column);

    ast_free(program);
}

static void test_compact_equals_expanded(void)
{
    char *expanded = html_of("page Home {\n    column {\n        heading \"Welcome!\"\n"
                             "        text \"Hello!\"\n    }\n}\n");
    char *compact = html_of("page Home{column{heading \"Welcome!\" text \"Hello!\"}}");
    char *tabs = html_of("page Home {\n\tcolumn {\n\t\theading \"Welcome!\"\n\t\ttext \"Hello!\"\n\t}\n}");
    char *weird = html_of("   page   Home\n{\n column\n{ heading\n\"Welcome!\"\n\n\n text \"Hello!\" } }  ");
    char *crlf = html_of("page Home {\r\n  column {\r\n    heading \"Welcome!\"\r\n"
                         "    text \"Hello!\"\r\n  }\r\n}\r\n");
    char *semicolons = html_of("page Home { column { heading \"Welcome!\"; text \"Hello!\"; } }");

    CHECK(expanded != NULL);
    CHECK_STR(compact, expanded);
    CHECK_STR(tabs, expanded);
    CHECK_STR(weird, expanded);
    CHECK_STR(crlf, expanded);
    CHECK_STR(semicolons, expanded);

    free(expanded);
    free(compact);
    free(tabs);
    free(weird);
    free(crlf);
    free(semicolons);
}

static void test_mixed_indentation_from_ai_assistants(void)
{
    /* Spaces, tabs, and inconsistent indentation all mean the same thing. */
    EXPECT_OK("page Home {\n"
              "  column {\n"
              "\t    heading \"Mixed\"\n"
              "        text \"indent\"\n"
              " }\n"
              "                }\n");
}

static void test_property_order_does_not_matter(void)
{
    char *a = html_of("page A { column gap = 4 padding = 8 background = \"#fff\" width = 50% "
                      "{ heading \"x\" size = 20 color = \"#000\" level = 2 align = \"center\" } }");
    char *b = html_of("page A { column width = 50% background = \"#fff\" padding = 8 gap = 4 "
                      "{ heading \"x\" align = \"center\" level = 2 color = \"#000\" size = 20 } }");
    CHECK(a != NULL);
    CHECK_STR(b, a);
    free(a);
    free(b);
}

static void test_all_elements_parse(void)
{
    EXPECT_OK("page Home title = \"Site\" background = \"#fafafa\" color = \"#111\" {\n"
              "  heading \"H\" level = 3 size = 30 color = \"#123\" align = \"right\"\n"
              "  text \"T\" size = 16 color = \"#123456\" align = \"left\"\n"
              "  link \"L\" to = \"about.html\" size = 14 color = \"#00f\"\n"
              "  button \"B\" to = \"https://example.com\" background = \"#000\" color = \"#fff\" size = 18\n"
              "  image \"https://example.com/a.png\" alt = \"A\" width = 50%\n"
              "  column align = \"center\" { text \"c\" }\n"
              "  row align = \"right\" { text \"r\" }\n"
              "}\n"
              "page About { text \"second page\" }\n");
}

static void test_multiple_pages(void)
{
    DiagList diags;
    AstNode *program = parse_ok("page Home { text \"a\" } page About { text \"b\" } "
                                "page Contact_Us { text \"c\" }",
                                &diags);
    CHECK(diags.error_count == 0);
    AstNode *page = program->first_child;
    CHECK_STR(page->file_name, "index.html");
    CHECK_STR(page->next_sibling->file_name, "about.html");
    CHECK_STR(page->next_sibling->next_sibling->file_name, "contact_us.html");
    ast_free(program);

    EXPECT_ERROR("page Home { text \"a\" }\npage home { text \"b\" }", 2, 6,
                 "Duplicate page name");
    EXPECT_ERROR("page Home { text \"a\" }\npage Index { text \"b\" }", 2, 6,
                 "Only the first page");
    EXPECT_ERROR("page Con { text \"a\" }", 1, 6, "reserved by Windows");
    EXPECT_OK("page Index { text \"a\" }");
}

static void test_missing_braces(void)
{
    EXPECT_ERROR("page Home {\n  column {\n    text \"x\"\n", 2, 10, "never closed");
    EXPECT_ERROR("page Home {\n  text \"x\"\n", 1, 11, "never closed");
    EXPECT_ERROR("page Home\n  text \"x\"\n}", 2, 3, "Expected '{'");
    EXPECT_ERROR("page Home {\n  column\n    text \"x\"\n  }\n}", 3, 5, "Expected '{'");
    EXPECT_ERROR("page Home {\n  column {\n    text \"x\"\n  \npage Two {\n}\n", 5, 1,
                 "forget a '}'");
}

static void test_unexpected_braces(void)
{
    EXPECT_ERROR("page Home {\n  text \"x\"\n}\n}\n", 4, 1, "no open block");
    EXPECT_ERROR("}", 1, 1, "no open block");
    EXPECT_ERROR("page Home {\n  {\n  }\n}", 2, 3, "Unexpected '{'");
    EXPECT_ERROR("page Home { text \"x\" { } }", 1, 22, "cannot contain other elements");
    EXPECT_ERROR("page Home { heading { } }", 1, 21, "cannot contain other elements");
}

static void test_legacy_colon_syntax(void)
{
    EXPECT_ERROR("page Home:\n\tcolumn:\n\t\ttext \"x\"\n", 1, 10, "braces instead of ':'");
    EXPECT_ERROR("page Home {\n  column gap = 4:\n    text \"x\"\n}", 2, 17,
                 "braces instead of ':'");
}

static void test_missing_values(void)
{
    EXPECT_ERROR("page A { heading \"x\" size = }", 1, 27, "Expected a value after 'size ='");
    EXPECT_ERROR("page A { heading \"x\" size }", 1, 22, "Expected '=' after 'size'");
    EXPECT_ERROR("page A { heading }", 1, 18, "Expected text in double quotes");
    EXPECT_ERROR("page A { text Hello }", 1, 15, "in double quotes");
    EXPECT_ERROR("page A { heading \"x\" size = \n text \"y\" }", 1, 27, "Expected a value");
    EXPECT_ERROR("page { }", 1, 6, "Expected a page name");
    EXPECT_ERROR("page \"Home\" { }", 1, 6, "without quotes");
    EXPECT_ERROR("page row { }", 1, 6, "keyword");
    EXPECT_ERROR("page A { link \"x\" }", 1, 10, "needs a destination");
    EXPECT_ERROR("page A { button \"x\" }", 1, 10, "needs a destination");
}

static void test_invalid_property_types(void)
{
    EXPECT_ERROR("page A { heading \"x\" size = \"big\" }", 1, 29, "whole number");
    EXPECT_ERROR("page A { heading \"x\" color = 5 }", 1, 30, "in quotes");
    EXPECT_ERROR("page A { column width = 50 { text \"x\" } }", 1, 25, "percentage");
    EXPECT_ERROR("page A { column gap = 50% { text \"x\" } }", 1, 23, "whole number");
    EXPECT_ERROR("page A { heading \"x\" size = 12.5 }", 1, 29, "whole number");
    EXPECT_ERROR("page A { heading \"x\" align = center }", 1, 30, "double quotes");
    EXPECT_ERROR("page A { heading \"x\" size = 16px }", 1, 29, "Unexpected unit");
    EXPECT_ERROR("page A { column padding = -4 { text \"x\" } }", 1, 27, "Negative");
}

static void test_property_validity_per_element(void)
{
    EXPECT_ERROR("page A { heading \"x\" gap = 4 }", 1, 22, "'gap' cannot be used on heading");
    EXPECT_ERROR("page A { text \"x\" padding = 4 }", 1, 19, "'padding' cannot be used on text");
    EXPECT_ERROR("page A { column size = 4 { text \"x\" } }", 1, 17, "cannot be used on column");
    EXPECT_ERROR("page A { text \"x\" level = 2 }", 1, 19, "cannot be used on text");
    EXPECT_ERROR("page A { heading \"x\" colour = \"#fff\" }", 1, 22, "Did you mean 'color'");
    EXPECT_ERROR("page A { heading \"x\" fontsize = 3 }", 1, 22, "Did you mean 'size'");
    EXPECT_ERROR("page A { link \"x\" href = \"a.html\" }", 1, 19, "Did you mean 'to'");
    EXPECT_ERROR("page A { column { text \"x\" }\n gap = 3 }", 2, 2, "wrong place");
}

static void test_duplicate_properties(void)
{
    EXPECT_ERROR("page A { heading \"x\" size = 10 size = 12 }", 1, 32, "Duplicate property 'size'");
    EXPECT_ERROR("page A { column gap = 1 gap = 2 { text \"x\" } }", 1, 25, "Duplicate");
    EXPECT_ERROR("page A { column padding top = 1 padding top = 2 { text \"x\" } }", 1, 33,
                 "Duplicate property 'padding top'");
    /* Different padding sides are not duplicates. */
    EXPECT_OK("page A { column padding = 1 padding top = 2 padding left = 3 { text \"x\" } }");
}

static void test_widths(void)
{
    EXPECT_OK("page A { column width = 100% { text \"x\" } }");
    EXPECT_OK("page A { column width = 0.01% { text \"x\" } }");
    EXPECT_OK("page A { column width = 33.33% { text \"x\" } }");
    EXPECT_ERROR("page A { column width = 120% { text \"x\" } }", 1, 25, "Invalid width '120%'");
    EXPECT_ERROR("page A { column width = 0% { text \"x\" } }", 1, 25, "Invalid width");
    EXPECT_ERROR("page A { column width = 100.01% { text \"x\" } }", 1, 25, "Invalid width");
    EXPECT_ERROR("page A { column width = 33.333% { text \"x\" } }", 1, 25, "decimal places");
    EXPECT_ERROR("page A { column width = 99999999999999999999% { text \"x\" } }", 1, 25,
                 "Invalid width");
    EXPECT_ERROR("page A { text \"x\" width = 50% }", 1, 19, "cannot be used on text");
}

static void test_colors(void)
{
    EXPECT_OK("page A { heading \"x\" color = \"#abc\" }");
    EXPECT_OK("page A { heading \"x\" color = \"#AABBCC\" }");
    EXPECT_ERROR("page A { heading \"x\" color = \"red\" }", 1, 30, "Expected #RGB or #RRGGBB");
    EXPECT_ERROR("page A { heading \"x\" color = \"#abcd\" }", 1, 30, "Invalid color");
    EXPECT_ERROR("page A { heading \"x\" color = \"#ggg\" }", 1, 30, "Invalid color");
    EXPECT_ERROR("page A { heading \"x\" color = \"\" }", 1, 30, "Invalid color");
    EXPECT_ERROR("page A { heading \"x\" color = \"#fff;x:y\" }", 1, 30, "Invalid color");
    EXPECT_ERROR("page A { column background = \"blue\" { text \"x\" } }", 1, 30,
                 "Invalid background color");
}

static void test_padding_values(void)
{
    EXPECT_OK("page A { column padding = 0 padding block = 256 { text \"x\" } }");
    EXPECT_ERROR("page A { column padding = 257 { text \"x\" } }", 1, 27, "between 0 and 256");
    EXPECT_ERROR("page A { column padding up = 4 { text \"x\" } }", 1, 25,
                 "Unknown padding side 'up'");
    EXPECT_ERROR("page A { column padding top 4 { text \"x\" } }", 1, 29, "Expected '='");
    EXPECT_ERROR("page A { column padding = \"4\" { text \"x\" } }", 1, 27, "whole number");
}

static void test_numeric_ranges(void)
{
    EXPECT_OK("page A { heading \"x\" size = 8 } page B { heading \"y\" size = 1000 }");
    EXPECT_ERROR("page A { heading \"x\" size = 7 }", 1, 29, "between 8 and 1000");
    EXPECT_ERROR("page A { heading \"x\" size = 1001 }", 1, 29, "between 8 and 1000");
    EXPECT_ERROR("page A { heading \"x\" size = 99999999999999999999 }", 1, 29, "between");
    EXPECT_ERROR("page A { heading \"x\" level = 7 }", 1, 30, "between 1 and 6");
    EXPECT_ERROR("page A { heading \"x\" level = 0 }", 1, 30, "between 1 and 6");
    EXPECT_ERROR("page A { column gap = 300 { text \"x\" } }", 1, 23, "between 0 and 256");
}

static void test_align_values(void)
{
    EXPECT_ERROR("page A { text \"x\" align = \"middle\" }", 1, 27, "Invalid align value");
    EXPECT_ERROR("page A { text \"x\" align = \"Center\" }", 1, 27, "Invalid align value");
}

static void test_links(void)
{
    EXPECT_OK("page A { link \"a\" to = \"about.html\" link \"b\" to = \"#top\" "
              "link \"c\" to = \"mailto:me@example.com\" link \"d\" to = \"tel:+15551234\" "
              "link \"e\" to = \"HTTPS://EXAMPLE.COM/x?y=1&z=2\" link \"f\" to = \"docs/\" }");
    EXPECT_ERROR("page A { link \"x\" to = \"javascript:alert(1)\" }", 1, 24,
                 "'javascript:' are not allowed");
    EXPECT_ERROR("page A { link \"x\" to = \"JavaScript:alert(1)\" }", 1, 24, "not allowed");
    EXPECT_ERROR("page A { link \"x\" to = \"data:text/html,hi\" }", 1, 24, "not allowed");
    EXPECT_ERROR("page A { link \"x\" to = \"\" }", 1, 24, "empty");
    EXPECT_ERROR("page A { link \"x\" to = \"my page.html\" }", 1, 24, "space");
    EXPECT_ERROR("page A { link \"x\" to = \"a\\\\b.html\" }", 1, 24, "backslash");
    EXPECT_ERROR("page A { link \"x\" to = \"C:/site/a.html\" }", 1, 24, "not allowed");
}

static void test_invalid_link_reported_once(void)
{
    /* Regression: an invalid `to` used to also report "needs a destination". */
    size_t errors = 0;
    char *text = diagnostics_of("page A { link \"x\" to = \"javascript:x\" }", &errors, NULL);
    CHECK(errors == 1);
    CHECK_NOT_CONTAINS(text, "needs a destination");
    free(text);
}

static void test_images(void)
{
    EXPECT_OK("page A { image \"assets/logo.png\" alt = \"Logo\" }");
    EXPECT_OK("page A { image \"assets/photos/cat.jpg\" alt = \"\" }");
    EXPECT_OK("page A { image \"https://example.com/cat.jpg\" alt = \"Cat\" }");
    EXPECT_ERROR("page A { image \"logo.png\" alt = \"x\" }", 1, 16, "assets folder");
    EXPECT_ERROR("page A { image \"assets/../secret.png\" alt = \"x\" }", 1, 16,
                 "inside the assets folder");
    EXPECT_ERROR("page A { image \"assets/\" alt = \"x\" }", 1, 16, "assets folder");
    EXPECT_ERROR("page A { image \"assets//a.png\" alt = \"x\" }", 1, 16, "inside the assets");
    EXPECT_ERROR("page A { image \"/etc/passwd\" alt = \"x\" }", 1, 16, "assets folder");
    EXPECT_ERROR("page A { image \"C:\\\\x.png\" alt = \"x\" }", 1, 16, "not allowed");
    EXPECT_ERROR("page A { image \"javascript:x\" alt = \"x\" }", 1, 16, "not allowed");
    EXPECT_ERROR("page A { image \"\" }", 1, 16, "empty");

    size_t errors = 0;
    size_t warnings = 0;
    char *text = diagnostics_of("page A { image \"assets/a.png\" }", &errors, &warnings);
    CHECK(errors == 0 && warnings == 1);
    CHECK_CONTAINS(text, "no 'alt' description");
    free(text);
}

static void test_unknown_elements(void)
{
    EXPECT_ERROR("page A { headng \"x\" }", 1, 10, "Did you mean 'heading'");
    EXPECT_ERROR("page A { paragraph \"x\" }", 1, 10, "Did you mean 'text'");
    EXPECT_ERROR("page A { div { } }", 1, 10, "Did you mean 'column'");
    EXPECT_ERROR("page A { img \"x\" }", 1, 10, "Did you mean 'image'");
    EXPECT_ERROR("page A { \"loose text\" }", 1, 10, "Unexpected text");
    EXPECT_ERROR("page A { 42 }", 1, 10, "Unexpected '42'");
    EXPECT_ERROR("page A { page B { } }", 1, 10, "cannot be placed inside");
}

static void test_top_level(void)
{
    EXPECT_ERROR("", 1, 1, "no pages");
    EXPECT_ERROR("   \n\n  # only a comment\n", 4, 1, "no pages");
    EXPECT_ERROR("heading \"x\"", 1, 1, "must be inside a page");
    EXPECT_ERROR("page A { text \"x\" } text \"y\"", 1, 21, "must be inside a page");
    EXPECT_ERROR("hello", 1, 1, "Expected 'page'");
    EXPECT_ERROR("page A { text \"x\" } garbage", 1, 21, "Expected 'page'");
}

static void test_lexer_errors_surface(void)
{
    EXPECT_ERROR("page A { text \"unterminated }", 1, 15, "missing its closing quote");
    EXPECT_ERROR("page A { text \xE2\x80\x9Chi\xE2\x80\x9D }", 1, 15, "curly quote");
    EXPECT_ERROR("page A { text \"x\" @ }", 1, 19, "Unexpected character '@'");
    EXPECT_ERROR("page A { text \"bad \\q\" }", 1, 20, "Unknown escape");
    EXPECT_ERROR("page A ( text \"x\" )", 1, 8, "curly braces");
}

static void test_escapes_decode(void)
{
    DiagList diags;
    AstNode *program = parse_ok("page A { text \"say \\\"hi\\\" \\\\o/\" }", &diags);
    CHECK(diags.error_count == 0);
    CHECK_STR(program->first_child->first_child->value, "say \"hi\" \\o/");
    ast_free(program);
}

static void test_empty_blocks_warn(void)
{
    size_t errors = 0;
    size_t warnings = 0;
    char *text = diagnostics_of("page A { column { } row {} }", &errors, &warnings);
    CHECK(errors == 0);
    CHECK(warnings == 2);
    CHECK_CONTAINS(text, "This column is empty");
    CHECK_CONTAINS(text, "This row is empty");
    free(text);

    text = diagnostics_of("page A { }", &errors, &warnings);
    CHECK(errors == 0 && warnings == 1);
    free(text);

    text = diagnostics_of("page A { text \"\" }", &errors, &warnings);
    CHECK(errors == 0 && warnings == 1);
    CHECK_CONTAINS(text, "This text is empty");
    free(text);
}

static void test_multiple_value_errors_reported_together(void)
{
    size_t errors = 0;
    char *text = diagnostics_of("page A {\n"
                                "  heading \"x\" size = 2 color = \"red\"\n"
                                "  column width = 200% { text \"y\" }\n"
                                "}\n",
                                &errors, NULL);
    CHECK(errors == 3);
    free(text);
}

static void test_error_cap_and_no_duplicates(void)
{
    /* A structural error stops the parse: exactly one error, no cascade. */
    size_t errors = 0;
    char *text = diagnostics_of("page A {\n column {\n  text \"a\"\n row {\n", &errors, NULL);
    CHECK(errors == 1);
    free(text);

    /* Many value errors are capped, and the cap is reported. */
    Buffer source;
    buffer_init(&source);
    buffer_append_str(&source, "page A {");
    for (int i = 0; i < 100; i++) {
        buffer_append_str(&source, " heading \"x\" size = 1");
    }
    buffer_append_str(&source, " }");
    text = diagnostics_of(buffer_cstr(&source), &errors, NULL);
    CHECK(errors == 100);
    CHECK_CONTAINS(text, "too many problems");
    free(text);
    buffer_free(&source);
}

static void test_nesting_limit(void)
{
    Buffer source;
    buffer_init(&source);
    buffer_append_str(&source, "page A {");
    for (int i = 0; i < 70; i++) {
        buffer_append_str(&source, " column {");
    }
    EXPECT_ERROR(buffer_cstr(&source), 1, 0, "nested too deeply");

    /* 63 nested columns + the page is exactly the limit and is fine. */
    buffer_clear(&source);
    buffer_append_str(&source, "page A {");
    for (int i = 0; i < PARSER_MAX_DEPTH - 1; i++) {
        buffer_append_str(&source, " column {");
    }
    buffer_append_str(&source, " text \"deep\"");
    for (int i = 0; i < PARSER_MAX_DEPTH; i++) {
        buffer_append_str(&source, " }");
    }
    EXPECT_OK(buffer_cstr(&source));
    buffer_free(&source);
}

static void test_bom_and_crlf_programs(void)
{
    EXPECT_OK("\xEF\xBB\xBFpage A {\r\n  text \"x\"\r\n}\r\n");
    /* Newlines do not end statements: `size` still belongs to the text. */
    EXPECT_ERROR("page A {\r\n  text \"x\"\r\n  size = 3\r\n  oops\r\n}\r\n", 3, 10,
                 "between 8 and 1000");
    EXPECT_ERROR("page A {\r\n  text \"x\"\r\n  size = 30\r\n  oops\r\n}\r\n", 4, 3,
                 "Unknown element 'oops'");
}

static void test_ast_free_handles_null(void)
{
    ast_free(NULL);
    CHECK(1);
}

void run_parser_tests(void)
{
    test_valid_page_structure();
    test_compact_equals_expanded();
    test_mixed_indentation_from_ai_assistants();
    test_property_order_does_not_matter();
    test_all_elements_parse();
    test_multiple_pages();
    test_missing_braces();
    test_unexpected_braces();
    test_legacy_colon_syntax();
    test_missing_values();
    test_invalid_property_types();
    test_property_validity_per_element();
    test_duplicate_properties();
    test_widths();
    test_colors();
    test_padding_values();
    test_numeric_ranges();
    test_align_values();
    test_links();
    test_invalid_link_reported_once();
    test_images();
    test_unknown_elements();
    test_top_level();
    test_lexer_errors_surface();
    test_escapes_decode();
    test_empty_blocks_warn();
    test_multiple_value_errors_reported_together();
    test_error_cap_and_no_duplicates();
    test_nesting_limit();
    test_bom_and_crlf_programs();
    test_ast_free_handles_null();
}
