#include <stdio.h>
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

int directory_exists(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    closedir(d);
    return 1;
}
