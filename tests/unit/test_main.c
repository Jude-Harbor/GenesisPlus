#include <stdlib.h>

#include "test.h"

int test_failures = 0;
int test_checks = 0;

void test_fail(const char *file, int line, const char *message)
{
    test_failures++;
    fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, message);
}

int compile_text(const char *source, Compilation *out)
{
    return compiler_compile(out, "test.gns", source, strlen(source), NULL);
}

static char *duplicate(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

char *html_of(const char *source)
{
    Compilation compilation;
    char *html = NULL;

    if (compile_text(source, &compilation)) {
        html = duplicate(buffer_cstr(&compilation.files[0].content));
    } else {
        Buffer text;
        buffer_init(&text);
        diag_format(&compilation.diags, &text);
        fprintf(stderr, "    unexpected compile failure:\n%s", buffer_cstr(&text));
        buffer_free(&text);
    }

    compilation_free(&compilation);
    return html;
}

char *diagnostics_of(const char *source, size_t *errors, size_t *warnings)
{
    Compilation compilation;
    compile_text(source, &compilation);

    Buffer text;
    buffer_init(&text);
    diag_format(&compilation.diags, &text);

    if (errors != NULL) {
        *errors = compilation.diags.error_count;
    }
    if (warnings != NULL) {
        *warnings = compilation.diags.warning_count;
    }

    char *result = duplicate(buffer_cstr(&text));
    buffer_free(&text);
    compilation_free(&compilation);
    return result;
}

void expect_error_at(const char *file, int line_no, const char *source, size_t line,
                     size_t column, const char *needle)
{
    Compilation compilation;
    int ok = compile_text(source, &compilation);
    test_checks++;

    if (ok || compilation.diags.error_count == 0) {
        test_fail(file, line_no, "expected a compile error, but compilation succeeded");
        fprintf(stderr, "    source: %s\n", source);
        compilation_free(&compilation);
        return;
    }

    int found = 0;
    for (size_t i = 0; i < compilation.diags.count; i++) {
        const Diagnostic *d = &compilation.diags.items[i];
        if (d->severity != DIAG_ERROR) {
            continue;
        }
        if (d->line == line && (column == 0 || d->column == column) &&
            (strstr(d->message, needle) != NULL || strstr(d->hint, needle) != NULL)) {
            found = 1;
        }
    }

    if (!found) {
        test_fail(file, line_no, "expected error not reported");
        fprintf(stderr, "    wanted line %zu col %zu containing \"%s\"\n    got:\n", line,
                column, needle);
        for (size_t i = 0; i < compilation.diags.count; i++) {
            const Diagnostic *d = &compilation.diags.items[i];
            fprintf(stderr, "      %zu:%zu %s | %s\n", d->line, d->column, d->message,
                    d->hint);
        }
    }

    compilation_free(&compilation);
}

void expect_ok(const char *file, int line_no, const char *source)
{
    Compilation compilation;
    int ok = compile_text(source, &compilation);
    test_checks++;

    if (!ok) {
        test_fail(file, line_no, "expected compilation to succeed");
        Buffer text;
        buffer_init(&text);
        diag_format(&compilation.diags, &text);
        fprintf(stderr, "    source: %s\n%s", source, buffer_cstr(&text));
        buffer_free(&text);
    }

    compilation_free(&compilation);
}

typedef struct {
    const char *name;
    void (*run)(void);
} Suite;

int main(void)
{
    static const Suite suites[] = {
        {"lexer", run_lexer_tests},
        {"parser", run_parser_tests},
        {"codegen", run_codegen_tests},
        {"formatter", run_formatter_tests},
        {"server", run_server_tests},
        {"golden", run_golden_tests},
        {"fuzz", run_fuzz_tests},
    };

    int failed_suites = 0;

    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        int before = test_failures;
        int checks_before = test_checks;
        suites[i].run();
        int failures = test_failures - before;
        printf("%-10s %s (%d checks%s)\n", suites[i].name, failures == 0 ? "ok  " : "FAIL",
               test_checks - checks_before, failures == 0 ? "" : ", see errors above");
        if (failures > 0) {
            failed_suites++;
        }
    }

    if (test_failures > 0) {
        printf("\n%d check%s FAILED in %d suite%s.\n", test_failures,
               test_failures == 1 ? "" : "s", failed_suites, failed_suites == 1 ? "" : "s");
        return 1;
    }

    printf("\nAll %d checks passed.\n", test_checks);
    return 0;
}
