#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

int platform_is_directory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

int platform_is_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

int platform_make_directory(const char *path)
{
#if defined(_WIN32)
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0777);
#endif

    if (result == 0) {
        return 1;
    }

    return errno == EEXIST && platform_is_directory(path);
}

int platform_read_file(const char *path, char **data, size_t *length, Buffer *error)
{
    *data = NULL;
    *length = 0;

    if (platform_is_directory(path)) {
        buffer_appendf(error, "'%s' is a folder, not a file.", path);
        return 0;
    }

    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        buffer_appendf(error, "Could not open '%s': %s.", path, strerror(errno));
        return 0;
    }

    Buffer content;
    buffer_init(&content);
    char chunk[16384];
    size_t got;

    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        if (content.length + got > PLATFORM_MAX_FILE_SIZE) {
            buffer_appendf(error, "'%s' is too large (the limit is %u MB).", path,
                           PLATFORM_MAX_FILE_SIZE / (1024u * 1024u));
            buffer_free(&content);
            fclose(file);
            return 0;
        }
        buffer_append(&content, chunk, got);
    }

    int read_failed = ferror(file);
    fclose(file);

    if (read_failed) {
        buffer_appendf(error, "Could not read '%s'.", path);
        buffer_free(&content);
        return 0;
    }

    /* Ensure an empty file still yields a valid string. */
    buffer_append(&content, "", 0);

    if (content.failed) {
        buffer_appendf(error, "Out of memory while reading '%s'.", path);
        buffer_free(&content);
        return 0;
    }

    *data = content.data;
    *length = content.length;
    return 1;
}

static int replace_file(const char *from, const char *to)
{
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

int platform_write_file_atomic(const char *path, const char *data, size_t length,
                               Buffer *error)
{
    if (platform_is_directory(path)) {
        buffer_appendf(error, "Cannot write '%s' because a folder with that name exists.",
                       path);
        return 0;
    }

    Buffer temp;
    buffer_init(&temp);
    buffer_appendf(&temp, "%s.genesis-tmp", path);

    if (temp.failed) {
        buffer_append_str(error, "Out of memory.");
        buffer_free(&temp);
        return 0;
    }

    FILE *file = fopen(temp.data, "wb");

    if (file == NULL) {
        buffer_appendf(error, "Could not create '%s': %s.", temp.data, strerror(errno));
        buffer_free(&temp);
        return 0;
    }

    int ok = length == 0 || fwrite(data, 1, length, file) == length;
    ok = fflush(file) == 0 && ok;
    ok = fclose(file) == 0 && ok;

    if (!ok) {
        buffer_appendf(error, "Could not write '%s' (is the disk full?).", path);
        remove(temp.data);
        buffer_free(&temp);
        return 0;
    }

    if (!replace_file(temp.data, path)) {
        buffer_appendf(error, "Could not replace '%s'. Is it open in another program?",
                       path);
        remove(temp.data);
        buffer_free(&temp);
        return 0;
    }

    buffer_free(&temp);
    return 1;
}

static int copy_file(const char *from, const char *to, Buffer *error)
{
    char *data = NULL;
    size_t length = 0;

    if (!platform_read_file(from, &data, &length, error)) {
        return 0;
    }

    int ok = platform_write_file_atomic(to, data, length, error);
    free(data);
    return ok;
}

static int copy_tree_depth(const char *from, const char *to, Buffer *error, int depth)
{
    if (depth > 32) {
        buffer_appendf(error, "Folders inside '%s' are nested too deeply.", from);
        return 0;
    }

    if (!platform_make_directory(to)) {
        buffer_appendf(error, "Could not create the folder '%s'.", to);
        return 0;
    }

    DIR *dir = opendir(from);

    if (dir == NULL) {
        buffer_appendf(error, "Could not open the folder '%s'.", from);
        return 0;
    }

    /* Collect names first so the copy order is deterministic. */
    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int ok = 1;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            char **grown = realloc(names, new_capacity * sizeof(char *));
            if (grown == NULL) {
                ok = 0;
                break;
            }
            names = grown;
            capacity = new_capacity;
        }

        size_t len = strlen(entry->d_name);
        names[count] = malloc(len + 1);
        if (names[count] == NULL) {
            ok = 0;
            break;
        }
        memcpy(names[count], entry->d_name, len + 1);
        count++;
    }

    closedir(dir);

    if (!ok) {
        buffer_append_str(error, "Out of memory while copying assets.");
    }

    for (size_t i = 0; ok && i < count; i++) {
        /* Skip our own temporary files. */
        size_t len = strlen(names[i]);
        if (len > 12 && strcmp(names[i] + len - 12, ".genesis-tmp") == 0) {
            continue;
        }

        Buffer source_path;
        Buffer target_path;
        buffer_init(&source_path);
        buffer_init(&target_path);
        path_join(&source_path, from, names[i]);
        path_join(&target_path, to, names[i]);

        if (source_path.failed || target_path.failed) {
            buffer_append_str(error, "Out of memory while copying assets.");
            ok = 0;
        } else if (platform_is_directory(source_path.data)) {
            ok = copy_tree_depth(source_path.data, target_path.data, error, depth + 1);
        } else if (platform_is_file(source_path.data)) {
            ok = copy_file(source_path.data, target_path.data, error);
        }

        buffer_free(&source_path);
        buffer_free(&target_path);
    }

    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);

    return ok;
}

