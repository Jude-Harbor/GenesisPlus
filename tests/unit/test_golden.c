/*
 * Golden-file tests: tests/golden/<name>.gns is compiled and every output
 * file is compared byte-for-byte with tests/golden/<name>.expected.
 *
 * To intentionally update the expected files after a deliberate output
 * change, run:  GENESIS_UPDATE_GOLDEN=1 ./build/unit_tests
 * and review the resulting diff before committing it.
 */

#include <stdlib.h>

#include "platform.h"
#include "test.h"

static const char *const CASES[] = {
    "elements",
    "layout",
    "padding",
    "multipage",
    "escaping",
};

static void render_outputs(const Compilation *compilation, Buffer *out)
{
    for (size_t i = 0; i < compilation->file_count; i++) {
        buffer_appendf(out, "===== %s =====\n", compilation->files[i].name);
        buffer_append(out, compilation->files[i].content.data,
                      compilation->files[i].content.length);
    }
}

static void run_case(const char *name, int update)
{
    Buffer source_path;
    Buffer expected_path;
    buffer_init(&source_path);
    buffer_init(&expected_path);
    buffer_appendf(&source_path, "tests/golden/%s.gns", name);
    buffer_appendf(&expected_path, "tests/golden/%s.expected", name);

    char *source = NULL;
    size_t length = 0;
    Buffer error;
    buffer_init(&error);
    test_checks++;

    if (!platform_read_file(source_path.data, &source, &length, &error)) {
        test_fail(__FILE__, __LINE__, "could not read golden source (run tests from the repo root)");
        fprintf(stderr, "    %s\n", buffer_cstr(&error));
        goto done;
    }

    Compilation compilation;
    if (!compiler_compile(&compilation, source_path.data, source, length, NULL)) {
        test_fail(__FILE__, __LINE__, "golden source failed to compile");
        Buffer text;
        buffer_init(&text);
        diag_format(&compilation.diags, &text);
        fprintf(stderr, "%s", buffer_cstr(&text));
        buffer_free(&text);
        compilation_free(&compilation);
        goto done;
    }

    Buffer actual;
    buffer_init(&actual);
    render_outputs(&compilation, &actual);
    compilation_free(&compilation);

    if (update) {
        buffer_clear(&error);
        if (!platform_write_file_atomic(expected_path.data, actual.data, actual.length,
                                        &error)) {
            test_fail(__FILE__, __LINE__, "could not update golden file");
        } else {
            printf("  updated %s\n", expected_path.data);
        }
    } else {
        char *expected = NULL;
        size_t expected_length = 0;
        buffer_clear(&error);
        if (!platform_read_file(expected_path.data, &expected, &expected_length, &error)) {
            test_fail(__FILE__, __LINE__, "missing golden file");
            fprintf(stderr, "    %s\n", buffer_cstr(&error));
        } else if (expected_length != actual.length ||
                   memcmp(expected, actual.data, actual.length) != 0) {
            test_fail(__FILE__, __LINE__, "output differs from golden file");
            /* Show the first differing line. */
            size_t i = 0;
            size_t line = 1;
            while (i < expected_length && i < actual.length && expected[i] == actual.data[i]) {
                if (expected[i] == '\n') {
                    line++;
                }
                i++;
            }
            fprintf(stderr, "    %s: first difference at line %zu\n", expected_path.data,
                    line);
        }
        free(expected);
    }

    buffer_free(&actual);

done:
    free(source);
    buffer_free(&error);
    buffer_free(&source_path);
    buffer_free(&expected_path);
}

void run_golden_tests(void)
{
    const char *update_env = getenv("GENESIS_UPDATE_GOLDEN");
    int update = update_env != NULL && strcmp(update_env, "1") == 0;

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        run_case(CASES[i], update);
    }
}
