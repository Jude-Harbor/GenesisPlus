#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "codegen.h"
#include "compiler.h"
#include "platform.h"
#include "version.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE (-1)
#define close_socket close
#endif

#define REQUEST_MAX 8192
#define TARGET_MAX 2048

/* ------------------------------------------------------------------------ */
/* Path resolution (pure functions, unit tested)                            */
/* ------------------------------------------------------------------------ */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_windows_device_name(const char *segment, size_t length)
{
    static const char *const names[] = {"con", "prn", "aux", "nul", "com", "lpt"};
    /* The name before the first '.' is what Windows checks. */
    size_t base = 0;
    while (base < length && segment[base] != '.') {
        base++;
    }

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i]);
        int match = 1;
        for (size_t k = 0; k < n && match; k++) {
            char c = k < base ? segment[k] : '\0';
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            match = c == names[i][k];
        }
        if (!match) {
            continue;
        }
        if (base == n) {
            return 1; /* con, prn, aux, nul */
        }
        if (base == n + 1 && (i >= 4) && segment[n] >= '0' && segment[n] <= '9') {
            return 1; /* com1, lpt9 */
        }
    }
    return 0;
}

int server_resolve_path(const char *request_path, char *out, size_t out_size)
{
    if (request_path == NULL || request_path[0] != '/' || out_size < 16) {
        return 0;
    }

    char decoded[TARGET_MAX + 1];
    size_t length = 0;

    for (const char *p = request_path; *p != '\0' && *p != '?' && *p != '#'; p++) {
        char c = *p;

        if (c == '%') {
            int high = hex_value(p[1]);
            int low = high >= 0 ? hex_value(p[2]) : -1;
            if (high < 0 || low < 0) {
                return 0;
            }
            c = (char)(high * 16 + low);
            p += 2;
        }

        if (c == '\0' || c == '\\' || c == ':' || (unsigned char)c < 0x20) {
            return 0;
        }

        if (length >= TARGET_MAX) {
            return 0;
        }
        decoded[length++] = c;
    }
    decoded[length] = '\0';

    /* Rebuild the path segment by segment, rejecting anything unsafe. */
    size_t used = 0;
    const char *segment = decoded;
    int ends_with_slash = length == 0 || decoded[length - 1] == '/';

    while (*segment != '\0') {
        while (*segment == '/') {
            segment++;
        }
        if (*segment == '\0') {
            break;
        }

        size_t seg_len = strcspn(segment, "/");

        /* No ".", "..", or hidden files such as ".git". */
        if (segment[0] == '.') {
            return 0;
        }
        if (is_windows_device_name(segment, seg_len)) {
            return 0;
        }
        /* Windows silently strips trailing dots and spaces from names. */
        if (segment[seg_len - 1] == ' ' || segment[seg_len - 1] == '.') {
            return 0;
        }

        if (used + seg_len + 2 >= out_size) {
            return 0;
        }
        if (used > 0) {
            out[used++] = '/';
        }
        memcpy(out + used, segment, seg_len);
        used += seg_len;
        segment += seg_len;
    }

    if (ends_with_slash) {
        const char *index = used > 0 ? "/index.html" : "index.html";
        size_t index_len = strlen(index);
        if (used + index_len + 1 > out_size) {
            return 0;
        }
        memcpy(out + used, index, index_len);
        used += index_len;
    }

    out[used] = '\0';
    return used > 0;
}

