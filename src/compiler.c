#include "compiler.h"

#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "parser.h"
#include "platform.h"

int compiler_resolve_entry(const char *argument, Buffer *entry, Buffer *error)
{
    if (argument == NULL || argument[0] == '\0') {
        if (!platform_is_file(GENESIS_DEFAULT_ENTRY)) {
            buffer_append_str(error,
                              "No " GENESIS_DEFAULT_ENTRY " found in the current folder.\n"
                              "  Run 'genesis init' to create a new project here, or pass a "
                              "file: genesis build site.gns");
            return 0;
        }
        buffer_append_str(entry, GENESIS_DEFAULT_ENTRY);
        return !entry->failed;
    }

    if (platform_is_directory(argument)) {
        Buffer candidate;
        buffer_init(&candidate);
        path_join(&candidate, argument, GENESIS_DEFAULT_ENTRY);

        int found = !candidate.failed && platform_is_file(candidate.data);

        if (found) {
            buffer_append_str(entry, candidate.data);
        } else {
            buffer_appendf(error,
                           "No " GENESIS_DEFAULT_ENTRY " found in the folder '%s'.\n"
                           "  Run 'genesis init %s' to create a project there.",
                           argument, argument);
        }

        buffer_free(&candidate);
        return found && !entry->failed;
    }

    if (!platform_is_file(argument)) {
        buffer_appendf(error, "The file '%s' does not exist.", argument);
        return 0;
    }

    size_t length = strlen(argument);
    if (length < 4 || strcmp(argument + length - 4, ".gns") != 0) {
        buffer_appendf(error, "'%s' is not a Genesis+ file. Source files end in .gns.",
                       argument);
        return 0;
    }

    buffer_append_str(entry, argument);
    return !entry->failed;
}

static int is_remote(const char *src)
{
    return strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0 ||
           strncmp(src, "HTTP://", 7) == 0 || strncmp(src, "HTTPS://", 8) == 0;
}

/* Checks that every local image exists inside the project folder. */
static void check_images(Compilation *compilation, const AstNode *node,
                         const char *project_dir)
{
    for (; node != NULL; node = node->next_sibling) {
        if (node->type == AST_IMAGE && node->value != NULL && !is_remote(node->value)) {
            Buffer path;
            buffer_init(&path);
            path_join(&path, project_dir, node->value);

            if (!path.failed && !platform_is_file(path.data)) {
                diag_report(&compilation->diags, DIAG_ERROR, node->line, node->column, 5,
                            "Check the spelling (including upper/lower case) and that the "
                            "file is inside the assets folder next to your .gns file.",
                            "Image file '%s' was not found.", node->value);
            }

            buffer_free(&path);
        }

        check_images(compilation, node->first_child, project_dir);
    }
}

int compiler_compile(Compilation *out, const char *display_name, const char *source,
                     size_t length, const char *project_dir)
{
    memset(out, 0, sizeof(*out));
    diag_init(&out->diags, display_name, source, length);

    out->program = parse_program(source, length, &out->diags);

    if (out->program == NULL) {
        return 0;
    }

    if (out->diags.error_count == 0 && project_dir != NULL) {
        check_images(out, out->program->first_child, project_dir);
    }

    if (out->diags.error_count > 0) {
        return 0;
    }

    size_t pages = (size_t)ast_child_count(out->program);
    out->files = calloc(pages + 1, sizeof(OutputFile));

    if (out->files == NULL) {
        diag_report(&out->diags, DIAG_ERROR, 1, 1, 1, NULL, "Out of memory.");
        return 0;
    }

    for (const AstNode *page = out->program->first_child; page != NULL;
         page = page->next_sibling) {
        OutputFile *file = &out->files[out->file_count++];
        buffer_init(&file->content);
        size_t name_length = strlen(page->file_name);
        file->name = malloc(name_length + 1);
        if (file->name != NULL) {
            memcpy(file->name, page->file_name, name_length + 1);
        }
        codegen_page_html(page, &file->content);
    }

    OutputFile *css = &out->files[out->file_count++];
    buffer_init(&css->content);
    css->name = malloc(sizeof("style.css"));
    if (css->name != NULL) {
        memcpy(css->name, "style.css", sizeof("style.css"));
    }
    codegen_css(&css->content);

    for (size_t i = 0; i < out->file_count; i++) {
        if (out->files[i].name == NULL || out->files[i].content.failed) {
            diag_report(&out->diags, DIAG_ERROR, 1, 1, 1, NULL,
                        "Out of memory while generating output.");
            return 0;
        }
    }

    return 1;
}

