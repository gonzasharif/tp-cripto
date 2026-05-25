#ifndef UTILS_H
#define UTILS_H

/**
 * Check whether a filename ends in ".bmp" (case-insensitive).
 *
 * The comparison looks at the last 4 characters of the string and
 * lowercases them before comparing against ".bmp", so "IMG.BMP",
 * "img.Bmp" and "img.bmp" are all considered valid.
 *
 * @param filename  Null-terminated string with the file name or path
 *                  to inspect. Must not be NULL.
 *
 * @return 1 if the filename ends in ".bmp" (any case),
 *         0 otherwise (including when the string is shorter than 4
 *         characters).
 */
int has_bmp_extension(const char *filename);

/**
 * Count the .bmp files inside a directory, optionally listing them.
 *
 * Iterates over every entry in `dir` and counts those whose name ends
 * in ".bmp" (case-insensitive, via has_bmp_extension) and that do not
 * match `exclude`. Subdirectories are not traversed recursively.
 *
 * @param dir         Path to the directory to scan. Must not be NULL.
 * @param exclude     File name to skip during the scan (typically the
 *                    secret image itself so it is not counted as a
 *                    carrier). Compared with strcmp against each
 *                    entry's d_name, so it should be a base name, not
 *                    a full path. Must not be NULL; pass "" to exclude
 *                    nothing.
 * @param print_list  If non-zero, every matched file is printed to
 *                    stdout as "  [i] <name>" while scanning.
 *                    If zero, the function is silent.
 *
 * @return The number of matching .bmp files on success,
 *         or -1 if the directory could not be opened.
 */
int count_bmp_files(const char *dir, const char *exclude, int print_list);

/**
 * List the .bmp files inside a directory, returning their full paths
 * sorted alphabetically.
 *
 * The order is deterministic (qsort + strcmp) so that distribute() and
 * recovery() agree on the mapping from carrier file to shadow index
 * (carrier 0 → x = 1, carrier 1 → x = 2, etc.). Entries whose name
 * matches `exclude` (compared with strcmp against d_name) are skipped,
 * which is used to leave the secret out of the carrier list.
 *
 * On success the function allocates an array of `count` C strings via
 * malloc. Each string is "<dir>/<filename>". The caller owns the
 * memory and must free every string and then the array itself.
 *
 * @param dir         Directory to scan. Must not be NULL.
 * @param exclude     Base filename to skip during the scan (typically
 *                    the secret image). Pass "" to exclude nothing.
 * @param out_paths   Output: receives the malloc'd array of paths.
 *                    Set to NULL on failure.
 *
 * @return Number of paths returned on success (0 if no matches),
 *         or -1 if the directory could not be opened or allocation
 *         failed.
 */
int list_bmp_files(const char *dir, const char *exclude, char ***out_paths);

/**
 * Check whether a path refers to an openable directory.
 *
 * Implemented by attempting opendir() on the path; this means the
 * function returns 0 both when the path does not exist and when it
 * exists but cannot be opened (e.g. permission denied, or not a
 * directory).
 *
 * @param path  Null-terminated path to test. Must not be NULL.
 *
 * @return 1 if the directory exists and can be opened,
 *         0 otherwise.
 */
int directory_exists(const char *path);

#endif /* UTILS_H */
