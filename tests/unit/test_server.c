#include "server.h"
#include "test.h"

static void check_resolves(const char *file, int line, const char *request,
                           const char *expected)
{
    char out[256];
    int ok = server_resolve_path(request, out, sizeof(out));
    test_checks++;

    if (expected == NULL) {
        if (ok) {
            test_fail(file, line, "unsafe path was accepted");
            fprintf(stderr, "    request: %s -> %s\n", request, out);
        }
        return;
    }

    if (!ok || strcmp(out, expected) != 0) {
        test_fail(file, line, "path resolved incorrectly");
        fprintf(stderr, "    request: %s\n    expected: %s\n    actual: %s\n", request,
                expected, ok ? out : "(rejected)");
    }
}

#define RESOLVES(request, expected) check_resolves(__FILE__, __LINE__, request, expected)
#define REJECTED(request) check_resolves(__FILE__, __LINE__, request, NULL)

static void test_path_resolution(void)
{
    RESOLVES("/", "index.html");
    RESOLVES("/index.html", "index.html");
    RESOLVES("/about", "about");
    RESOLVES("/about.html?x=1#y", "about.html");
    RESOLVES("/assets/a%20b.png", "assets/a b.png");
    RESOLVES("/docs/", "docs/index.html");
    RESOLVES("//assets///x.png", "assets/x.png");
    RESOLVES("/style.css", "style.css");
}

static void test_path_traversal_rejected(void)
{
    REJECTED("");
    REJECTED("index.html");
    REJECTED("/../secret");
    REJECTED("/assets/../../etc/passwd");
    REJECTED("/%2e%2e/secret");
    REJECTED("/%2E%2E%2Fsecret");
    REJECTED("/assets/..%2f..%2fx");
    REJECTED("/..\\x");
    REJECTED("/%5c..%5cx");
    REJECTED("/C:/Windows/win.ini");
    REJECTED("/C%3A/Windows");
    REJECTED("/.git/config");
    REJECTED("/./index.html");
    REJECTED("/a%00b");
    REJECTED("/bad%zz");
    REJECTED("/bad%4");
    REJECTED("/con");
    REJECTED("/NUL.txt");
    REJECTED("/assets/com1.png");
    REJECTED("/lpt9");
    REJECTED("/trailing.");
    REJECTED("/trailing%20");
    RESOLVES("/console.html", "console.html");
    RESOLVES("/com10", "com10");
}

static void test_mime_types(void)
{
    CHECK_STR(server_mime_type("index.html"), "text/html; charset=utf-8");
    CHECK_STR(server_mime_type("style.css"), "text/css; charset=utf-8");
    CHECK_STR(server_mime_type("a/b.PNG"), "image/png");
    CHECK_STR(server_mime_type("x.jpeg"), "image/jpeg");
    CHECK_STR(server_mime_type("x.svg"), "image/svg+xml");
    CHECK_STR(server_mime_type("x.woff2"), "font/woff2");
    CHECK_STR(server_mime_type("noext"), "application/octet-stream");
}

void run_server_tests(void)
{
    test_path_resolution();
    test_path_traversal_rejected();
    test_mime_types();
}
