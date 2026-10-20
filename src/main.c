#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "compiler.h"
#include "diag.h"
#include "formatter.h"
#include "platform.h"
#include "server.h"
#include "version.h"

/* Exit codes. */
#define EXIT_OK 0
#define EXIT_FAILED 1 /* compile errors, failed build, failed check */
#define EXIT_USAGE 2  /* invalid command-line arguments */

static void print_help(FILE *stream)
{
    fputs("Genesis+ " GENESIS_VERSION " - build websites with a simple language.\n"
          "\n"
          "Usage:\n"
          "  genesis build [file-or-folder]   Compile lead.gns into dist/\n"
          "  genesis dev   [file-or-folder]   Build, serve on localhost, and rebuild on changes\n"
          "  genesis check [file-or-folder]   Report errors without writing any files\n"
          "  genesis fmt   [file-or-folder]   Format a .gns file in place\n"
          "  genesis init  [folder]           Create a new project (lead.gns and assets/)\n"
          "  genesis help  [command]          Show help for a command\n"
          "  genesis --version                Show the version\n"
          "\n"
          "Without a file, commands use lead.gns in the current folder.\n"
          "Run 'genesis help <command>' for the options of each command.\n",
          stream);
}

static int print_command_help(const char *command)
{
    if (strcmp(command, "build") == 0) {
        puts("Usage: genesis build [file-or-folder]\n\n"
             "Compiles a Genesis+ file into a website in the 'dist' folder next to it.\n"
             "Writes one .html file per page plus style.css, and copies the 'assets'\n"
             "folder if there is one. Nothing is written if there are errors.");
    } else if (strcmp(command, "dev") == 0) {
        puts("Usage: genesis dev [file-or-folder] [--port N]\n\n"
             "Builds the project, serves dist/ at http://localhost:3000/ (only reachable\n"
             "from this computer), and rebuilds whenever the .gns file or the assets\n"
             "folder changes. Open pages reload automatically. Press Ctrl+C to stop.\n\n"
             "Options:\n"
             "  --port N   Use port N instead of 3000 (1024-65535)");
    } else if (strcmp(command, "check") == 0) {
        puts("Usage: genesis check [file-or-folder] [--machine]\n"
             "       genesis check --stdin [--stdin-filename PATH] [--machine]\n\n"
             "Reports errors and warnings without writing any files.\n\n"
             "Options:\n"
             "  --stdin                Read the source from standard input\n"
             "  --stdin-filename PATH  Name to use in messages; images are looked up\n"
             "                         relative to this path's folder\n"
             "  --machine              One tab-separated line per problem, for editors:\n"
             "                         severity, line, column, length, message");
    } else if (strcmp(command, "fmt") == 0) {
        puts("Usage: genesis fmt [file-or-folder] [--check]\n"
             "       genesis fmt --stdin [--machine]\n\n"
             "Rewrites a .gns file in the standard style (4-space indentation, one\n"
             "element per line). Files with errors are left unchanged.\n\n"
             "Options:\n"
             "  --check    Do not write; exit with status 1 if the file needs formatting\n"
             "  --stdin    Read from standard input and write the result to standard output\n"
             "  --machine  With --stdin, report errors in the machine-readable format");
    } else if (strcmp(command, "init") == 0) {
        puts("Usage: genesis init [folder]\n\n"
             "Creates lead.gns and an assets folder in the given folder (default: the\n"
             "current folder). Existing files are never overwritten.");
    } else {
        fprintf(stderr, "Genesis+: unknown command '%s'.\n\n", command);
        print_help(stderr);
        return EXIT_USAGE;
    }
    return EXIT_OK;
}

static int usage_error(const char *command, const char *message, const char *detail)
{
    fprintf(stderr, "Genesis+: %s%s%s\nRun 'genesis help %s' for usage.\n", message,
            detail ? " " : "", detail ? detail : "", command);
    return EXIT_USAGE;
}

