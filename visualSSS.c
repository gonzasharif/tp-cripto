#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* ─── Constants ─────────────────────────────────────────────── */
#define K_MIN      2
#define K_MAX      10
#define N_MIN      2
#define BMP_EXT    ".bmp"

/* ─── Usage ──────────────────────────────────────────────────── */
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  Distribute: %s -d -secret <image.bmp> -k <number> [-n <number>] [-dir <directory>]\n"
        "  Recover:    %s -r -secret <image.bmp> -k <number> [-n <number>] [-dir <directory>]\n"
        "\nParameters:\n"
        "  -d              Distribute secret image into shadow images.\n"
        "  -r              Recover secret image from shadow images.\n"
        "  -secret <img>   BMP file to hide (-d) or output file (-r). Must end in .bmp\n"
        "  -k <number>     Minimum shadows needed to recover (%d <= k <= %d).\n"
        "  -n <number>     Total shadows (only with -d, n >= k, n >= %d).\n"
        "                  If omitted, n = number of BMP images found in directory.\n"
        "  -dir <dir>      Directory containing carrier images (default: current directory).\n",
        prog, prog, K_MIN, K_MAX, N_MIN);
}

/* ─── Helper: check .bmp extension (case-insensitive) ────────── */
static int has_bmp_extension(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4) return 0;
    const char *ext = filename + len - 4;
    /* Compare ignoring case */
    char lower[5];
    for (int i = 0; i < 4; i++)
        lower[i] = (char)(ext[i] >= 'A' && ext[i] <= 'Z' ? ext[i] + 32 : ext[i]);
    lower[4] = '\0';
    return strcmp(lower, BMP_EXT) == 0;
}

/* ─── Helper: count .bmp files in a directory ────────────────── */
static int count_bmp_files(const char *dir, const char *exclude, int print_list) {
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

/* ─── Helper: check that a directory exists ──────────────────── */
static int directory_exists(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    closedir(d);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {

    /* ── Raw parsed values ──────────────────────────────────── */
    int    mode       = 0;          /* 'd' or 'r' */
    char  *secret     = NULL;
    int    k          = -1;
    int    n          = -1;         /* -1 means "not provided" */
    char  *dir        = ".";        /* default: current directory */

    int    secret_set = 0;
    int    k_set      = 0;
    int    n_set      = 0;

    /* ── Argument parsing ───────────────────────────────────── */
    if (argc < 2) {
        fprintf(stderr, "Error: no arguments provided.\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-d") == 0) {
            if (mode != 0) {
                fprintf(stderr, "Error: only one of -d or -r may be specified.\n\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            mode = 'd';

        } else if (strcmp(argv[i], "-r") == 0) {
            if (mode != 0) {
                fprintf(stderr, "Error: only one of -d or -r may be specified.\n\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            mode = 'r';

        } else if (strcmp(argv[i], "-secret") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -secret requires an argument.\n\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            secret = argv[++i];
            secret_set = 1;

        } else if (strcmp(argv[i], "-k") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -k requires an argument.\n\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            char *end;
            k = (int)strtol(argv[++i], &end, 10);
            if (*end != '\0') {
                fprintf(stderr, "Error: -k value must be an integer (got '%s').\n\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            k_set = 1;

        } else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -n requires an argument.\n\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            char *end;
            n = (int)strtol(argv[++i], &end, 10);
            if (*end != '\0') {
                fprintf(stderr, "Error: -n value must be an integer (got '%s').\n\n", argv[i]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            n_set = 1;

        } else if (strcmp(argv[i], "-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -dir requires an argument.\n\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            dir = argv[++i];

        } else {
            fprintf(stderr, "Error: unknown parameter '%s'.\n\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* ── Validate mandatory parameters are present ──────────── */
    if (mode == 0) {
        fprintf(stderr, "Error: must specify either -d (distribute) or -r (recover).\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!secret_set) {
        fprintf(stderr, "Error: -secret is required.\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!k_set) {
        fprintf(stderr, "Error: -k is required.\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── -n is only allowed with -d ─────────────────────────── */
    if (n_set && mode == 'r') {
        fprintf(stderr, "Error: -n can only be used with -d (distribute).\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── Validate secret filename ends in .bmp ──────────────── */
    if (!has_bmp_extension(secret)) {
        fprintf(stderr, "Error: secret image '%s' must have a .bmp extension.\n\n", secret);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── On distribute, secret file must exist ──────────────── */
    if (mode == 'd') {
        printf("secret: '%s'.\n", secret);
        FILE *f = fopen(secret, "rb");
        if (!f) {
            fprintf(stderr, "Error: secret image '%s' not found or cannot be opened.\n", secret);
            return EXIT_FAILURE;
        }
        fclose(f);
    }

    /* ── Validate k range ───────────────────────────────────── */
    if (k < K_MIN || k > K_MAX) {
        fprintf(stderr, "Error: k must be between %d and %d (got %d).\n\n", K_MIN, K_MAX, k);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* ── Validate directory ─────────────────────────────────── */
    if (!directory_exists(dir)) {
        fprintf(stderr, "Error: directory '%s' does not exist or cannot be opened.\n", dir);
        return EXIT_FAILURE;
    }

 /* ── Resolve n if not provided (only relevant for -d) ────── */
    if (mode == 'd' && !n_set) {
        /* Extract just the filename from the secret path for exclusion */
        const char *secret_basename = strrchr(secret, '/');
        secret_basename = secret_basename ? secret_basename + 1 : secret;
 
        const char *dir_label = (dir[0] == '.' && dir[1] == '\0') ? "current directory" : dir;
        printf("Info: -n not provided. Scanning %s for BMP carrier images:\n", dir_label);
 
        int found = count_bmp_files(dir, secret_basename, 1);
        if (found < 0) {
            fprintf(stderr, "Error: could not scan directory '%s'.\n", dir);
            return EXIT_FAILURE;
        }
        n = found;
        printf("Info: Found %d BMP carrier image(s).\n", n);
    }


    /* ── Validate n ─────────────────────────────────────────── */
    if (mode == 'd') {
        if (n < N_MIN) {
            fprintf(stderr,
                "Error: n must be at least %d (got %d).\n"
                "       Make sure there are enough BMP images in '%s'.\n",
                N_MIN, n, dir);
            return EXIT_FAILURE;
        }
        if (n < k) {
            fprintf(stderr, "Error: n (%d) must be >= k (%d).\n", n, k);
            return EXIT_FAILURE;
        }
    }

    /* ── Summary ────────────────────────────────────────────── */
    printf("Mode    : %s\n", mode == 'd' ? "distribute" : "recover");
    printf("Secret  : %s\n", secret);
    printf("k       : %d\n", k);
    if (mode == 'd')
        printf("n       : %d\n", n);
    printf("Dir     : %s\n", dir);
    printf("\n");

    /* ── TODO: call distribute() or recover() here ──────────── */

    printf("Done :)\n");
    return EXIT_SUCCESS;
}
