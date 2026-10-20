# Command-line reference

```
genesis <command> [arguments] [options]
```

Commands that take a `file-or-folder` argument accept:

- nothing: use `lead.gns` in the current folder;
- a folder: use `lead.gns` inside it;
- a file ending in `.gns`.

The project folder is the folder containing the source file. Output goes to `dist/` and images are read from `assets/` inside it.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success (warnings do not change this). |
| `1` | The source has errors, a file could not be read or written, or the server could not start. |
| `2` | The command line was invalid (unknown command or option, missing value, too many arguments). |

## `genesis build [file-or-folder]`

Compiles the project and writes the website to `dist/`:

- one `.html` file per page (`index.html` for the first page, `<name>.html` for the others);
- `style.css`, always written;
- `assets/`, a copy of the project's `assets` folder (including subfolders), if it exists.

Behaviour:

- The whole program is compiled in memory first. If there is any error, **nothing is written** and the previous contents of `dist/` are left untouched.
- Each file is written to a temporary file and then moved into place, so a crash or full disk never leaves a half-written page behind.
- `dist/` is created if needed. Only the files listed above are created or replaced; other files in `dist/` are left alone, and nothing is ever deleted.
- If `dist` exists as a file, the build fails with an explanation.

```
$ genesis build
Built lead.gns -> ./dist/
  index.html
  about.html
  style.css
  assets/ (copied)
```

## `genesis dev [file-or-folder] [--port N]`

Builds the project, then serves `dist/` at `http://localhost:3000/`.

- **Only reachable from your computer:** the server listens on `127.0.0.1`.
- **Rebuilds automatically.** About twice a second it checks whether the source file or anything in `assets/` changed, and rebuilds.
- **Reloads the browser.** Pages served by the dev server include a small script that checks `/__genesis/version` and reloads the page after each rebuild. This script is added only to pages sent by the dev server; the files in `dist/` never contain it.
- **Shows errors in the browser.** While the source has errors, pages show the compiler's error message instead of the old site. The server keeps running and recovers as soon as the file is fixed.
- **Friendly addresses.** `/` serves `index.html`, `/about` serves `about.html`, and `/docs/` serves `docs/index.html`.
- **Safe paths.** Only files inside `dist/` are served. Requests containing `..`, backslashes, `:`, hidden files (names starting with `.`), Windows device names (such as `con`), encoded NUL bytes, or invalid percent-encoding are rejected with status 400. Only `GET` and `HEAD` are allowed (others get 405).
- **Malformed requests.** Request targets longer than 2048 bytes get 414, and requests whose headers exceed 8 KB get 431. The server always finishes sending its response before closing the connection.
- **Ports.** `--port N` chooses another port (1024–65535). If the port is in use, the server explains this and exits with status 1.
- **Stopping.** Press `Ctrl+C`. The server closes its socket and exits with status 0.

The dev server is a development tool. It handles one request at a time and is not meant to host a public website; use `genesis build` and a static web host for that.

## `genesis check [file-or-folder] [--machine]`

Reports errors and warnings without writing any files. Exits with `1` if there are errors.

```
$ genesis check
lead.gns: no problems found.
```

### For editors: `--stdin`, `--stdin-filename`, `--machine`

`genesis check --stdin` reads the source from standard input. `--stdin-filename PATH` sets the name used in messages; local images are looked up relative to that file's folder (without it, the current folder is used).

`--machine` prints one line per problem, separated by tabs, on standard output:

```
severity<TAB>line<TAB>column<TAB>length<TAB>message
```

- `severity` is `error` or `warning`;
- `line` and `column` are 1-based; columns count Unicode code points;
- `length` is the number of code points the problem covers (at least 1);
- `message` is a single line; a hint, if any, is appended as ` Hint: ...`.

The VS Code extension uses this format.

## `genesis fmt [file-or-folder] [--check]`

Rewrites the file in the standard style:

- one element per line, with its properties on the same line;
- four spaces of indentation per block;
- `{` at the end of the opening line and `}` on its own line;
- single spaces around `=`;
- comments kept; at most one blank line kept between statements; one blank line between pages;
- semicolons removed;
- line endings kept (CRLF stays CRLF).

Files with errors are never changed. `--check` does not write anything; it exits with `1` if the file is not formatted.

`genesis fmt --stdin` reads standard input and writes the formatted result to standard output (add `--machine` to get errors in the machine-readable format).

## `genesis init [folder]`

Creates a starter project in the folder (default: the current folder), creating the folder if needed:

- `lead.gns`, a small example page;
- `assets/`;
- `.gitignore` containing `dist/`.

Existing files are never overwritten.

## `genesis help [command]`, `genesis --help`, `genesis --version`

Show general help, help for one command (`genesis help dev`, or `genesis dev --help`), or the version (`genesis --version`, `genesis -v`).
