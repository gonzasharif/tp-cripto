#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "utils.h"

#define BMP_EXT ".bmp"

int has_bmp_extension(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4) return 0;
    const char *ext = filename + len - 4;
    char lower[5];
    for (int i = 0; i < 4; i++)
        lower[i] = (char)(ext[i] >= 'A' && ext[i] <= 'Z' ? ext[i] + 32 : ext[i]);
    lower[4] = '\0';
    return strcmp(lower, BMP_EXT) == 0;
}

int count_bmp_files(const char *dir, const char *exclude, int print_list) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (has_bmp_extension(entry->d_name) &&
            strcmp(entry->d_name, exclude) != 0) {
            if (print_list)
                printf("  [%d] %s\n", count + 1, entry->d_name);
            count++;
        }
    }
    closedir(d);
    return count;
}

/* qsort comparator: lexicographic order of C strings via strcmp. */
static int cmp_strings(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

int list_bmp_files(const char *dir, const char *exclude, char ***out_paths) {
    *out_paths = NULL;

    DIR *d = opendir(dir);
    if (!d) return -1;

    /* Grow a dynamic array of "<dir>/<name>" strings as we scan. */
    size_t capacity = 16;
    size_t count    = 0;
    char **paths    = malloc(capacity * sizeof(char *));
    if (!paths) { closedir(d); return -1; }

    size_t dir_len = strlen(dir);
    int need_slash = (dir_len > 0 && dir[dir_len - 1] != '/');

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (!has_bmp_extension(entry->d_name))             continue;
        if (strcmp(entry->d_name, exclude) == 0)           continue;

        if (count == capacity) {
            capacity *= 2;
            char **resized = realloc(paths, capacity * sizeof(char *));
            if (!resized) goto fail;
            paths = resized;
        }

        size_t name_len = strlen(entry->d_name);
        size_t path_len = dir_len + (need_slash ? 1 : 0) + name_len + 1;
        char *full = malloc(path_len);
        if (!full) goto fail;

        if (need_slash) snprintf(full, path_len, "%s/%s", dir, entry->d_name);
        else            snprintf(full, path_len, "%s%s",  dir, entry->d_name);

        paths[count++] = full;
    }
    closedir(d);

    /* Sort for deterministic carrier ordering. */
    qsort(paths, count, sizeof(char *), cmp_strings);

    *out_paths = paths;
    return (int)count;

fail:
    closedir(d);
    for (size_t i = 0; i < count; i++) free(paths[i]);
    free(paths);
    return -1;
}

int directory_exists(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    closedir(d);
    return 1;
}
