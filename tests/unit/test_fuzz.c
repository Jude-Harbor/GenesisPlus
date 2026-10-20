/*
 * Deterministic fuzz tests. Random and mutated inputs are fed to the
 * compiler and formatter to make sure malformed input never crashes,
 * hangs, or corrupts memory (run `make sanitize` to check the latter with
 * AddressSanitizer), and that two properties always hold:
 *
 *   1. A successful compile produces a complete HTML document.
 *   2. Formatting is idempotent and never changes the generated website.
 */

#include <stdlib.h>

#include "formatter.h"
#include "test.h"

static unsigned long long rng = 0x9E3779B97F4A7C15ull;

static unsigned next_random(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng >> 32);
}

static unsigned random_below(unsigned n)
{
    return n == 0 ? 0 : next_random() % n;
}

static int failures_reported = 0;

static void report_input(const char *what, const char *data, size_t length)
{
    if (failures_reported++ > 5) {
        return;
    }
    fprintf(stderr, "    %s for input (%zu bytes): \"", what, length);
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
            fputc(c, stderr);
        } else {
            fprintf(stderr, "\\x%02x", c);
        }
    }
    fprintf(stderr, "\"\n");
}

static char *first_page_html(const char *data, size_t length, int *ok)
{
    Compilation compilation;
    *ok = compiler_compile(&compilation, "fuzz.gns", data, length, NULL);
    char *html = NULL;

    if (*ok) {
        const Buffer *content = &compilation.files[0].content;
        html = malloc(content->length + 1);
        if (html != NULL) {
            memcpy(html, buffer_cstr(content), content->length + 1);
        }
    }

    compilation_free(&compilation);
    return html;
}

static void check_input(const char *data, size_t length)
{
    int ok = 0;
    char *html = first_page_html(data, length, &ok);
    test_checks++;

    if (ok) {
        size_t html_length = html != NULL ? strlen(html) : 0;
        if (html == NULL || html_length < 8 ||
            strcmp(html + html_length - 8, "</html>\n") != 0) {
            test_fail(__FILE__, __LINE__, "incomplete HTML document");
            report_input("incomplete HTML", data, length);
        }
    }

    DiagList *diags = malloc(sizeof(DiagList));
    if (diags == NULL) {
        free(html);
        return;
    }
    diag_init(diags, "fuzz.gns", data, length);

    Buffer formatted;
    buffer_init(&formatted);
    int formatted_ok = format_source(data, length, &formatted, diags);
    test_checks++;

    if (formatted_ok != ok) {
        test_fail(__FILE__, __LINE__, "formatter and compiler disagree about validity");
        report_input("validity mismatch", data, length);
    }

    if (formatted_ok) {
        int ok_after = 0;
        char *html_after = first_page_html(formatted.data, formatted.length, &ok_after);
        test_checks++;
        if (!ok_after || html == NULL || html_after == NULL || strcmp(html, html_after) != 0) {
            test_fail(__FILE__, __LINE__, "formatting changed the output");
            report_input("format changed output", data, length);
        }
        free(html_after);

        Buffer again;
        buffer_init(&again);
        diag_init(diags, "fuzz.gns", formatted.data, formatted.length);
        format_source(formatted.data, formatted.length, &again, diags);
        test_checks++;
        if (again.length != formatted.length ||
            memcmp(buffer_cstr(&again), buffer_cstr(&formatted), formatted.length) != 0) {
            test_fail(__FILE__, __LINE__, "formatter is not idempotent");
            report_input("not idempotent", data, length);
        }
        buffer_free(&again);
    }

    buffer_free(&formatted);
    free(diags);
    free(html);
}

static const char *const FRAGMENTS[] = {
    "page", "column", "row", "heading", "text", "link", "button", "image", "Home",
    "About", "{", "}", "{", "}", "=", "\"x\"", "\"#fff\"", "\"#7c3aed\"", "\"center\"",
    "12", "36", "50%", "33.33%", "size", "color", "gap", "padding", "top", "inline",
    "width", "to", "alt", "title", "level", "align", "background", "\"a.html\"",
    "\"assets/a.png\"", "\n", " ", "\t", "# comment\n", ";", ":", "\"", "\\", "\r\n",
    "\r", "\xE2\x80\x9C", "\xC2\xA0", "\xEF\xBB\xBF", "-1", "1.5", "7", "1000", "\"\\\"\"",
    "\"<&>\"", "\xFF", "\x01", "%", "16px", "\"javascript:x\"",
};

