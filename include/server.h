#ifndef GENESIS_SERVER_H
#define GENESIS_SERVER_H

#include <stddef.h>

#define SERVER_DEFAULT_PORT 3000

/*
 * Runs `genesis dev`: builds the project, serves `<project>/dist/` on
 * http://127.0.0.1:<port>/, rebuilds when the source file or the assets
 * folder changes (checked twice a second), and reloads open browser tabs
 * after each rebuild. Runs until interrupted with Ctrl+C.
 *
 * Returns a process exit code: 0 after a clean shutdown, 1 if the server
 * could not start.
 */
int server_run(const char *entry_path, int port);

/*
 * Maps a request path such as "/about" or "/assets/a%20b.png" to a path
 * relative to dist/ ("about.html", "assets/a b.png"). Rejects anything that
 * could escape the output folder. Returns 1 on success.
 * Exposed for unit testing.
 */
int server_resolve_path(const char *request_path, char *out, size_t out_size);

/* Returns the Content-Type for a file name. Exposed for unit testing. */
const char *server_mime_type(const char *path);

#endif /* GENESIS_SERVER_H */