int platform_copy_tree(const char *from, const char *to, Buffer *error)
{
    return copy_tree_depth(from, to, error, 0);
}

static unsigned long long mix(unsigned long long hash, unsigned long long value)
{
    /* FNV-1a over the 8 bytes of value. */
    for (int i = 0; i < 8; i++) {
        hash ^= (value >> (i * 8)) & 0xFFu;
        hash *= 1099511628211ull;
    }
    return hash;
}

int platform_file_stamp(const char *path, unsigned long long *stamp)
{
    struct stat info;

    if (stat(path, &info) != 0) {
        *stamp = 0;
        return 0;
    }

    unsigned long long hash = 1469598103934665603ull;
    hash = mix(hash, (unsigned long long)info.st_mtime);
    hash = mix(hash, (unsigned long long)info.st_size);
    *stamp = hash;
    return 1;
}

static void tree_stamp_depth(const char *path, unsigned long long *hash, int depth)
{
    if (depth > 32) {
        return;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        Buffer child;
        buffer_init(&child);
        path_join(&child, path, entry->d_name);

        if (!child.failed) {
            /* Order-independent combination, since readdir order varies. */
            unsigned long long name_hash = 1469598103934665603ull;
            for (const char *p = entry->d_name; *p != '\0'; p++) {
                name_hash = mix(name_hash, (unsigned char)*p);
            }

            if (platform_is_directory(child.data)) {
                unsigned long long sub = name_hash;
                tree_stamp_depth(child.data, &sub, depth + 1);
                *hash += sub;
            } else {
                unsigned long long file = 0;
                platform_file_stamp(child.data, &file);
                *hash += mix(name_hash, file);
            }
        }

        buffer_free(&child);
    }

    closedir(dir);
}

void platform_tree_stamp(const char *path, unsigned long long *stamp)
{
    unsigned long long hash = 0;
    tree_stamp_depth(path, &hash, 0);
    *stamp = hash;
}

void platform_sleep_ms(unsigned milliseconds)
{
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    nanosleep(&delay, NULL);
#endif
}

void platform_set_binary_stdio(void)
{
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

void path_join(Buffer *out, const char *a, const char *b)
{
    size_t len = strlen(a);
    buffer_append_str(out, a);

    if (len > 0 && a[len - 1] != '/' && a[len - 1] != '\\') {
        buffer_append_char(out, '/');
    }

    buffer_append_str(out, b);
}

void path_dirname(Buffer *out, const char *path)
{
    const char *last = NULL;

    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            last = p;
        }
    }

    if (last == NULL) {
        buffer_append_char(out, '.');
    } else if (last == path) {
        buffer_append_char(out, '/');
    } else {
        buffer_append(out, path, (size_t)(last - path));
    }
}