static int ends_with(const char *text, const char *suffix)
{
    size_t a = strlen(text);
    size_t b = strlen(suffix);
    if (b > a) {
        return 0;
    }
    for (size_t i = 0; i < b; i++) {
        char c = text[a - b + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

const char *server_mime_type(const char *path)
{
    static const struct {
        const char *extension;
        const char *type;
    } types[] = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".json", "application/json"},
        {".txt", "text/plain; charset=utf-8"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".avif", "image/avif"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".mp3", "audio/mpeg"},
        {".pdf", "application/pdf"},
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (ends_with(path, types[i].extension)) {
            return types[i].type;
        }
    }

    return "application/octet-stream";
}

/* ------------------------------------------------------------------------ */
/* Server state                                                             */
/* ------------------------------------------------------------------------ */

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

typedef struct {
    const char *entry_path;
    Buffer dist_dir;
    Buffer assets_dir;
    unsigned long long version; /* increases after every rebuild */
    int build_ok;
    Buffer build_log;
    unsigned long long source_stamp;
    unsigned long long assets_stamp;
} DevState;

/* Polls the browser for a new build and reloads the page when it changes.
   Only injected into pages served by `genesis dev`, never into dist/. */
static const char RELOAD_SCRIPT[] =
    "<script>/* Genesis+ dev server live reload */(function(){var v=null;"
    "setInterval(function(){fetch('/__genesis/version',{cache:'no-store'})"
    ".then(function(r){return r.text();}).then(function(t){if(v===null){v=t;}"
    "else if(t!==v){location.reload();}}).catch(function(){});},700);})();"
    "</script>\n";

static void stamp_inputs(DevState *state, unsigned long long *source,
                         unsigned long long *assets)
{
    platform_file_stamp(state->entry_path, source);
    platform_tree_stamp(buffer_cstr(&state->assets_dir), assets);
}

static void rebuild(DevState *state)
{
    buffer_clear(&state->build_log);
    stamp_inputs(state, &state->source_stamp, &state->assets_stamp);
    state->build_ok = compiler_build(state->entry_path, &state->build_log, 0);
    state->version++;

    FILE *stream = state->build_ok ? stdout : stderr;
    fputs(buffer_cstr(&state->build_log), stream);
    if (state->build_ok) {
        fputs("Ready. Open pages reload automatically.\n", stream);
    } else {
        fputs("Waiting for you to fix the error above...\n", stream);
    }
    fflush(stream);
}

/* ------------------------------------------------------------------------ */
/* HTTP                                                                     */
/* ------------------------------------------------------------------------ */

/* Winsock takes int lengths; POSIX takes size_t. */
static int socket_send(socket_t sock, const char *data, size_t length)
{
#if defined(_WIN32)
    return send(sock, data, (int)length, 0);
#else
    return (int)send(sock, data, length, 0);
#endif
}

static int socket_receive(socket_t sock, char *data, size_t length)
{
#if defined(_WIN32)
    return recv(sock, data, (int)length, 0);
#else
    return (int)recv(sock, data, length, 0);
#endif
}

static int send_all(socket_t client, const char *data, size_t length)
{
    while (length > 0) {
        size_t chunk = length > 65536 ? 65536 : length;
        int sent = socket_send(client, data, chunk);
        if (sent <= 0) {
            return 0;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return 1;
}

static void send_response(socket_t client, int status, const char *reason,
                          const char *content_type, const char *body, size_t body_length,
                          int head_only, const char *extra_headers)
{
    Buffer headers;
    buffer_init(&headers);
    buffer_appendf(&headers,
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Cache-Control: no-store\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Connection: close\r\n"
                   "%s"
                   "\r\n",
                   status, reason, content_type, body_length,
                   extra_headers != NULL ? extra_headers : "");

    if (!headers.failed && send_all(client, headers.data, headers.length) && !head_only &&
        body_length > 0) {
        send_all(client, body, body_length);
    }

    buffer_free(&headers);
}

static void send_message_page(socket_t client, int status, const char *reason,
                              const char *title, const char *details, int head_only)
{
    Buffer page;
    buffer_init(&page);
    buffer_append_str(&page,
                      "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                      "<title>");
    codegen_escape_html(&page, title);
    buffer_append_str(&page,
                      "</title>\n<style>body{font-family:system-ui,sans-serif;margin:0;"
                      "padding:2rem;background:#1e1b2e;color:#f4f2ff}h1{margin-top:0;"
                      "color:#c4b5fd}pre{white-space:pre-wrap;background:#2a2540;padding:1rem;"
                      "border-radius:8px;line-height:1.5;font-size:14px;overflow-x:auto}"
                      "</style>\n</head>\n<body>\n<h1>");
    codegen_escape_html(&page, title);
    buffer_append_str(&page, "</h1>\n<pre>");
    codegen_escape_html(&page, details);
    buffer_append_str(&page, "</pre>\n<p>This page reloads automatically.</p>\n");
    buffer_append_str(&page, RELOAD_SCRIPT);
    buffer_append_str(&page, "</body>\n</html>\n");

    send_response(client, status, reason, "text/html; charset=utf-8", buffer_cstr(&page),
                  page.length, head_only, NULL);
    buffer_free(&page);
}

/* Finds a servable file for `relative` inside dist. */
static int find_file(const DevState *state, const char *relative, Buffer *path)
{
    buffer_clear(path);
    path_join(path, buffer_cstr(&state->dist_dir), relative);
    if (platform_is_file(buffer_cstr(path))) {
        return 1;
    }

    /* "/about" -> about.html */
    const char *last_slash = strrchr(relative, '/');
    const char *name = last_slash != NULL ? last_slash + 1 : relative;
    if (strchr(name, '.') == NULL) {
        buffer_append_str(path, ".html");
        if (platform_is_file(buffer_cstr(path))) {
            return 1;
        }

        /* "/docs" -> docs/index.html */
        buffer_clear(path);
        path_join(path, buffer_cstr(&state->dist_dir), relative);
        buffer_append_str(path, "/index.html");
        if (platform_is_file(buffer_cstr(path))) {
            return 1;
        }
    }

    return 0;
}

static void serve_file(socket_t client, const char *path, int head_only)
{
    char *data = NULL;
    size_t length = 0;
    Buffer error;
    buffer_init(&error);

    if (!platform_read_file(path, &data, &length, &error)) {
        send_message_page(client, 500, "Internal Server Error", "Could not read file",
                          buffer_cstr(&error), head_only);
        buffer_free(&error);
        return;
    }

    const char *type = server_mime_type(path);

    if (strncmp(type, "text/html", 9) == 0) {
        /* Inject live reload before the last </body>, or at the end. */
        Buffer page;
        buffer_init(&page);
        const char *body_end = NULL;
        for (const char *p = data; (p = strstr(p, "</body>")) != NULL; p++) {
            body_end = p;
        }
        size_t split = body_end != NULL ? (size_t)(body_end - data) : length;
        buffer_append(&page, data, split);
        buffer_append_str(&page, RELOAD_SCRIPT);
        buffer_append(&page, data + split, length - split);
        send_response(client, 200, "OK", type, buffer_cstr(&page), page.length, head_only,
                      NULL);
        buffer_free(&page);
    } else {
        send_response(client, 200, "OK", type, data, length, head_only, NULL);
    }

    free(data);
    buffer_free(&error);
}

static void set_receive_timeout(socket_t client, int milliseconds)
{
#if defined(_WIN32)
    DWORD timeout = (DWORD)milliseconds;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

/*
 * Closes a client connection gracefully. Closing a socket that still has
 * unread request data makes the operating system send a TCP reset, and the
 * client may then lose the response we just sent (for example a 431 for
 * oversized headers). So: stop sending, read and discard what is left for a
 * short time, then close.
 */
static void close_client(socket_t client)
{
#if defined(_WIN32)
    shutdown(client, SD_SEND);
#else
    shutdown(client, SHUT_WR);
#endif
    set_receive_timeout(client, 200);

    /* Bounded in bytes and in reads, so a slow client cannot stall the
       server for long. */
    char discard[4096];
    size_t drained = 0;
    for (int reads = 0; reads < 64 && drained < 256u * 1024u; reads++) {
        int got = socket_receive(client, discard, sizeof(discard));
        if (got <= 0) {
            break;
        }
        drained += (size_t)got;
    }

    close_socket(client);
}

static void handle_client(DevState *state, socket_t client)
{
    char request[REQUEST_MAX + 1];
    size_t length = 0;

    set_receive_timeout(client, 2000);

    while (length < REQUEST_MAX) {
        int got = socket_receive(client, request + length, REQUEST_MAX - length);
        if (got <= 0) {
            break;
        }
        length += (size_t)got;
        request[length] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL || strstr(request, "\n\n") != NULL) {
            break;
        }
    }
    request[length] = '\0';

    if (length == 0) {
        return;
    }

    if (strstr(request, "\r\n\r\n") == NULL && strstr(request, "\n\n") == NULL) {
        if (length >= REQUEST_MAX) {
            send_message_page(client, 431, "Request Header Fields Too Large",
                              "Request too large", "The request headers are too large.", 0);
        } else {
            send_message_page(client, 400, "Bad Request", "Bad request",
                              "The request was incomplete.", 0);
        }
        return;
    }

    /* Request line: METHOD SP TARGET SP VERSION */
    char method[16];
    char target[TARGET_MAX + 1];
    size_t i = 0;
    size_t m = 0;

    while (i < length && request[i] != ' ' && m < sizeof(method) - 1) {
        method[m++] = request[i++];
    }
    method[m] = '\0';

    if (i >= length || request[i] != ' ') {
        send_message_page(client, 400, "Bad Request", "Bad request",
                          "The request line is malformed.", 0);
        return;
    }
    i++;

    size_t t = 0;
    while (i < length && request[i] != ' ' && request[i] != '\r' && request[i] != '\n') {
        if (t >= TARGET_MAX) {
            send_message_page(client, 414, "URI Too Long", "Address too long",
                              "The requested address is too long.", 0);
            return;
        }
        target[t++] = request[i++];
    }
    target[t] = '\0';

    int head_only = strcmp(method, "HEAD") == 0;

    if (strcmp(method, "GET") != 0 && !head_only) {
        send_response(client, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                      "Method not allowed\n", 19, 0, "Allow: GET, HEAD\r\n");
        return;
    }

    if (strcmp(target, "/__genesis/version") == 0) {
        char body[32];
        int body_length = snprintf(body, sizeof(body), "%llu", state->version);
        send_response(client, 200, "OK", "text/plain; charset=utf-8", body,
                      (size_t)body_length, head_only, NULL);
        return;
    }

    char relative[TARGET_MAX + 32];

    if (!server_resolve_path(target, relative, sizeof(relative))) {
        send_message_page(client, 400, "Bad Request", "Invalid address",
                          "That address is not allowed.", head_only);
        return;
    }

    /* While the build is broken, show the error instead of stale pages. */
    const char *mime = server_mime_type(relative);
    int wants_page = strncmp(mime, "text/html", 9) == 0 || strchr(relative, '.') == NULL;

    if (!state->build_ok && wants_page) {
        send_message_page(client, 500, "Internal Server Error", "Genesis+ build failed",
                          buffer_cstr(&state->build_log), head_only);
        return;
    }

    Buffer path;
    buffer_init(&path);

    if (find_file(state, relative, &path)) {
        serve_file(client, buffer_cstr(&path), head_only);
    } else {
        Buffer details;
        buffer_init(&details);
        buffer_appendf(&details, "There is no file for %s in the generated website.", target);
        send_message_page(client, 404, "Not Found", "Page not found", buffer_cstr(&details),
                          head_only);
        buffer_free(&details);
    }

    buffer_free(&path);
}

/* ------------------------------------------------------------------------ */
/* Main loop                                                                */
/* ------------------------------------------------------------------------ */

static int last_socket_error_is_in_use(void)
{
#if defined(_WIN32)
    int code = WSAGetLastError();
    return code == WSAEADDRINUSE || code == WSAEACCES;
#else
    return errno == EADDRINUSE;
#endif
}

/* Waits up to `milliseconds` for a pending connection. Returns > 0 when one
   is ready, 0 on timeout, < 0 on error (for example when interrupted). */
static int wait_for_connection(socket_t listener, int milliseconds)
{
#if defined(_WIN32)
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener, &readable);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = milliseconds * 1000;

    int ready = select(0, &readable, NULL, NULL, &timeout);
    return ready > 0 && FD_ISSET(listener, &readable) ? 1 : ready;
#else
    struct pollfd entry;
    entry.fd = listener;
    entry.events = POLLIN;
    entry.revents = 0;

    int ready = poll(&entry, 1, milliseconds);
    return ready > 0 && (entry.revents & POLLIN) ? 1 : ready;
#endif
}

static socket_t open_listener(int port)
{
    socket_t listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener == INVALID_SOCKET_VALUE) {
        fprintf(stderr, "Genesis+ error: could not create a network socket.\n");
        return INVALID_SOCKET_VALUE;
    }

    int yes = 1;
#if defined(_WIN32)
    /* Make a busy port fail instead of being silently shared. */
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&yes, sizeof(yes));
#else
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(0x7f000001UL); /* localhost only */

    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0) {
        if (last_socket_error_is_in_use()) {
            fprintf(stderr,
                    "Genesis+ error: port %d is already in use.\n"
                    "  Stop the other program using it, or pick another port:\n"
                    "  genesis dev --port %d\n",
                    port, port < 65535 ? port + 1 : 3001);
        } else {
            fprintf(stderr, "Genesis+ error: could not listen on port %d.\n", port);
        }
        close_socket(listener);
        return INVALID_SOCKET_VALUE;
    }

    if (listen(listener, 16) != 0) {
        fprintf(stderr, "Genesis+ error: could not listen on port %d.\n", port);
        close_socket(listener);
        return INVALID_SOCKET_VALUE;
    }

    return listener;
}