static const char *const SEEDS[] = {
    "page Home {\n    column gap = 16 padding = 24 background = \"#f5f3ff\" {\n"
    "        heading \"Welcome\" size = 36 color = \"#7c3aed\" align = \"center\"\n"
    "        text \"Hello\" # comment\n"
    "        row { column width = 50% { text \"a\" } column { link \"b\" to = \"x.html\" } }\n"
    "        button \"Go\" to = \"#go\" image \"assets/a.png\" alt = \"A\"\n"
    "    }\n}\npage About title = \"About\" { text \"x\" }\n",
    "page A{row{column{heading\"a\"level=2}column padding top=4 padding=8{text\"b\"}}}",
};

static void fuzz_fragments(int iterations)
{
    Buffer input;
    buffer_init(&input);
    size_t fragment_count = sizeof(FRAGMENTS) / sizeof(FRAGMENTS[0]);

    for (int i = 0; i < iterations; i++) {
        buffer_clear(&input);
        unsigned pieces = 1 + random_below(60);
        for (unsigned k = 0; k < pieces; k++) {
            buffer_append_str(&input, FRAGMENTS[random_below((unsigned)fragment_count)]);
            if (random_below(3) == 0) {
                buffer_append_char(&input, ' ');
            }
        }
        check_input(buffer_cstr(&input), input.length);
    }

    buffer_free(&input);
}

static void fuzz_random_bytes(int iterations)
{
    char data[96];

    for (int i = 0; i < iterations; i++) {
        size_t length = random_below(sizeof(data));
        for (size_t k = 0; k < length; k++) {
            data[k] = (char)random_below(256);
        }
        check_input(data, length);
    }
}

static void fuzz_mutations(int iterations)
{
    Buffer input;
    buffer_init(&input);
    size_t seed_count = sizeof(SEEDS) / sizeof(SEEDS[0]);

    for (int i = 0; i < iterations; i++) {
        buffer_clear(&input);
        buffer_append_str(&input, SEEDS[random_below((unsigned)seed_count)]);

        unsigned mutations = 1 + random_below(4);
        for (unsigned m = 0; m < mutations && input.length > 0; m++) {
            size_t at = random_below((unsigned)input.length);
            unsigned kind = random_below(3);
            static const char interesting[] = "{}\"=#\\\n %:;0aZ";

            if (kind == 0) {
                /* delete one byte */
                memmove(input.data + at, input.data + at + 1, input.length - at);
                input.length--;
            } else if (kind == 1) {
                /* replace one byte */
                input.data[at] = interesting[random_below(sizeof(interesting) - 1)];
            } else {
                /* insert one byte */
                char c = interesting[random_below(sizeof(interesting) - 1)];
                Buffer grown;
                buffer_init(&grown);
                buffer_append(&grown, input.data, at);
                buffer_append_char(&grown, c);
                buffer_append(&grown, input.data + at, input.length - at);
                buffer_free(&input);
                input = grown;
            }
        }

        check_input(input.data, input.length);
    }

    buffer_free(&input);
}

static void fuzz_pathological(void)
{
    /* Deep nesting, long lines, long strings, many pages. */
    Buffer input;
    buffer_init(&input);

    buffer_append_str(&input, "page A ");
    for (int i = 0; i < 5000; i++) {
        buffer_append_str(&input, "{");
    }
    check_input(input.data, input.length);

    buffer_clear(&input);
    buffer_append_str(&input, "page A { text \"");
    for (int i = 0; i < 200000; i++) {
        buffer_append_char(&input, 'x');
    }
    buffer_append_str(&input, "\" }");
    check_input(input.data, input.length);

    buffer_clear(&input);
    for (int i = 0; i < 300; i++) {
        buffer_appendf(&input, "page P%d { text \"%d\" }\n", i, i);
    }
    check_input(input.data, input.length);

    buffer_clear(&input);
    for (int i = 0; i < 20000; i++) {
        buffer_append_str(&input, "}");
    }
    check_input(input.data, input.length);

    buffer_free(&input);
}

void run_fuzz_tests(void)
{
    fuzz_fragments(6000);
    fuzz_random_bytes(3000);
    fuzz_mutations(6000);
    fuzz_pathological();
}