/* ------------------------------------------------------------------------ */
/* Argument parsing                                                         */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *path;
    int use_stdin;
    const char *stdin_filename;
    int machine;
    int check_only;
    int port;
    int help;
} Options;

/* Parses the options allowed by `allowed` (a space-separated list). */
static int parse_options(const char *command, int argc, char **argv, const char *allowed,
                         Options *options)
{
    memset(options, 0, sizeof(*options));
    options->port = SERVER_DEFAULT_PORT;

    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            options->help = 1;
            continue;
        }

        if (arg[0] == '-' && arg[1] != '\0') {
            /* Check the option is allowed for this command. */
            char needle[48];
            snprintf(needle, sizeof(needle), " %s ", arg);
            char haystack[160];
            snprintf(haystack, sizeof(haystack), " %s ", allowed);

            if (strlen(arg) > 40 || strstr(haystack, needle) == NULL) {
                return usage_error(command, "unknown option", arg);
            }

            if (strcmp(arg, "--stdin") == 0) {
                options->use_stdin = 1;
            } else if (strcmp(arg, "--machine") == 0) {
                options->machine = 1;
            } else if (strcmp(arg, "--check") == 0) {
                options->check_only = 1;
            } else if (strcmp(arg, "--stdin-filename") == 0) {
                if (i + 1 >= argc) {
                    return usage_error(command, "--stdin-filename needs a path.", NULL);
                }
                options->stdin_filename = argv[++i];
            } else if (strcmp(arg, "--port") == 0) {
                if (i + 1 >= argc) {
                    return usage_error(command, "--port needs a number.", NULL);
                }
                const char *value = argv[++i];
                char *end = NULL;
                long port = strtol(value, &end, 10);
                if (end == value || *end != '\0' || port < 1024 || port > 65535) {
                    return usage_error(command,
                                       "--port must be a whole number from 1024 to 65535, not",
                                       value);
                }
                options->port = (int)port;
            }
            continue;
        }

        if (options->path != NULL) {
            return usage_error(command, "too many arguments:", arg);
        }
        options->path = arg;
    }

    return -1; /* parsed successfully */
}

/* ------------------------------------------------------------------------ */
/* Commands                                                                 */
/* ------------------------------------------------------------------------ */

