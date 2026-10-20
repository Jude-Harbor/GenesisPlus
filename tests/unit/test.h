#ifndef GENESIS_TEST_H
#define GENESIS_TEST_H

/*
 * A deliberately tiny test framework. Each test is a void function that
 * uses CHECK macros; failures are reported with file and line and the run
 * continues so that one problem does not hide others.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "diag.h"

extern int test_failures;
extern int test_checks;

void test_fail(const char *file, int line, const char *message);

#define CHECK(condition)                                                       \
    do {                                                                       \
        test_checks++;                                                         \
        if (!(condition)) {                                                    \
            test_fail(__FILE__, __LINE__, "CHECK(" #condition ")");            \
        }                                                                      \
    } while (0)

#define CHECK_STR(actual, expected)                                            \
    do {                                                                       \
        const char *check_a_ = (actual);                                       \
        const char *check_e_ = (expected);                                     \
        test_checks++;                                                         \
        if (check_a_ == NULL || strcmp(check_a_, check_e_) != 0) {             \
            test_fail(__FILE__, __LINE__, "strings differ");                   \
            fprintf(stderr, "    expected: %s\n    actual:   %s\n", check_e_,  \
                    check_a_ ? check_a_ : "(null)");                           \
        }                                                                      \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                       \
    do {                                                                       \
        const char *check_h_ = (haystack);                                     \
        const char *check_n_ = (needle);                                       \
        test_checks++;                                                         \
        if (check_h_ == NULL || strstr(check_h_, check_n_) == NULL) {          \
            test_fail(__FILE__, __LINE__, "text not found");                   \
            fprintf(stderr, "    looking for: %s\n    in: %s\n", check_n_,     \
                    check_h_ ? check_h_ : "(null)");                           \
        }                                                                      \
    } while (0)

#define CHECK_NOT_CONTAINS(haystack, needle)                                   \
    do {                                                                       \
        const char *check_h_ = (haystack);                                     \
        const char *check_n_ = (needle);                                       \
        test_checks++;                                                         \
        if (check_h_ == NULL || strstr(check_h_, check_n_) != NULL) {          \
            test_fail(__FILE__, __LINE__, "unexpected text found");            \
            fprintf(stderr, "    did not expect: %s\n    in: %s\n", check_n_,  \
                    check_h_ ? check_h_ : "(null)");                           \
        }                                                                      \
    } while (0)

/* ---- Helpers shared by the test files (defined in test_main.c) ---- */

/* Compiles `source` (no image file checks). Returns 1 on success. The
   compilation must be released with compilation_free(). */
int compile_text(const char *source, Compilation *out);

/* Returns the HTML of the first page as a new heap string, or NULL (and
   prints the diagnostics) if compilation failed. */
char *html_of(const char *source);

/* Returns the formatted diagnostics for `source` as a heap string. */
char *diagnostics_of(const char *source, size_t *errors, size_t *warnings);

/* Checks that `source` fails with an error at line:column whose message
   (or hint) contains `needle`. Pass column 0 to skip the column check. */
void expect_error_at(const char *file, int line_no, const char *source, size_t line,
                     size_t column, const char *needle);

#define EXPECT_ERROR(source, line, column, needle)                             \
    expect_error_at(__FILE__, __LINE__, (source), (line), (column), (needle))

/* Checks that `source` compiles without errors. */
void expect_ok(const char *file, int line_no, const char *source);
#define EXPECT_OK(source) expect_ok(__FILE__, __LINE__, (source))

/* Test suites. */
void run_lexer_tests(void);
void run_parser_tests(void);
void run_codegen_tests(void);
void run_formatter_tests(void);
void run_server_tests(void);
void run_fuzz_tests(void);
void run_golden_tests(void);

#endif /* GENESIS_TEST_H */
