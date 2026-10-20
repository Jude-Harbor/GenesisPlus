#include <stdlib.h>

#include "codegen.h"
#include "test.h"
#include "version.h"

static void test_document_structure(void)
{
    char *html = html_of("page Home { text \"x\" }");
    CHECK(html != NULL);
    const char *prefix = "<!doctype html>\n<html lang=\"en\">\n<head>\n";
    CHECK(html != NULL && strncmp(html, prefix, strlen(prefix)) == 0);
    CHECK_CONTAINS(html, "<meta charset=\"utf-8\">");
    CHECK_CONTAINS(html, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    CHECK_CONTAINS(html, "<title>Home</title>");
    CHECK_CONTAINS(html, "<link rel=\"stylesheet\" href=\"style.css\">");
    CHECK_CONTAINS(html, "<body>\n  <main>\n    <p>x</p>\n  </main>\n</body>\n</html>\n");
    free(html);
}

static void test_every_element(void)
{
    char *html = html_of(
        "page Home {\n"
        "  heading \"H\"\n"
        "  heading \"H2\" level = 2\n"
        "  text \"T\"\n"
        "  link \"L\" to = \"about.html\"\n"
        "  button \"B\" to = \"#start\"\n"
        "  image \"assets/a.png\" alt = \"Alt\"\n"
        "  column { text \"c\" }\n"
        "  row { text \"r\" }\n"
        "}\n");
    CHECK_CONTAINS(html, "    <h1>H</h1>\n");
    CHECK_CONTAINS(html, "    <h2>H2</h2>\n");
    CHECK_CONTAINS(html, "    <p>T</p>\n");
    CHECK_CONTAINS(html, "    <a href=\"about.html\">L</a>\n");
    CHECK_CONTAINS(html, "    <a class=\"button\" href=\"#start\">B</a>\n");
    CHECK_CONTAINS(html, "    <img src=\"assets/a.png\" alt=\"Alt\">\n");
    CHECK_CONTAINS(html, "    <div class=\"column\">\n      <p>c</p>\n    </div>\n");
    CHECK_CONTAINS(html, "    <div class=\"row\">\n      <p>r</p>\n    </div>\n");
    free(html);
}

static void test_escaping(void)
{
    char *html = html_of("page Home title = \"A & B <x>\" {\n"
                         "  heading \"<script>alert('x')</script>\"\n"
                         "  text \"5 < 10 & 10 > 5 \\\"quoted\\\"\"\n"
                         "  link \"go\" to = \"page.html?a=1&b='2'\"\n"
                         "  image \"assets/a&b.png\" alt = \"\\\"><script>\"\n"
                         "}\n");
    CHECK(html != NULL);
    CHECK_CONTAINS(html, "<title>A &amp; B &lt;x&gt;</title>");
    CHECK_CONTAINS(html, "<h1>&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt;</h1>");
    CHECK_CONTAINS(html, "<p>5 &lt; 10 &amp; 10 &gt; 5 &quot;quoted&quot;</p>");
    CHECK_CONTAINS(html, "href=\"page.html?a=1&amp;b=&#39;2&#39;\"");
    CHECK_CONTAINS(html, "src=\"assets/a&amp;b.png\" alt=\"&quot;&gt;&lt;script&gt;\"");
    CHECK_NOT_CONTAINS(html, "<script>");
    free(html);

    Buffer out;
    buffer_init(&out);
    codegen_escape_html(&out, "&<>\"'plain");
    CHECK_STR(buffer_cstr(&out), "&amp;&lt;&gt;&quot;&#39;plain");
    buffer_free(&out);
}

static void test_unicode_passthrough(void)
{
    char *html = html_of("page Home { text \"Caf\xC3\xA9 \xF0\x9F\x9A\x80\" }");
    CHECK_CONTAINS(html, "<p>Caf\xC3\xA9 \xF0\x9F\x9A\x80</p>");
    free(html);
}

static void test_text_styles(void)
{
    char *html = html_of("page A {\n"
                         "  heading \"x\" size = 36 color = \"#7c3aed\" align = \"center\"\n"
                         "  text \"y\" size = 18 color = \"#fff\"\n"
                         "  button \"b\" to = \"#\" background = \"#000\" color = \"#fff\" size = 20\n"
                         "}\n");
    CHECK_CONTAINS(html,
                   "<h1 style=\"font-size: 36px; color: #7c3aed; text-align: center\">x</h1>");
    CHECK_CONTAINS(html, "<p style=\"font-size: 18px; color: #fff\">y</p>");
    CHECK_CONTAINS(html,
                   "<a class=\"button\" href=\"#\" style=\"font-size: 20px; color: #fff; "
                   "background-color: #000\">b</a>");
    free(html);
}

static void test_layout_styles(void)
{
    char *html = html_of("page A {\n"
                         "  column background = \"#222222\" gap = 16 width = 50% padding = 24 {\n"
                         "    text \"x\"\n"
                         "  }\n"
                         "  row gap = 0 align = \"center\" { text \"y\" }\n"
                         "}\n");
    CHECK_CONTAINS(html, "<div class=\"column sized\" style=\"background-color: #222222; "
                         "gap: 16px; width: 50%; padding: 24px\">");
    /* gap = 0 is an explicit value and must be emitted. */
    CHECK_CONTAINS(html, "<div class=\"row align-center\" style=\"gap: 0px\">");
    free(html);
}

static void test_padding_precedence(void)
{
    /* Written in "wrong" order; output must still go general -> specific. */
    char *html = html_of("page A { column padding right = 6 padding top = 4 padding inline = 3 "
                         "padding block = 2 padding = 1 padding left = 5 padding bottom = 7 "
                         "{ text \"x\" } }");
    CHECK_CONTAINS(html, "style=\"padding: 1px; padding-block: 2px; padding-inline: 3px; "
                         "padding-top: 4px; padding-bottom: 7px; padding-left: 5px; "
                         "padding-right: 6px\"");
    free(html);

    html = html_of("page A { column padding top = 12 padding = 24 { text \"x\" } }");
    char *general = strstr(html, "padding: 24px");
    char *top = strstr(html, "padding-top: 12px");
    CHECK(general != NULL && top != NULL && general < top);
    free(html);
}

static void test_widths(void)
{
    char *html = html_of("page A { row {\n"
                         "  column width = 33.33% { text \"a\" }\n"
                         "  column width = 12.5% { text \"b\" }\n"
                         "  column width = 100% { text \"c\" }\n"
                         "  image \"https://x.org/i.png\" alt = \"i\" width = 25%\n"
                         "} }");
    CHECK_CONTAINS(html, "width: 33.33%");
    CHECK_CONTAINS(html, "width: 12.5%");
    CHECK_CONTAINS(html, "width: 100%");
    CHECK_CONTAINS(html, "<img class=\"sized\" src=\"https://x.org/i.png\" alt=\"i\" "
                         "style=\"width: 25%\">");
    free(html);
}

static void test_page_styles_and_title(void)
{
    char *html = html_of("page Home title = \"My Site\" background = \"#000\" color = \"#eee\" "
                         "{ text \"x\" }");
    CHECK_CONTAINS(html, "<title>My Site</title>");
    CHECK_CONTAINS(html, "<body style=\"background-color: #000; color: #eee\">");
    free(html);
}

static void test_empty_content(void)
{
    char *html = html_of("page A { column { } text \"\" image \"assets/a.png\" }");
    CHECK_CONTAINS(html, "<div class=\"column\"></div>");
    CHECK_CONTAINS(html, "<p></p>");
    CHECK_CONTAINS(html, "alt=\"\"");
    free(html);

    html = html_of("page Empty { }");
    CHECK_CONTAINS(html, "<main>\n  </main>");
    free(html);
}

static void test_nested_output_is_balanced(void)
{
    char *html = html_of("page A { column { row { column { row { text \"deep\" } } } } }");
    CHECK(html != NULL);

    int opens = 0;
    int closes = 0;
    for (const char *p = html; (p = strstr(p, "<div")) != NULL; p++) {
        opens++;
    }
    for (const char *p = html; (p = strstr(p, "</div>")) != NULL; p++) {
        closes++;
    }
    CHECK(opens == 4 && closes == 4);
    CHECK_CONTAINS(html, "          <div class=\"row\">\n            <p>deep</p>\n");
    free(html);
}

static void test_multiple_pages_output(void)
{
    Compilation compilation;
    CHECK(compile_text("page Home { link \"About\" to = \"about.html\" } "
                       "page About title = \"About us\" { text \"hi\" }",
                       &compilation));
    CHECK(compilation.file_count == 3);
    CHECK_STR(compilation.files[0].name, "index.html");
    CHECK_STR(compilation.files[1].name, "about.html");
    CHECK_STR(compilation.files[2].name, "style.css");
    CHECK_CONTAINS(buffer_cstr(&compilation.files[1].content), "<title>About us</title>");
    compilation_free(&compilation);
}

static void test_deterministic_output(void)
{
    const char *source = "page A { column gap = 4 { heading \"x\" size = 20 text \"y\" } }";
    char *first = html_of(source);
    char *second = html_of(source);
    CHECK_STR(second, first);
    free(first);
    free(second);

    Buffer a;
    Buffer b;
    buffer_init(&a);
    buffer_init(&b);
    codegen_css(&a);
    codegen_css(&b);
    CHECK_STR(buffer_cstr(&a), buffer_cstr(&b));
    CHECK_CONTAINS(buffer_cstr(&a), "box-sizing: border-box");
    CHECK_CONTAINS(buffer_cstr(&a), "@media (max-width: 640px)");
    CHECK_CONTAINS(buffer_cstr(&a), "Genesis+ " GENESIS_VERSION);
    buffer_free(&a);
    buffer_free(&b);
}

static void test_failed_compile_produces_no_files(void)
{
    Compilation compilation;
    CHECK(!compile_text("page A { text \"x\" size = 1 }", &compilation));
    CHECK(compilation.file_count == 0);
    compilation_free(&compilation);
}

static void test_buffer_growth(void)
{
    Buffer buffer;
    buffer_init(&buffer);
    CHECK_STR(buffer_cstr(&buffer), "");
    for (int i = 0; i < 10000; i++) {
        buffer_appendf(&buffer, "%d,", i % 10);
    }
    CHECK(buffer.length == 20000);
    CHECK(!buffer.failed);
    CHECK(buffer.data[buffer.length] == '\0');
    buffer_clear(&buffer);
    CHECK(buffer.length == 0 && buffer_cstr(&buffer)[0] == '\0');
    buffer_free(&buffer);
}

void run_codegen_tests(void)
{
    test_document_structure();
    test_every_element();
    test_escaping();
    test_unicode_passthrough();
    test_text_styles();
    test_layout_styles();
    test_padding_precedence();
    test_widths();
    test_page_styles_and_title();
    test_empty_content();
    test_nested_output_is_balanced();
    test_multiple_pages_output();
    test_deterministic_output();
    test_failed_compile_produces_no_files();
    test_buffer_growth();
}