void compilation_free(Compilation *compilation)
{
    ast_free(compilation->program);

    for (size_t i = 0; i < compilation->file_count; i++) {
        free(compilation->files[i].name);
        buffer_free(&compilation->files[i].content);
    }

    free(compilation->files);
    compilation->program = NULL;
    compilation->files = NULL;
    compilation->file_count = 0;
}

int compiler_write_output(const Compilation *compilation, const char *project_dir,
                          Buffer *error)
{
    Buffer dist;
    buffer_init(&dist);
    path_join(&dist, project_dir, "dist");

    if (dist.failed) {
        buffer_append_str(error, "Out of memory.");
        buffer_free(&dist);
        return 0;
    }

    if (platform_is_file(dist.data)) {
        buffer_appendf(error,
                       "'%s' is a file, but Genesis+ needs a folder there for the website. "
                       "Rename or move that file.",
                       dist.data);
        buffer_free(&dist);
        return 0;
    }

    if (!platform_make_directory(dist.data)) {
        buffer_appendf(error, "Could not create the output folder '%s'.", dist.data);
        buffer_free(&dist);
        return 0;
    }

    int ok = 1;

    for (size_t i = 0; ok && i < compilation->file_count; i++) {
        const OutputFile *file = &compilation->files[i];
        Buffer path;
        buffer_init(&path);
        path_join(&path, dist.data, file->name);

        ok = !path.failed && platform_write_file_atomic(path.data, file->content.data,
                                                        file->content.length, error);
        buffer_free(&path);
    }

    if (ok) {
        Buffer assets;
        Buffer target;
        buffer_init(&assets);
        buffer_init(&target);
        path_join(&assets, project_dir, "assets");
        path_join(&target, dist.data, "assets");

        if (assets.failed || target.failed) {
            buffer_append_str(error, "Out of memory.");
            ok = 0;
        } else if (platform_is_directory(assets.data)) {
            ok = platform_copy_tree(assets.data, target.data, error);
        }

        buffer_free(&assets);
        buffer_free(&target);
    }

    buffer_free(&dist);
    return ok;
}

int compiler_build(const char *entry_path, Buffer *log, int quiet)
{
    char *source = NULL;
    size_t length = 0;
    Buffer error;
    buffer_init(&error);

    if (!platform_read_file(entry_path, &source, &length, &error)) {
        buffer_appendf(log, "Genesis+ error: %s\n", buffer_cstr(&error));
        buffer_free(&error);
        return 0;
    }

    Buffer project_dir;
    buffer_init(&project_dir);
    path_dirname(&project_dir, entry_path);

    Compilation *compilation = malloc(sizeof(Compilation));

    if (compilation == NULL || project_dir.failed) {
        buffer_append_str(log, "Genesis+ error: out of memory.\n");
        free(compilation);
        free(source);
        buffer_free(&project_dir);
        buffer_free(&error);
        return 0;
    }

    int ok = compiler_compile(compilation, entry_path, source, length, project_dir.data);

    diag_format(&compilation->diags, log);

    if (!ok) {
        size_t errors = compilation->diags.error_count;
        buffer_appendf(log, "Build failed: %zu error%s. No files were written.\n", errors,
                       errors == 1 ? "" : "s");
    } else if (!compiler_write_output(compilation, project_dir.data, &error)) {
        buffer_appendf(log, "Genesis+ error: %s\nBuild failed while writing output.\n",
                       buffer_cstr(&error));
        ok = 0;
    } else if (!quiet) {
        Buffer dist;
        buffer_init(&dist);
        path_join(&dist, project_dir.data, "dist");

        buffer_appendf(log, "Built %s -> %s/\n", entry_path, buffer_cstr(&dist));
        for (size_t i = 0; i < compilation->file_count; i++) {
            buffer_appendf(log, "  %s\n", compilation->files[i].name);
        }

        Buffer assets;
        buffer_init(&assets);
        path_join(&assets, project_dir.data, "assets");
        if (platform_is_directory(buffer_cstr(&assets))) {
            buffer_append_str(log, "  assets/ (copied)\n");
        }
        buffer_free(&assets);

        if (compilation->diags.warning_count > 0) {
            buffer_appendf(log, "Finished with %zu warning%s.\n",
                           compilation->diags.warning_count,
                           compilation->diags.warning_count == 1 ? "" : "s");
        }
        buffer_free(&dist);
    }

    compilation_free(compilation);
    free(compilation);
    free(source);
    buffer_free(&project_dir);
    buffer_free(&error);
    return ok;
}