static int read_stdin(char **data, size_t *length)
{
    Buffer content;
    buffer_init(&content);
    char chunk[16384];
    size_t got;

    while ((got = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        if (content.length + got > PLATFORM_MAX_FILE_SIZE) {
            fprintf(stderr, "Genesis+ error: input is too large.\n");
            buffer_free(&content);
            return 0;
        }
        buffer_append(&content, chunk, got);
    }
    buffer_append(&content, "", 0);

    if (content.failed || ferror(stdin)) {
        fprintf(stderr, "Genesis+ error: could not read standard input.\n");
        buffer_free(&content);
        return 0;
    }

    *data = content.data;
    *length = content.length;
    return 1;
}

static int resolve_or_report(const char *path, Buffer *entry)
{
    Buffer error;
    buffer_init(&error);
    int ok = compiler_resolve_entry(path, entry, &error);
    if (!ok) {
        fprintf(stderr, "Genesis+ error: %s\n", buffer_cstr(&error));
    }
    buffer_free(&error);
    return ok;
}

static void print_diagnostics(const DiagList *diags, int machine, FILE *stream)
{
    Buffer text;
    buffer_init(&text);
    if (machine) {
        diag_format_machine(diags, &text);
    } else {
        diag_format(diags, &text);
    }
    fputs(buffer_cstr(&text), stream);
    buffer_free(&text);
}

static int command_build(int argc, char **argv)
{
    Options options;
    int status = parse_options("build", argc, argv, "", &options);
    if (status >= 0) {
        return status;
    }
    if (options.help) {
        return print_command_help("build");
    }

    Buffer entry;
    buffer_init(&entry);

    if (!resolve_or_report(options.path, &entry)) {
        buffer_free(&entry);
        return EXIT_FAILED;
    }

    Buffer log;
    buffer_init(&log);
    int ok = compiler_build(entry.data, &log, 0);
    fputs(buffer_cstr(&log), ok ? stdout : stderr);

    buffer_free(&log);
    buffer_free(&entry);
    return ok ? EXIT_OK : EXIT_FAILED;
}

static int command_check(int argc, char **argv)
{
    Options options;
    int status = parse_options("check", argc, argv, "--stdin --stdin-filename --machine",
                               &options);
    if (status >= 0) {
        return status;
    }
    if (options.help) {
        return print_command_help("check");
    }
    if (options.use_stdin && options.path != NULL) {
        return usage_error("check", "use either a file or --stdin, not both.", NULL);
    }

    Buffer entry;
    Buffer project_dir;
    buffer_init(&entry);
    buffer_init(&project_dir);
    char *source = NULL;
    size_t length = 0;
    const char *display = NULL;

    if (options.use_stdin) {
        platform_set_binary_stdio();
        if (!read_stdin(&source, &length)) {
            return EXIT_FAILED;
        }
        display = options.stdin_filename != NULL ? options.stdin_filename : "<stdin>";
        path_dirname(&project_dir, options.stdin_filename != NULL ? options.stdin_filename
                                                                  : "input.gns");
    } else {
        if (!resolve_or_report(options.path, &entry)) {
            buffer_free(&entry);
            return EXIT_FAILED;
        }
        Buffer error;
        buffer_init(&error);
        if (!platform_read_file(entry.data, &source, &length, &error)) {
            fprintf(stderr, "Genesis+ error: %s\n", buffer_cstr(&error));
            buffer_free(&error);
            buffer_free(&entry);
            return EXIT_FAILED;
        }
        buffer_free(&error);
        display = entry.data;
        path_dirname(&project_dir, entry.data);
    }

    Compilation *compilation = malloc(sizeof(Compilation));
    if (compilation == NULL) {
        fprintf(stderr, "Genesis+ error: out of memory.\n");
        free(source);
        buffer_free(&entry);
        buffer_free(&project_dir);
        return EXIT_FAILED;
    }

    int ok = compiler_compile(compilation, display, source, length, project_dir.data);

    print_diagnostics(&compilation->diags, options.machine,
                      options.machine ? stdout : stderr);

    if (!options.machine) {
        if (ok) {
            size_t warnings = compilation->diags.warning_count;
            if (warnings > 0) {
                printf("%s: no errors, %zu warning%s.\n", display, warnings,
                       warnings == 1 ? "" : "s");
            } else {
                printf("%s: no problems found.\n", display);
            }
        } else {
            size_t errors = compilation->diags.error_count;
            fprintf(stderr, "%s: %zu error%s.\n", display, errors, errors == 1 ? "" : "s");
        }
    }

    compilation_free(compilation);
    free(compilation);
    free(source);
    buffer_free(&entry);
    buffer_free(&project_dir);
    return ok ? EXIT_OK : EXIT_FAILED;
}

static int command_fmt(int argc, char **argv)
{
    Options options;
    int status = parse_options("fmt", argc, argv, "--check --stdin --machine", &options);
    if (status >= 0) {
        return status;
    }
    if (options.help) {
        return print_command_help("fmt");
    }
    if (options.use_stdin && (options.path != NULL || options.check_only)) {
        return usage_error("fmt", "--stdin cannot be combined with a file or --check.", NULL);
    }

    char *source = NULL;
    size_t length = 0;
    Buffer entry;
    buffer_init(&entry);

    if (options.use_stdin) {
        platform_set_binary_stdio();
        if (!read_stdin(&source, &length)) {
            return EXIT_FAILED;
        }
    } else {
        if (!resolve_or_report(options.path, &entry)) {
            buffer_free(&entry);
            return EXIT_FAILED;
        }
        Buffer error;
        buffer_init(&error);
        if (!platform_read_file(entry.data, &source, &length, &error)) {
            fprintf(stderr, "Genesis+ error: %s\n", buffer_cstr(&error));
            buffer_free(&error);
            buffer_free(&entry);
            return EXIT_FAILED;
        }
        buffer_free(&error);
    }

    const char *display = options.use_stdin ? "<stdin>" : entry.data;
    DiagList *diags = malloc(sizeof(DiagList));
    Buffer formatted;
    buffer_init(&formatted);
    int result = EXIT_OK;

    if (diags == NULL) {
        fprintf(stderr, "Genesis+ error: out of memory.\n");
        result = EXIT_FAILED;
    } else {
        diag_init(diags, display, source, length);

        if (!format_source(source, length, &formatted, diags)) {
            print_diagnostics(diags, options.machine, options.machine ? stdout : stderr);
            if (!options.machine) {
                fprintf(stderr, "%s was not formatted because it has errors.\n", display);
            }
            result = EXIT_FAILED;
        } else if (options.use_stdin) {
            fwrite(formatted.data, 1, formatted.length, stdout);
        } else {
            int changed = formatted.length != length ||
                          memcmp(formatted.data, source, length) != 0;

            if (options.check_only) {
                if (changed) {
                    printf("%s needs formatting. Run: genesis fmt %s\n", display, display);
                    result = EXIT_FAILED;
                } else {
                    printf("%s is already formatted.\n", display);
                }
            } else if (changed) {
                Buffer error;
                buffer_init(&error);
                if (platform_write_file_atomic(entry.data, formatted.data, formatted.length,
                                               &error)) {
                    printf("Formatted %s\n", display);
                } else {
                    fprintf(stderr, "Genesis+ error: %s\n", buffer_cstr(&error));
                    result = EXIT_FAILED;
                }
                buffer_free(&error);
            } else {
                printf("%s is already formatted.\n", display);
            }
        }
    }

    free(diags);
    buffer_free(&formatted);
    buffer_free(&entry);
    free(source);
    return result;
}

static int command_dev(int argc, char **argv)
{
    Options options;
    int status = parse_options("dev", argc, argv, "--port", &options);
    if (status >= 0) {
        return status;
    }
    if (options.help) {
        return print_command_help("dev");
    }

    Buffer entry;
    buffer_init(&entry);

    if (!resolve_or_report(options.path, &entry)) {
        buffer_free(&entry);
        return EXIT_FAILED;
    }

    int result = server_run(entry.data, options.port);
    buffer_free(&entry);
    return result;
}

static const char STARTER_SOURCE[] =
    "# Welcome to Genesis+!\n"
    "# Build this site with:   genesis build\n"
    "# Preview it live with:   genesis dev\n"
    "\n"
    "page Home title = \"My Genesis+ site\" {\n"
    "    column gap = 12 padding = 32 background = \"#f5f3ff\" {\n"
    "        heading \"Hello, world!\" color = \"#5b21b6\"\n"
    "        text \"This website was built with Genesis+.\"\n"
    "        button \"Learn more\" to = \"https://example.com\"\n"
    "    }\n"
    "\n"
    "    row gap = 16 {\n"
    "        column {\n"
    "            heading \"Simple\" level = 2\n"
    "            text \"Blocks use braces, so indentation never breaks your page.\"\n"
    "        }\n"
    "        column {\n"
    "            heading \"Fast\" level = 2\n"
    "            text \"Edit lead.gns and the preview updates by itself.\"\n"
    "        }\n"
    "    }\n"
    "}\n";

static int write_new_file(const char *path, const char *content, int *created)
{
    *created = 0;

    if (platform_is_file(path) || platform_is_directory(path)) {
        return 1;
    }

    Buffer error;
    buffer_init(&error);
    int ok = platform_write_file_atomic(path, content, strlen(content), &error);
    if (!ok) {
        fprintf(stderr, "Genesis+ error: %s\n", buffer_cstr(&error));
    }
    buffer_free(&error);
    *created = ok;
    return ok;
}

static int command_init(int argc, char **argv)
{
    Options options;
    int status = parse_options("init", argc, argv, "", &options);
    if (status >= 0) {
        return status;
    }
    if (options.help) {
        return print_command_help("init");
    }

    const char *folder = options.path != NULL ? options.path : ".";

    if (platform_is_file(folder)) {
        fprintf(stderr, "Genesis+ error: '%s' is a file, not a folder.\n", folder);
        return EXIT_FAILED;
    }

    if (!platform_make_directory(folder)) {
        fprintf(stderr, "Genesis+ error: could not create the folder '%s'.\n", folder);
        return EXIT_FAILED;
    }

    Buffer lead;
    Buffer assets;
    Buffer gitignore;
    buffer_init(&lead);
    buffer_init(&assets);
    buffer_init(&gitignore);
    path_join(&lead, folder, GENESIS_DEFAULT_ENTRY);
    path_join(&assets, folder, "assets");
    path_join(&gitignore, folder, ".gitignore");

    int ok = 1;
    int created_lead = 0;
    int created_ignore = 0;
    int had_assets = platform_is_directory(buffer_cstr(&assets));

    if (platform_is_file(buffer_cstr(&lead))) {
        printf("%s already exists, so it was left unchanged.\n", buffer_cstr(&lead));
    } else {
        ok = write_new_file(buffer_cstr(&lead), STARTER_SOURCE, &created_lead);
    }

    if (ok && !platform_make_directory(buffer_cstr(&assets))) {
        fprintf(stderr, "Genesis+ error: could not create '%s'.\n", buffer_cstr(&assets));
        ok = 0;
    }

    if (ok) {
        ok = write_new_file(buffer_cstr(&gitignore), "# Generated website\ndist/\n",
                            &created_ignore);
    }

    if (ok) {
        if (created_lead) {
            printf("Created %s\n", buffer_cstr(&lead));
        }
        if (!had_assets) {
            printf("Created %s/ (put your images here)\n", buffer_cstr(&assets));
        }
        if (created_ignore) {
            printf("Created %s\n", buffer_cstr(&gitignore));
        }
        printf("\nNext steps:\n");
        if (options.path != NULL) {
            printf("  cd %s\n", folder);
        }
        printf("  genesis dev      # preview at http://localhost:%d/\n", SERVER_DEFAULT_PORT);
        printf("  genesis build    # write the website to dist/\n");
    }

    buffer_free(&lead);
    buffer_free(&assets);
    buffer_free(&gitignore);
    return ok ? EXIT_OK : EXIT_FAILED;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs("Genesis+: no command given. To build lead.gns, run: genesis build\n\n", stderr);
        print_help(stderr);
        return EXIT_USAGE;
    }

    const char *command = argv[1];
    int rest_count = argc - 2;
    char **rest = argv + 2;

    if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0 ||
        strcmp(command, "version") == 0) {
        puts("Genesis+ " GENESIS_VERSION);
        return EXIT_OK;
    }

    if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0 ||
        strcmp(command, "help") == 0) {
        if (rest_count >= 1) {
            return print_command_help(rest[0]);
        }
        print_help(stdout);
        return EXIT_OK;
    }

    if (strcmp(command, "build") == 0) {
        return command_build(rest_count, rest);
    }
    if (strcmp(command, "check") == 0) {
        return command_check(rest_count, rest);
    }
    if (strcmp(command, "dev") == 0) {
        return command_dev(rest_count, rest);
    }
    if (strcmp(command, "fmt") == 0) {
        return command_fmt(rest_count, rest);
    }
    if (strcmp(command, "init") == 0) {
        return command_init(rest_count, rest);
    }

    if (strlen(command) > 4 && strcmp(command + strlen(command) - 4, ".gns") == 0) {
        fprintf(stderr, "Genesis+: to build a file, run: genesis build %s\n", command);
        return EXIT_USAGE;
    }

    fprintf(stderr, "Genesis+: unknown command '%s'.\n\n", command);
    print_help(stderr);
    return EXIT_USAGE;
}
