#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "shamir/shamir.h"

#include "utils/utils.h"

/* ─── Constants ─────────────────────────────────────────────── */
#define K_MIN      2
#define K_MAX      10
#define N_MIN      2

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

static void exit_failure(char **argv, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    print_usage(argv[0]);
    exit(SSS_USAGE);
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
        exit_failure(argv, "Error: no arguments provided.\n\n");
    }

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-d") == 0) {
            if (mode != 0) {
                exit_failure(argv, "Error: only one of -d or -r may be specified.\n\n");
            }
            mode = 'd';

        } else if (strcmp(argv[i], "-r") == 0) {
            if (mode != 0) {
                exit_failure(argv, "Error: only one of -d or -r may be specified.\n\n");
            }
            mode = 'r';

        } else if (strcmp(argv[i], "-secret") == 0) {
            if (i + 1 >= argc) {
                exit_failure(argv, "Error: -secret requires an argument.\n\n");
            }
            secret = argv[++i];
            secret_set = 1;

        } else if (strcmp(argv[i], "-k") == 0) {
            if (i + 1 >= argc) {
                exit_failure(argv, "Error: -k requires an argument.\n\n");
            }
            char *end;
            k = (int)strtol(argv[++i], &end, 10);
            if (*end != '\0') {
                exit_failure(argv, "Error: -k value must be an integer (got '%s').\n\n", argv[i]);
            }
            k_set = 1;

        } else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                exit_failure(argv, "Error: -n requires an argument.\n\n");
            }
            char *end;
            n = (int)strtol(argv[++i], &end, 10);
            if (*end != '\0') {
                exit_failure(argv, "Error: -n value must be an integer (got '%s').\n\n", argv[i]);
            }
            n_set = 1;

        } else if (strcmp(argv[i], "-dir") == 0) {
            if (i + 1 >= argc) {
                exit_failure(argv, "Error: -dir requires an argument.\n\n");
            }
            dir = argv[++i];

        } else {
            exit_failure(argv, "Error: unknown parameter '%s'.\n\n", argv[i]);
        }
    }

    /* ── Validate mandatory parameters are present ──────────── */
    if (mode == 0) {
        exit_failure(argv, "Error: must specify either -d (distribute) or -r (recover).\n\n");
    }
    if (!secret_set) {
        exit_failure(argv, "Error: -secret is required.\n\n");
    }
    if (!k_set) {
        exit_failure(argv, "Error: -k is required.\n\n");
    }

    /* ── -n is only allowed with -d ─────────────────────────── */
    if (n_set && mode == 'r') {
        exit_failure(argv, "Error: -n can only be used with -d (distribute).\n\n");
    }

    /* ── Validate secret filename ends in .bmp ──────────────── */
    if (!has_bmp_extension(secret)) {
        exit_failure(argv, "Error: secret image '%s' must have a .bmp extension.\n\n", secret);
    }

    /* ── On distribute, secret file must exist ──────────────── */
    if (mode == 'd') {
        FILE *f = fopen(secret, "rb");
        if (!f) {
            fprintf(stderr, "Error: secret image '%s' not found or cannot be opened.\n", secret);
            return SSS_IO;
        }
        fclose(f);
    }

    /* ── Validate k range ───────────────────────────────────── */
    if (k < K_MIN || k > K_MAX) {
        exit_failure(argv, "Error: k must be between %d and %d (got %d).\n\n", K_MIN, K_MAX, k);
    }

    /* ── Validate directory ─────────────────────────────────── */
    if (!directory_exists(dir)) {
        fprintf(stderr, "Error: directory '%s' does not exist or cannot be opened.\n", dir);
        return SSS_IO;
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
            return SSS_IO;
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
            return SSS_DATA;
        }
        if (n < k) {
            fprintf(stderr, "Error: n (%d) must be >= k (%d).\n", n, k);
            return SSS_DATA;
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
    /* ────────────────────────────────────────────────────────── */

    int rc = (mode == 'd') ? distribute(secret, k, n, dir)
                           : recovery(secret, k, dir);
    if (rc != SSS_OK) {
        return rc;   /* propagate the specific error category */
    }

    printf("Done :)\n");
    return SSS_OK;
}