int server_run(const char *entry_path, int port)
{
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Genesis+ error: could not start Windows networking.\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    DevState state;
    memset(&state, 0, sizeof(state));
    state.entry_path = entry_path;
    buffer_init(&state.dist_dir);
    buffer_init(&state.assets_dir);
    buffer_init(&state.build_log);

    Buffer project_dir;
    buffer_init(&project_dir);
    path_dirname(&project_dir, entry_path);
    path_join(&state.dist_dir, buffer_cstr(&project_dir), "dist");
    path_join(&state.assets_dir, buffer_cstr(&project_dir), "assets");
    buffer_free(&project_dir);

    socket_t listener = open_listener(port);

    if (listener == INVALID_SOCKET_VALUE) {
        buffer_free(&state.dist_dir);
        buffer_free(&state.assets_dir);
        buffer_free(&state.build_log);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    rebuild(&state);

    printf("\nGenesis+ %s dev server\n"
           "  Serving  %s/\n"
           "  Open     http://localhost:%d/\n"
           "  Watching %s and %s/ for changes. Press Ctrl+C to stop.\n\n",
           GENESIS_VERSION, buffer_cstr(&state.dist_dir), port, entry_path,
           buffer_cstr(&state.assets_dir));
    fflush(stdout);

    stop_requested = 0;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int ticks = 0;

    while (!stop_requested) {
        int ready = wait_for_connection(listener, 250);

        if (ready > 0) {
            socket_t client = accept(listener, NULL, NULL);
            if (client != INVALID_SOCKET_VALUE) {
                handle_client(&state, client);
                close_client(client);
            }
        }

        /* Check for changes about twice a second. */
        if (++ticks >= 2 || ready > 0) {
            ticks = 0;
            unsigned long long source = 0;
            unsigned long long assets = 0;
            stamp_inputs(&state, &source, &assets);

            if (source != state.source_stamp || assets != state.assets_stamp) {
                /* Give editors a moment to finish writing the file. */
                platform_sleep_ms(80);
                printf("\nChange detected, rebuilding...\n");
                rebuild(&state);
            }
        }
    }

    close_socket(listener);
#if defined(_WIN32)
    WSACleanup();
#endif

    printf("\nGenesis+ dev server stopped.\n");

    buffer_free(&state.dist_dir);
    buffer_free(&state.assets_dir);
    buffer_free(&state.build_log);
    return 0;
}
